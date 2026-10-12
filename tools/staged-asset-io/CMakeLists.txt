gen_flatbuffer_cpp(
  TARGET_NAME staged_asset_io_manifest_schema
  SCHEMA_FILE "../../schema/staged-asset-manifest.fbs"
  BIN_PATH_NAME "staged-asset-io/schema")

# Embeds staged-asset-manifest.fbs into the binary as text, so manifest JSON
# files can be parsed with no schema (*.fbs) files on disk.
embed_fbs_schema(
  TARGET_NAME staged_asset_io_manifest_schema_embedded
  ROOT_SCHEMA_FILE "../../schema/staged-asset-manifest.fbs"
  SCHEMA_FILES "../../schema/staged-asset-manifest.fbs"
  BIN_PATH_NAME "staged-asset-io/schema"
  CPP_NAMESPACE "stagedio::embedded_schema")

set(staged-asset-io-headers
  "include/staged-asset-io/assimp-import.h"
  "include/staged-asset-io/assimp-io-system.h"
  "include/staged-asset-io/file-access-journal.h"
  "include/staged-asset-io/reference-normalize.h"
  "include/staged-asset-io/staged-asset-manifest.h"
  "include/staged-asset-io/staged-filesystem.h")

set(staged-asset-io-sources
  "src/assimp-import.cc"
  "src/assimp-io-system.cc"
  "src/reference-normalize.cc"
  "src/staged-asset-manifest.cc"
  "src/staged-filesystem.cc")

add_library(staged-asset-io STATIC ${staged-asset-io-headers} ${staged-asset-io-sources})
set_target_properties(staged-asset-io PROPERTIES CXX_STANDARD 23)
target_link_libraries(staged-asset-io
  PUBLIC assimp tool-utils spdlog::spdlog
          staged_asset_io_manifest_schema staged_asset_io_manifest_schema_embedded)
target_include_directories(staged-asset-io
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
         "${CMAKE_BINARY_DIR}")

# Lightweight self-test (plain checks, no framework) - run manually via
# build/tools/staged-asset-io/staged-asset-io-selftest
add_executable(staged-asset-io-selftest selftest.cc)
set_target_properties(staged-asset-io-selftest PROPERTIES CXX_STANDARD 23)
target_link_libraries(staged-asset-io-selftest PRIVATE staged-asset-io)