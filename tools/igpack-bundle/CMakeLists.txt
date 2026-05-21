gen_flatbuffer_cpp(
  TARGET_NAME igpack_bundle_types_schema
  SCHEMA_FILE "../../schema/types.fbs"
  BIN_PATH_NAME "igpack-bundle/schema")

gen_flatbuffer_cpp(
  TARGET_NAME igpack_bundle_igasset_schema
  SCHEMA_FILE "../../schema/igasset.fbs"
  BIN_PATH_NAME "igpack-bundle/schema")

gen_flatbuffer_cpp(
  TARGET_NAME igpack_bundle_igpack_schema
  SCHEMA_FILE "../../schema/igpack.fbs"
  BIN_PATH_NAME "igpack-bundle/schema")

gen_flatbuffer_cpp(
  TARGET_NAME igpack_bundle_igpack_bundle_schema
  SCHEMA_FILE "../../schema/igpack-bundle-plan.fbs"
  BIN_PATH_NAME "igpack-bundle/schema")

set(igpack-bundle-headers
  "include/igpack-bundle/exec-config.h")

set(igpack-bundle-sources
  "main.cc")

add_executable(igpack-bundle ${igpack-bundle-headers} ${igpack-bundle-sources})
set_target_properties(igpack-bundle PROPERTIES CXX_STANDARD 23)
target_link_libraries(igpack-bundle
  PRIVATE igpack_bundle_types_schema igpack_bundle_igasset_schema igpack_bundle_igpack_schema igpack_bundle_igpack_bundle_schema
          CLI11::CLI11 spdlog::spdlog igasync igasset tool-utils)
target_include_directories(igpack-bundle
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
          "${CMAKE_BINARY_DIR}")

# Stage igpack-bundle-plan.fbs (and the two .fbs files it `include`s) next to
# the igpack-bundle binary so the tool can locate the schema with no -s flag.
# The FlatBuffers parser resolves includes relative to the schema's parent
# directory, so all three must live in the same folder.
set(_igpack_bundle_schema_files
  "${CMAKE_SOURCE_DIR}/schema/igpack-bundle-plan.fbs"
  "${CMAKE_SOURCE_DIR}/schema/igasset.fbs"
  "${CMAKE_SOURCE_DIR}/schema/types.fbs")
add_custom_command(TARGET igpack-bundle POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${_igpack_bundle_schema_files}
    "$<TARGET_FILE_DIR:igpack-bundle>"
  COMMENT "Staging igpack-bundle-plan.fbs (+ includes) next to igpack-bundle"
  VERBATIM)