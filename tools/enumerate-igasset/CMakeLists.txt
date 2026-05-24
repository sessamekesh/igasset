gen_flatbuffer_cpp(
  TARGET_NAME enumerate_igasset_types_schema
  SCHEMA_FILE "../../schema/types.fbs"
  BIN_PATH_NAME "enumerate-igasset/schema")

gen_flatbuffer_cpp(
  TARGET_NAME enumerate_igasset_igasset_schema
  SCHEMA_FILE "../../schema/igasset.fbs"
  BIN_PATH_NAME "enumerate-igasset/schema"
  SCHEMA_TARGETS enumerate_igasset_types_schema)

set(enumerate-igasset-target-sources
  "main.cc")

add_executable(enumerate-igasset ${enumerate-igasset-target-sources})
set_target_properties(enumerate-igasset PROPERTIES CXX_STANDARD 23)
target_link_libraries(enumerate-igasset
  PRIVATE enumerate_igasset_types_schema enumerate_igasset_igasset_schema
          igasset CLI11::CLI11 spdlog::spdlog picosha2 tool-utils)
