#ifndef STAGED_ASSET_IO_ASSIMP_IO_SYSTEM_H
#define STAGED_ASSET_IO_ASSIMP_IO_SYSTEM_H

#include <staged-asset-io/file-access-journal.h>
#include <staged-asset-io/staged-asset-manifest.h>

#include <assimp/DefaultIOSystem.h>
#include <spdlog/logger.h>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

namespace stagedio {

// IOStream backed by an std::ifstream - used by AssimpStagedIOSystem (the
// stock DefaultIOStream only pairs with DefaultIOSystem).
class StagedIOStream final : public Assimp::IOStream {
 public:
  explicit StagedIOStream(const std::filesystem::path& physical_path);
  ~StagedIOStream() override = default;

  bool opened() const {
    return stream_.is_open();
  }

  size_t Read(void* buffer, size_t size, size_t count) override;
  size_t Write(const void*, size_t, size_t) override {
    return 0;
  }
  aiReturn Seek(size_t offset, aiOrigin origin) override;
  size_t Tell() const override;
  size_t FileSize() const override {
    return size_;
  }
  void Flush() override {}

 private:
  // mutable: the Assimp IOStream interface declares Tell() const, so the
  // underlying stream must be queryable from const methods.
  mutable std::ifstream stream_;
  size_t size_;
};

// Assimp IOSystem for staged (untrusted) input. Every file Assimp tries to
// open - the main asset plus any referenced files (glTF buffer/image uris,
// OBJ mtllib, ...) - is resolved as a logical path through the manifest to a
// physical file under staging_root. Unmapped names are denied and recorded,
// never probed on the filesystem: the manifest acts as a capability list.
//
// base_dir is the logical parent directory of the model being imported (e.g.
// "models/" when importing "models/scene.obj"). Assimp importers are
// inconsistent about reference qualification: the OBJ importer opens the
// raw mtllib reference ("scene.mtl"), while the glTF importers prepend the
// model's own directory themselves ("models/scene.bin"). Requests are
// therefore resolved in two steps: first relative to base_dir (spec
// behavior - a model's references resolve in the model's directory), then
// as an already-qualified logical path. The first step that hits the
// manifest wins; denials record the base_dir-resolved name (or the raw
// request when it cannot be normalized).
//
// NOTE: Assimp::Importer takes ownership of (and deletes) its IOSystem. Any
// state that must outlive the import (the journal) is shared, not owned by
// the IOSystem.
class AssimpStagedIOSystem final : public Assimp::IOSystem {
 public:
  AssimpStagedIOSystem(std::filesystem::path staging_root,
                       std::shared_ptr<const StagedAssetManifest> manifest,
                       std::shared_ptr<FileAccessJournal> journal,
                       std::shared_ptr<spdlog::logger> log,
                       std::string base_dir = "");
  ~AssimpStagedIOSystem() override = default;

  bool Exists(const char* file) const override;
  char getOsSeparator() const override {
    return '/';
  }
  Assimp::IOStream* Open(const char* file, const char* mode) override;
  void Close(Assimp::IOStream* file) override;
  bool ComparePaths(const char* one, const char* second) const override;

 private:
  // Resolves an Assimp request to (normalized logical name, physical path).
  // Returns nullopt (recording a denial) when the request is invalid or
  // unmapped.
  std::optional<std::pair<std::string, std::filesystem::path>> resolve(
      const char* file) const;

  std::filesystem::path staging_root_;
  std::shared_ptr<const StagedAssetManifest> manifest_;
  std::shared_ptr<FileAccessJournal> journal_;
  std::shared_ptr<spdlog::logger> log_;
  std::string base_dir_;
};

// Assimp IOSystem for direct (trusted) input: behavior identical to the
// default IOSystem (which it delegates to), but records every file opened
// so dependency extraction works uniformly across both policies.
class AssimpRecordingIOSystem final : public Assimp::IOSystem {
 public:
  explicit AssimpRecordingIOSystem(std::shared_ptr<FileAccessJournal> journal);
  ~AssimpRecordingIOSystem() override = default;

  bool Exists(const char* file) const override;
  char getOsSeparator() const override;
  Assimp::IOStream* Open(const char* file, const char* mode) override;
  void Close(Assimp::IOStream* file) override;
  bool ComparePaths(const char* one, const char* second) const override;

 private:
  std::shared_ptr<FileAccessJournal> journal_;
  Assimp::DefaultIOSystem default_io_;
};

}  // namespace stagedio

#endif