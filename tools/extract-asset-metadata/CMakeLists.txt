gen_flatbuffer_cpp(
  TARGET_NAME extract_asset_metadata_types_schema
  SCHEMA_FILE "../../schema/types.fbs"
  BIN_PATH_NAME "extract-asset-metadata/schema")

gen_flatbuffer_cpp(
  TARGET_NAME extract_asset_metadata_igasset_schema
  SCHEMA_FILE "../../schema/igasset.fbs"
  BIN_PATH_NAME "extract-asset-metadata/schema"
  SCHEMA_TARGETS extract_asset_metadata_types_schema)

gen_flatbuffer_cpp(
  TARGET_NAME extract_asset_metadata_igpack_schema
  SCHEMA_FILE "../../schema/igpack.fbs"
  BIN_PATH_NAME "extract-asset-metadata/schema"
  SCHEMA_TARGETS extract_asset_metadata_igasset_schema)

set(extract-asset-metadata-headers
  "include/extract-asset-metadata/detect.h"
  "include/extract-asset-metadata/metadata.h"
  "include/extract-asset-metadata/report.h"
  "include/extract-asset-metadata/utf8.h")

set(extract-asset-metadata-sources
  "main.cc"
  "src/deep-import.cc"
  "src/detect-image.cc"
  "src/detect-model.cc"
  "src/detect-shader.cc"
  "src/report-json.cc"
  "src/report-text.cc")

add_executable(extract-asset-metadata
  ${extract-asset-metadata-headers} ${extract-asset-metadata-sources})
set_target_properties(extract-asset-metadata PROPERTIES CXX_STANDARD 23)
target_link_libraries(extract-asset-metadata
  PRIVATE extract_asset_metadata_types_schema
          extract_asset_metadata_igasset_schema
          extract_asset_metadata_igpack_schema
          CLI11::CLI11 spdlog::spdlog picosha2 tool-utils staged-asset-io
          assimp stb)
target_include_directories(extract-asset-metadata
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include"
          "${CMAKE_BINARY_DIR}")