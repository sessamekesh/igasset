#ifndef IGASSET_TOOL_UTILS_EMBEDDED_SCHEMA_H
#define IGASSET_TOOL_UTILS_EMBEDDED_SCHEMA_H

#include <flatbuffers/idl.h>
#include <spdlog/logger.h>

#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace toolutils {

// A {filename, contents} pair for a flatbuffers schema (`.fbs`) file whose
// text has been embedded into the binary at build time (see
// cmake/embed_fbs_schema.cmake).
using EmbeddedSchemaFile = std::pair<std::string_view, std::string_view>;

// Parses the schema named `root_schema_filename` (looked up in `files`) into
// `parser`. Any `include "...";` directives it contains are resolved against
// `files` instead of the filesystem, so this never touches disk - callers can
// ship a single binary with no *.fbs files alongside it.
bool ParseEmbeddedSchema(flatbuffers::Parser& parser,
                        const std::shared_ptr<spdlog::logger>& log,
                        std::string_view root_schema_filename,
                        std::span<const EmbeddedSchemaFile> files);

}  // namespace toolutils

#endif
