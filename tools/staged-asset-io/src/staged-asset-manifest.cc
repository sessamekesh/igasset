#include <staged-asset-io/staged-asset-manifest.h>

#include <staged-asset-io/reference-normalize.h>
#include <staged-asset-io/schema/staged-asset-manifest-embedded.h>
#include <staged-asset-io/schema/staged-asset-manifest.h>
#include <tool-utils/embedded-schema.h>

#include <flatbuffers/idl.h>
#include <spdlog/logger.h>

#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace stagedio {

namespace {

// Physical names are expected to be bare staged file names (storage UUIDs) -
// no directories, no traversal segments, no absolute paths.
bool is_valid_physical_name(const std::string& physical) {
  if (physical.empty()) {
    return false;
  }
  if (physical.find('/') != std::string::npos) {
    return false;
  }
  if (physical.find('\\') != std::string::npos) {
    return false;
  }
  return physical != ".." && physical != ".";
}

}  // namespace

std::shared_ptr<const StagedAssetManifest> StagedAssetManifest::from_json(
    const std::string& manifest_json,
    const std::shared_ptr<spdlog::logger>& log) {
  flatbuffers::Parser parser;
  if (!toolutils::ParseEmbeddedSchema(
          parser, log, stagedio::embedded_schema::kRootSchemaFilename,
          stagedio::embedded_schema::kEmbeddedSchemaFiles)) {
    return nullptr;
  }

  if (!parser.Parse(manifest_json.c_str())) {
    log->error("Failed to parse staged asset manifest: {}", parser.error_);
    return nullptr;
  }

  const auto* manifest = IgAssetStaging::GetStagedAssetManifest(
      parser.builder_.GetBufferPointer());
  const auto* entries = (manifest != nullptr) ? manifest->entries() : nullptr;
  if (entries == nullptr) {
    log->error("Staged asset manifest has no entries");
    return nullptr;
  }

  auto out = std::shared_ptr<StagedAssetManifest>(new StagedAssetManifest());
  for (const auto* entry : *entries) {
    if (entry == nullptr || entry->logical_path() == nullptr ||
        entry->physical_path() == nullptr) {
      log->error("Staged asset manifest entry missing logical/physical path");
      return nullptr;
    }

    const std::string logical_raw = entry->logical_path()->str();
    auto logical = normalize_logical_reference("", logical_raw);
    if (!logical.has_value()) {
      log->error("Staged asset manifest entry has invalid logical path: {}",
                 logical_raw);
      return nullptr;
    }

    const std::string physical = entry->physical_path()->str();
    if (!is_valid_physical_name(physical)) {
      log->error("Staged asset manifest entry has invalid physical name: {}",
                 physical);
      return nullptr;
    }

    const auto [it, inserted] = out->entries_.emplace(*logical, physical);
    if (!inserted) {
      log->error("Staged asset manifest has duplicate logical path: {}",
                 it->first);
      return nullptr;
    }
  }

  return out;
}

std::shared_ptr<const StagedAssetManifest> StagedAssetManifest::load(
    const std::filesystem::path& manifest_file,
    const std::shared_ptr<spdlog::logger>& log) {
  std::ifstream fin(manifest_file, std::ios::binary);
  if (!fin) {
    log->error("Failed to open staged asset manifest {}",
               manifest_file.string());
    return nullptr;
  }

  std::string content((std::istreambuf_iterator<char>(fin)),
                      std::istreambuf_iterator<char>());
  return from_json(content, log);
}

const std::string* StagedAssetManifest::physical_for(
    const std::string& logical_path) const {
  const auto it = entries_.find(logical_path);
  return (it == entries_.end()) ? nullptr : &it->second;
}

std::shared_ptr<const StagedAssetManifest>
StagedAssetManifest::with_extra_entry(
    const std::string& logical_path, const std::string& physical_path,
    const std::shared_ptr<spdlog::logger>& log) const {
  auto logical = normalize_logical_reference("", logical_path);
  if (!logical.has_value()) {
    log->error("Extra manifest entry has invalid logical path: {}",
               logical_path);
    return nullptr;
  }

  if (!is_valid_physical_name(physical_path)) {
    log->error("Extra manifest entry has invalid physical name: {}",
               physical_path);
    return nullptr;
  }

  auto out =
      std::shared_ptr<StagedAssetManifest>(new StagedAssetManifest(*this));
  const auto [it, inserted] = out->entries_.emplace(*logical, physical_path);
  if (!inserted && it->second != physical_path) {
    log->error(
        "Extra manifest entry conflicts with an existing mapping for logical "
        "path: {} (already mapped to {})",
        it->first, it->second);
    return nullptr;
  }

  return out;
}

}  // namespace stagedio