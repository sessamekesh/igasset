#include <staged-asset-io/assimp-io-system.h>

#include <spdlog/logger.h>
#include <staged-asset-io/reference-normalize.h>

#include <assimp/types.h>
#include <memory>
#include <string_view>
#include <utility>

namespace stagedio {

//
// StagedIOStream
//

StagedIOStream::StagedIOStream(const std::filesystem::path& physical_path)
    : stream_(physical_path, std::ios::binary), size_(0) {
  if (stream_.is_open()) {
    stream_.seekg(0, std::ios::end);
    size_ = static_cast<std::size_t>(stream_.tellg());
    stream_.seekg(0, std::ios::beg);
  }
}

std::size_t StagedIOStream::Read(void* buffer, std::size_t size,
                                 std::size_t count) {
  if (size == 0 || count == 0 || !stream_.is_open()) {
    return 0;
  }

  stream_.read(reinterpret_cast<char*>(buffer),
               static_cast<std::streamsize>(size * count));
  return static_cast<std::size_t>(stream_.gcount()) / size;
}

aiReturn StagedIOStream::Seek(std::size_t offset, aiOrigin origin) {
  if (!stream_.is_open()) {
    return aiReturn_FAILURE;
  }

  stream_.clear();
  switch (origin) {
    case aiOrigin_SET:
      stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
      break;
    case aiOrigin_CUR:
      stream_.seekg(static_cast<std::streamoff>(offset), std::ios::cur);
      break;
    case aiOrigin_END:
      // Note: assimp passes a positive offset relative to the end.
      stream_.seekg(-static_cast<std::streamoff>(offset), std::ios::end);
      break;
    default:
      return aiReturn_FAILURE;
  }

  return stream_ ? aiReturn_SUCCESS : aiReturn_FAILURE;
}

std::size_t StagedIOStream::Tell() const {
  return static_cast<std::size_t>(stream_.tellg());
}

//
// AssimpStagedIOSystem
//

AssimpStagedIOSystem::AssimpStagedIOSystem(
    std::filesystem::path staging_root,
    std::shared_ptr<const StagedAssetManifest> manifest,
    std::shared_ptr<FileAccessJournal> journal,
    std::shared_ptr<spdlog::logger> log, std::string base_dir)
    : staging_root_(std::move(staging_root)),
      manifest_(std::move(manifest)),
      journal_(std::move(journal)),
      log_(std::move(log)->clone("AssimpStagedIOSystem")),
      base_dir_(std::move(base_dir)) {}

std::optional<std::pair<std::string, std::filesystem::path>>
AssimpStagedIOSystem::resolve(const char* file) const {
  if (file == nullptr) {
    return std::nullopt;
  }

  // Step 1: resolve the reference relative to the model's logical parent
  // directory (spec behavior - a model's references live alongside it).
  auto relative = normalize_logical_reference(base_dir_, file);

  // Step 2: the request may already be fully qualified (the glTF importers
  // prepend the model's directory themselves before calling Open, and the
  // main-file open passes the full logical name).
  auto qualified = normalize_logical_reference("", file);

  // Prefer the model-relative interpretation, then the qualified one.
  if (relative.has_value()) {
    const std::string* physical = manifest_->physical_for(*relative);
    if (physical != nullptr) {
      journal_->record(*relative, true);
      return std::make_pair(*relative,
                            staging_root_ / std::filesystem::path(*physical));
    }
  }
  if (qualified.has_value() &&
      (!relative.has_value() || *qualified != *relative)) {
    const std::string* physical = manifest_->physical_for(*qualified);
    if (physical != nullptr) {
      journal_->record(*qualified, true);
      return std::make_pair(*qualified,
                            staging_root_ / std::filesystem::path(*physical));
    }
  }

  // Denied. Record the most accurate logical name for the orchestrator:
  // - a request that already carried the model's directory prefix (glTF
  //   self-prefixing, the OBJ importer's basename fallback, the main-file
  //   open) is reported under its qualified interpretation;
  // - a raw reference is reported under its model-relative interpretation
  //   (where the orchestrator is expected to map it).
  const bool request_qualified =
      qualified.has_value() &&
      (base_dir_.empty() || qualified->rfind(base_dir_, 0) == 0);
  if (request_qualified) {
    journal_->record(*qualified, false);
    log_->warn("Denied Assimp file access: {} is not in the staged manifest",
               *qualified);
  } else if (relative.has_value()) {
    journal_->record(*relative, false);
    log_->warn("Denied Assimp file access: {} is not in the staged manifest",
               *relative);
  } else {
    journal_->record(file, false);
    log_->warn("Denied Assimp file access: invalid logical path {}", file);
  }
  return std::nullopt;
}

bool AssimpStagedIOSystem::Exists(const char* file) const {
  if (file == nullptr) {
    return false;
  }
  auto relative = normalize_logical_reference(base_dir_, file);
  if (relative.has_value() && manifest_->physical_for(*relative) != nullptr) {
    return true;
  }
  auto qualified = normalize_logical_reference("", file);
  return qualified.has_value() &&
         manifest_->physical_for(*qualified) != nullptr;
}

Assimp::IOStream* AssimpStagedIOSystem::Open(const char* file,
                                             const char* mode) {
  if (mode != nullptr &&
      std::string_view(mode).find('r') == std::string_view::npos) {
    // Staged inputs are read-only.
    return nullptr;
  }

  auto physical = resolve(file);
  if (!physical.has_value()) {
    return nullptr;
  }

  auto stream =
      std::unique_ptr<StagedIOStream>(new StagedIOStream(physical->second));
  if (!stream->opened()) {
    log_->error("Staged file mapped from logical path {} could not be opened",
                physical->first);
    return nullptr;
  }

  return stream.release();
}

void AssimpStagedIOSystem::Close(Assimp::IOStream* file) {
  delete file;
}

bool AssimpStagedIOSystem::ComparePaths(const char* one,
                                        const char* second) const {
  if (one == nullptr || second == nullptr) {
    return false;
  }
  auto one_rel = normalize_logical_reference(base_dir_, one);
  auto second_rel = normalize_logical_reference(base_dir_, second);
  if (one_rel.has_value() && second_rel.has_value()) {
    return *one_rel == *second_rel;
  }
  auto one_norm = normalize_logical_reference("", one);
  auto second_norm = normalize_logical_reference("", second);
  return one_norm.has_value() && second_norm.has_value() &&
         *one_norm == *second_norm;
}

//
// AssimpRecordingIOSystem
//

AssimpRecordingIOSystem::AssimpRecordingIOSystem(
    std::shared_ptr<FileAccessJournal> journal)
    : journal_(std::move(journal)) {}

bool AssimpRecordingIOSystem::Exists(const char* file) const {
  return default_io_.Exists(file);
}

char AssimpRecordingIOSystem::getOsSeparator() const {
  return default_io_.getOsSeparator();
}

Assimp::IOStream* AssimpRecordingIOSystem::Open(const char* file,
                                                const char* mode) {
  if (file != nullptr) {
    journal_->record(file, true);
  }
  return default_io_.Open(file, mode);
}

void AssimpRecordingIOSystem::Close(Assimp::IOStream* file) {
  default_io_.Close(file);
}

bool AssimpRecordingIOSystem::ComparePaths(const char* one,
                                           const char* second) const {
  return default_io_.ComparePaths(one, second);
}

}  // namespace stagedio