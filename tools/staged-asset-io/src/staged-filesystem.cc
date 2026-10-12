#include <staged-asset-io/staged-filesystem.h>

#include <spdlog/logger.h>
#include <staged-asset-io/reference-normalize.h>

#include <utility>

namespace stagedio {

StagedFilesystem::StagedFilesystem(
    IoPolicy policy, std::filesystem::path input_root,
    std::filesystem::path output_root,
    std::shared_ptr<const StagedAssetManifest> manifest,
    std::shared_ptr<FileAccessJournal> journal,
    std::shared_ptr<spdlog::logger> log)
    : policy_(policy),
      input_root_(std::move(input_root)),
      output_root_(std::move(output_root)),
      manifest_(std::move(manifest)),
      journal_(std::move(journal)),
      log_(log->clone("StagedFilesystem")),
      underlying_(log) {}

std::shared_ptr<StagedFilesystem> StagedFilesystem::direct(
    std::filesystem::path input_root, std::filesystem::path output_root,
    std::shared_ptr<spdlog::logger> log) {
  return std::shared_ptr<StagedFilesystem>(new StagedFilesystem(
      IoPolicy::Direct, std::move(input_root), std::move(output_root), nullptr,
      std::make_shared<FileAccessJournal>(), std::move(log)));
}

std::shared_ptr<StagedFilesystem> StagedFilesystem::staged(
    std::filesystem::path input_root, std::filesystem::path output_root,
    std::shared_ptr<const StagedAssetManifest> manifest,
    std::shared_ptr<spdlog::logger> log) {
  if (manifest == nullptr) {
    log->error("Cannot create a staged filesystem without a manifest");
    return nullptr;
  }

  return std::shared_ptr<StagedFilesystem>(new StagedFilesystem(
      IoPolicy::Staged, std::move(input_root), std::move(output_root),
      std::move(manifest), std::make_shared<FileAccessJournal>(),
      std::move(log)));
}

std::optional<std::filesystem::path> StagedFilesystem::resolve_input(
    const std::string& logical_input_path) const {
  if (policy_ == IoPolicy::Direct) {
    // Historical igasset-gen behavior: plain operator/ joining, where an
    // absolute path replaces the root entirely. Intentionally unchecked -
    // this policy is for trusted inputs only.
    journal_->record(logical_input_path, true);
    return input_root_ / std::filesystem::path(logical_input_path);
  }

  auto logical = normalize_logical_reference("", logical_input_path);
  if (!logical.has_value()) {
    journal_->record(logical_input_path, false);
    log_->warn("Denied input read: invalid logical path {}",
               logical_input_path);
    return std::nullopt;
  }

  const std::string* physical = manifest_->physical_for(*logical);
  if (physical == nullptr) {
    journal_->record(*logical, false);
    log_->warn("Denied input read: {} is not in the staged manifest", *logical);
    return std::nullopt;
  }

  journal_->record(*logical, true);
  return input_root_ / std::filesystem::path(*physical);
}

std::optional<std::filesystem::path> StagedFilesystem::resolve_output(
    const std::string& logical_output_path) const {
  if (policy_ == IoPolicy::Direct) {
    // Historical igasset-gen behavior - see resolve_input.
    return output_root_ / std::filesystem::path(logical_output_path);
  }

  auto logical = normalize_logical_reference("", logical_output_path);
  if (!logical.has_value()) {
    log_->warn("Denied output write: invalid logical path {}",
               logical_output_path);
    return std::nullopt;
  }

  return output_root_ / std::filesystem::path(*logical);
}

std::optional<std::string> StagedFilesystem::read_input(
    const std::string& logical_input_path) const {
  auto physical = resolve_input(logical_input_path);
  if (!physical.has_value()) {
    return std::nullopt;
  }
  return underlying_.read_bin(*physical);
}

bool StagedFilesystem::write_output(const std::string& logical_output_path,
                                    flatbuffers::FlatBufferBuilder& fbb) const {
  auto physical = resolve_output(logical_output_path);
  if (!physical.has_value()) {
    return false;
  }
  return underlying_.write_bin(*physical, fbb);
}

}  // namespace stagedio