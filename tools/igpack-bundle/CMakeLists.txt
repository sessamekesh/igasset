gen_flatbuffer_cpp(
  TARGET_NAME igpack_bundle_types_schema
  SCHEMA_FILE "../../schema/types.fbs"
  BIN_PATH_NAME "igpack-bundle/schema")

gen_flatbuffer_cpp(
  TARGET_NAME igpack_bundle_igasset_schema
  SCHEMA_FILE "../../schema/igasset.fbs"
  BIN_PATH_NAME "igpack-bundle/schema"
  SCHEMA_TARGETS igpack_bundle_types_schema)

gen_flatbuffer_cpp(
  TARGET_NAME igpack_bundle_igpack_schema
  SCHEMA_FILE "../../schema/igpack.fbs"
  BIN_PATH_NAME "igpack-bundle/schema"
  SCHEMA_TARGETS igpack_bundle_igasset_schema)

gen_flatbuffer_cpp(
  TARGET_NAME igpack_bundle_igpack_bundle_schema
  SCHEMA_FILE "../../schema/igpack-bundle-plan.fbs"
  BIN_PATH_NAME "igpack-bundle/schema"
  SCHEMA_TARGETS igpack_bundle_igasset_schema)

# Embeds igpack-bundle-plan.fbs (+ the two .fbs files it `include`s) into the
# binary as text, so igpack-bundle can parse *.igpack-bundle-plan.json files
# with no schema (*.fbs) files on disk and no -s/--schema flag.
embed_fbs_schema(
  TARGET_NAME igpack_bundle_plan_schema_embedded
  ROOT_SCHEMA_FILE "../../schema/igpack-bundle-plan.fbs"
  SCHEMA_FILES
    "../../schema/igpack-bundle-plan.fbs"
    "../../schema/igasset.fbs"
    "../../schema/types.fbs"
  BIN_PATH_NAME "igpack-bundle/schema"
  CPP_NAMESPACE "igpackbundle::embedded_schema")

set(igpack-bundle-headers
  "include/igpack-bundle/exec-config.h")

set(igpack-bundle-sources
  "main.cc")

add_executable(igpack-bundle ${igpack-bundle-headers} ${igpack-bundle-sources})
set_target_properties(igpack-bundle PROPERTIES CXX_STANDARD 23)
target_link_libraries(igpack-bundle
  PRIVATE igpack_bundle_types_schema igpack_bundle_igasset_schema igpack_bundle_igpack_schema igpack_bundle_igpack_bundle_schema
          igpack_bundle_plan_schema_embedded
          CLI11::CLI11 spdlog::spdlog igasync igasset tool-utils)
target_include_directories(igpack-bundle
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
          "${CMAKE_BINARY_DIR}")