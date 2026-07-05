#include <tool-utils/embedded-schema.h>

#include <flatbuffers/util.h>

#include <filesystem>

namespace toolutils {

namespace {

// FlatBuffers' LoadFileFunction is a plain function pointer (no user-data
// parameter), so the table being resolved has to live somewhere static for
// the duration of the Parse() call below.
std::span<const EmbeddedSchemaFile> g_active_embedded_files;

// `name` may arrive as a bare filename or joined with an (empty) include
// path - normalize to the filename so it matches the embedded table.
const std::string_view* FindEmbeddedFile(const char* name) {
  std::string requested_name = std::filesystem::path(name).filename().string();
  for (const auto& [filename, contents] : g_active_embedded_files) {
    if (filename == requested_name) {
      return &contents;
    }
  }
  return nullptr;
}

bool LoadEmbeddedFile(const char* name, bool /*binary*/, std::string* buf) {
  const std::string_view* contents = FindEmbeddedFile(name);
  if (contents == nullptr) {
    return false;
  }
  *buf = std::string(*contents);
  return true;
}

// FlatBuffers' Parser::DoParse hashes an already-`include`d file with its
// contents folded in only when FileExists() reports it as real; otherwise it
// hashes the filename alone (see idl_parser.cpp's "if the file is in-memory"
// comment). Since embedded schemas never exist on disk, leaving FileExists()
// at its default (always false for us) makes that hash disagree with the one
// computed by the include-site caller (which always folds in contents) - the
// "have we already parsed this include" check then never matches, and
// Parser::DoParse recurses on the same include forever until the stack
// overflows. Reporting embedded files as "existing" keeps both hashes
// computed the same way.
bool EmbeddedFileExists(const char* name) {
  return FindEmbeddedFile(name) != nullptr;
}

}  // namespace

bool ParseEmbeddedSchema(flatbuffers::Parser& parser,
                        const std::shared_ptr<spdlog::logger>& log,
                        std::string_view root_schema_filename,
                        std::span<const EmbeddedSchemaFile> files) {
  const std::string_view* root_contents = nullptr;
  for (const auto& [filename, contents] : files) {
    if (filename == root_schema_filename) {
      root_contents = &contents;
      break;
    }
  }
  if (root_contents == nullptr) {
    log->error("Embedded schema {} not found", root_schema_filename);
    return false;
  }

  std::string const root_schema_filename_str(root_schema_filename);

  g_active_embedded_files = files;
  auto* previous_load_fn =
      flatbuffers::SetLoadFileFunction(&LoadEmbeddedFile);
  auto* previous_exists_fn =
      flatbuffers::SetFileExistsFunction(&EmbeddedFileExists);
  bool const ok = parser.Parse(root_contents->data(), /* include_paths= */ nullptr,
                               root_schema_filename_str.c_str());
  flatbuffers::SetFileExistsFunction(previous_exists_fn);
  flatbuffers::SetLoadFileFunction(previous_load_fn);
  g_active_embedded_files = {};

  if (!ok) {
    log->error("Failed to parse embedded schema {}: {}", root_schema_filename,
               parser.error_);
  }
  return ok;
}

}  // namespace toolutils
