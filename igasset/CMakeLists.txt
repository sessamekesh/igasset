gen_flatbuffer_cpp(
  TARGET_NAME igasset_schema
  SCHEMA_FILE "../schema/igasset.fbs"
  BIN_PATH_NAME "igasset/schema")

gen_flatbuffer_cpp(
  TARGET_NAME igpack_schema
  SCHEMA_FILE "../schema/igpack.fbs"
  BIN_PATH_NAME "igasset/schema")

set(igasset_headers
  "include/igasset/generators/plane.h"
  "include/igasset/draco_dec.h"
  "include/igasset/fb_utils.h"
  "include/igasset/igpack_decoder.h"
  "include/igasset/image2d.h"
  "include/igasset/index_buffer.h"
  "include/igasset/vertex_types.h"
  "include/igasset/wgsl_source.h")

set(igasset_sources
  "src/generators/plane.cc"
  "src/draco_dec.cc"
  "src/fb_utils.cc"
  "src/igpack_decoder.cc"
  "src/image2d.cc"
  "src/index_buffer.cc"
  "src/wgsl_source.cc")

if (IGASSET_BUILD_IGASSET_DECODER)
  add_library(igasset STATIC ${igasset_headers} ${igasset_sources})
  set_property(TARGET igasset PROPERTY CXX_STANDARD 23)
  target_include_directories(igasset PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${draco_SOURCE_DIR}/src")
  target_include_directories(igasset PUBLIC "${CMAKE_BINARY_DIR}")
  if (IGASSET_ENABLE_BASISU_SUPPORT)
    target_include_directories(igasset PUBLIC "${basisu_SOURCE_DIR}")
    target_link_libraries(igasset PUBLIC basisu_encoder)
    target_compile_definitions(igasset PRIVATE IGASSET_ENABLE_BASISU_SUPPORT=1)
  else ()
    target_compile_definitions(igasset PRIVATE IGASSET_ENABLE_BASISU_SUPPORT=0)
  endif ()
  target_link_libraries(igasset
    PUBLIC igasset_schema igpack_schema spdlog::spdlog glm::glm draco_lib)
endif ()
