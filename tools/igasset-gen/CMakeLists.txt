gen_flatbuffer_cpp(
  TARGET_NAME igasset_gen_types_schema
  SCHEMA_FILE "../../schema/types.fbs"
  BIN_PATH_NAME "igasset-gen/schema")

gen_flatbuffer_cpp(
  TARGET_NAME igasset_gen_igasset_schema
  SCHEMA_FILE "../../schema/igasset.fbs"
  BIN_PATH_NAME "igasset-gen/schema")

gen_flatbuffer_cpp(
  TARGET_NAME igasset_gen_igpack_schema
  SCHEMA_FILE "../../schema/igpack.fbs"
  BIN_PATH_NAME "igasset-gen/schema")

gen_flatbuffer_cpp(
  TARGET_NAME igasset_gen_plan_schema
  SCHEMA_FILE "../../schema/igasset-gen-plan.fbs"
  BIN_PATH_NAME "igasset-gen/schema")

set(igasset-gen-headers
  "include/igasset-gen/assimp-geo-processor.h"
  "include/igasset-gen/assimp-util.h"
  "include/igasset-gen/basisu-processor.h"
  "include/igasset-gen/exec-config.h"
  "include/igasset-gen/igasset-generator.h"
  "include/igasset-gen/inc-build.h"
  "include/igasset-gen/stb-parse.h"
  "include/igasset-gen/wgsl-copy-processor.h")

set(igasset-gen-sources
  "src/assimp-geo-processor.cc"
  "src/assimp-util.cc"
  "src/basisu-processor.cc"
  "src/igasset-generator.cc"
  "src/inc-build.cc"
  "src/stb-parse.cc"
  "src/wgsl-copy-processor.cc"
  "main.cc")

add_executable(igasset-gen ${igasset-gen-headers} ${igasset-gen-sources})
set_target_properties(igasset-gen PROPERTIES CXX_STANDARD 23)
target_link_libraries(igasset-gen
  PRIVATE igasset_gen_igasset_schema igasset_gen_igpack_schema igasset_gen_plan_schema igasset_gen_types_schema
          CLI11::CLI11 spdlog::spdlog igasync igasset tool-utils
          assimp draco_lib stb)
target_include_directories(igasset-gen
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include"
          "${CMAKE_BINARY_DIR}")
target_include_directories(igasset-gen SYSTEM
  PRIVATE "${draco_SOURCE_DIR}/src")

if (IGASSET_ENABLE_BASISU_SUPPORT)
  target_link_libraries(igasset-gen PRIVATE basisu_encoder)
  target_include_directories(igasset-gen SYSTEM PRIVATE "${basisu_SOURCE_DIR}")
  target_compile_definitions(igasset-gen PRIVATE IGASSET_ENABLE_BASISU_SUPPORT=1)
endif ()

# Stage igasset-gen-plan.fbs (and the two .fbs files it `include`s) next to
# the igasset-gen binary so the tool can locate the schema with no -s flag.
# The FlatBuffers parser resolves includes relative to the schema's parent
# directory, so all three must live in the same folder.
set(_igasset_gen_schema_files
  "${CMAKE_CURRENT_SOURCE_DIR}/../../schema/igasset-gen-plan.fbs"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../schema/igasset.fbs"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../schema/types.fbs")
add_custom_command(TARGET igasset-gen POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${_igasset_gen_schema_files}
    "$<TARGET_FILE_DIR:igasset-gen>"
  COMMENT "Staging igasset-gen-plan.fbs (+ includes) next to igasset-gen"
  VERBATIM)