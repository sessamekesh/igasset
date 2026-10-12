#ifndef STAGED_ASSET_IO_FILE_ACCESS_JOURNAL_H
#define STAGED_ASSET_IO_FILE_ACCESS_JOURNAL_H

#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace stagedio {

// One recorded attempt to access an input file by logical name.
struct FileAccessRecord {
  // Logical path as requested (post-normalization where a normalization was
  // applied).
  std::string logical_path;

  // True when access was permitted (manifest hit in staged mode; always in
  // direct mode), false when denied.
  bool allowed;
};

// Thread-safe record of every attempted logical input-file access made
// through a StagedFilesystem or an Assimp IOSystem adapter. Used by
// extract-asset-metadata for dependency extraction and by igasset-gen for
// diagnostics. Output writes are not recorded - only input reads.
//
// Shared ownership: the journal is expected to OUTLIVE the Assimp::IOSystem
// instances that record into it (Assimp::Importer owns and deletes its
// IOSystem).
class FileAccessJournal {
 public:
  void record(std::string logical_path, bool allowed) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.push_back(FileAccessRecord{std::move(logical_path), allowed});
  }

  std::vector<FileAccessRecord> snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<FileAccessRecord> entries_;
};

}  // namespace stagedio

#endif