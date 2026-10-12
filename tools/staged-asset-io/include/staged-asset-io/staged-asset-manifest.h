#ifndef STAGED_ASSET_IO_STAGED_ASSET_MANIFEST_H
#define STAGED_ASSET_IO_STAGED_ASSET_MANIFEST_H

#include <spdlog/logger.h>

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace stagedio {

// An immutable logical->physical file mapping, loaded from manifest JSON
// (schema/staged-asset-manifest.fbs). See the schema file for the authoring
// contract.
//
// Keys are normalized logical paths; values are physical file names relative
// to the staging root.
class StagedAssetManifest {
 public:
  // Parse manifest content (JSON). Returns nullptr on validation failure
  // (details are logged).
  static std::shared_ptr<const StagedAssetManifest> from_json(
      const std::string& manifest_json,
      const std::shared_ptr<spdlog::logger>& log);

  // Load + parse a manifest file from disk. Returns nullptr on failure.
  static std::shared_ptr<const StagedAssetManifest> load(
      const std::filesystem::path& manifest_file,
      const std::shared_ptr<spdlog::logger>& log);

  // Returns the physical file name (relative to the staging root) for a
  // normalized logical path, or nullptr when unmapped. Pure lookup - this
  // never touches the filesystem.
  const std::string* physical_for(const std::string& logical_path) const;

  // Returns a copy of this manifest with one additional (logical ->
  // physical) mapping, validated exactly like a loaded entry (the logical
  // path must normalize; the physical name must be a bare file name).
  // Used by tools that must process an input whose logical name is not in
  // the orchestrator-authored manifest (e.g. extract-asset-metadata
  // --logical-name): synthesize a runtime entry so --deep can import the
  // file through the staged IOSystem.
  //
  // Returns nullptr (and logs) when the extra entry is invalid or maps a
  // logical path that is already mapped to a DIFFERENT physical file. An
  // identical duplicate mapping is tolerated (the entry already exists).
  // This manifest is never modified.
  std::shared_ptr<const StagedAssetManifest> with_extra_entry(
      const std::string& logical_path, const std::string& physical_path,
      const std::shared_ptr<spdlog::logger>& log) const;

  std::size_t size() const {
    return entries_.size();
  }

 private:
  StagedAssetManifest() = default;

  std::map<std::string, std::string> entries_;
};

}  // namespace stagedio

#endif