gen_flatbuffer_cpp(
  TARGET_NAME enumerate_igpack_types_schema
  SCHEMA_FILE "../../schema/types.fbs"
  BIN_PATH_NAME "enumerate-igpack/schema")

gen_flatbuffer_cpp(
  TARGET_NAME enumerate_igpack_igasset_schema
  SCHEMA_FILE "../../schema/igasset.fbs"
  BIN_PATH_NAME "enumerate-igpack/schema"
  SCHEMA_TARGETS enumerate_igpack_types_schema)

gen_flatbuffer_cpp(
  TARGET_NAME enumerate_igpack_igpack_schema
  SCHEMA_FILE "../../schema/igpack.fbs"
  BIN_PATH_NAME "enumerate-igpack/schema"
  SCHEMA_TARGETS enumerate_igpack_igasset_schema)

set(enumerate-igpack-target-sources "main.cc")

add_executable(enumerate-igpack ${enumerate-igpack-target-sources})
set_target_properties(enumerate-igpack PROPERTIES CXX_STANDARD 23)
target_link_libraries(enumerate-igpack
  PRIVATE enumerate_igpack_types_schema enumerate_igpack_igasset_schema enumerate_igpack_igpack_schema
          igasset CLI11::CLI11 spdlog::spdlog picosha2 tool-utils)
