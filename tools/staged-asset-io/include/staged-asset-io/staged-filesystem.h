#ifndef STAGED_ASSET_IO_STAGED_FILESYSTEM_H
#define STAGED_ASSET_IO_STAGED_FILESYSTEM_H

#include <staged-asset-io/file-access-journal.h>
#include <staged-asset-io/staged-asset-manifest.h>
#include <tool-utils/filesystem.h>

#include <flatbuffers/flatbuffer_builder.h>
#include <spdlog/logger.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace stagedio {

// File access policy for a StagedFilesystem.
enum class IoPolicy {
  // Trusted inputs: plan paths are resolved with exactly the historical
  // igasset-gen behavior (std::filesystem::operator/ root joining - an
  // absolute path replaces the root - with no containment checks) and go
  // straight to the OS filesystem. For local development + existing tooling
  // operating on trusted files.
  Direct,

  // Untrusted inputs: deny-by-default. Input reads resolve logical names
  // through the manifest (capability list); output writes must be relative
  // paths contained within the output root. For user-uploaded files staged
  // by an orchestrating service.
  Staged,
};

// The single I/O boundary used by the IgAsset generator tools.
//
// All tools speak *logical* file names (the user-facing / remote-storage
// names carried by igasset-gen plans and referenced from inside asset
// files); this class owns the mapping from logical names to actual files on
// disk and enforces the access policy for that mapping:
//
//  - Direct: historical igasset-gen behavior, unchecked.
//  - Staged: logical -> manifest -> physical file under the input root.
//    Anything unmapped is denied and recorded, never probed on disk.
//
// Every attempted input access (allowed or denied) is recorded into the
// journal so callers can extract file dependencies (extract-asset-metadata)
// or diagnose denials (igasset-gen).
class StagedFilesystem {
 public:
  static std::shared_ptr<StagedFilesystem> direct(
      std::filesystem::path input_root, std::filesystem::path output_root,
      std::shared_ptr<spdlog::logger> log);

  // Returns nullptr if manifest is null.
  static std::shared_ptr<StagedFilesystem> staged(
      std::filesystem::path input_root, std::filesystem::path output_root,
      std::shared_ptr<const StagedAssetManifest> manifest,
      std::shared_ptr<spdlog::logger> log);

  IoPolicy policy() const {
    return policy_;
  }

  // Resolve a logical input name to the physical file that would be read.
  // Staged policy: nullopt (recorded as a denial) when the name is invalid
  // or unmapped. Direct policy: historical operator/ semantics - never
  // fails.
  std::optional<std::filesystem::path> resolve_input(
      const std::string& logical_input_path) const;

  // Resolve a logical output name to the physical file that would be
  // written. Staged policy: nullopt (recorded as a denial) unless the name
  // is a relative path contained within the output root. Direct policy:
  // historical operator/ semantics - never fails.
  std::optional<std::filesystem::path> resolve_output(
      const std::string& logical_output_path) const;

  // Read a logical input file as raw bytes. Returns nullopt on failure
  // (staged denials + unreadable files are logged).
  std::optional<std::string> read_input(
      const std::string& logical_input_path) const;

  // Write an igasset FlatBuffer to a logical output path. Output writes are
  // not recorded in the journal (they are not dependencies).
  bool write_output(const std::string& logical_output_path,
                    flatbuffers::FlatBufferBuilder& fbb) const;

  std::shared_ptr<FileAccessJournal> journal() const {
    return journal_;
  }

  // The manifest backing this filesystem, or nullptr in direct mode.
  const std::shared_ptr<const StagedAssetManifest>& manifest() const {
    return manifest_;
  }

  // The physical input root (direct: the trusted input root; staged: the
  // per-job staging directory holding the UUID-named files). Needed by
  // callers that construct Assimp IOSystem adapters alongside this
  // filesystem.
  const std::filesystem::path& input_root() const {
    return input_root_;
  }

 private:
  StagedFilesystem(IoPolicy policy, std::filesystem::path input_root,
                   std::filesystem::path output_root,
                   std::shared_ptr<const StagedAssetManifest> manifest,
                   std::shared_ptr<FileAccessJournal> journal,
                   std::shared_ptr<spdlog::logger> log);

  IoPolicy policy_;
  std::filesystem::path input_root_;
  std::filesystem::path output_root_;
  std::shared_ptr<const StagedAssetManifest> manifest_;
  std::shared_ptr<FileAccessJournal> journal_;
  std::shared_ptr<spdlog::logger> log_;
  toolutils::Filesystem underlying_;
};

}  // namespace stagedio

#endif