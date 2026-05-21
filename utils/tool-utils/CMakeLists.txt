gen_flatbuffer_cpp(
  TARGET_NAME toolutils_types_schema
  SCHEMA_FILE "../../schema/types.fbs"
  BIN_PATH_NAME "tool-utils/schema")

gen_flatbuffer_cpp(
  TARGET_NAME toolutils_igasset_schema
  SCHEMA_FILE "../../schema/igasset.fbs"
  BIN_PATH_NAME "tool-utils/schema")

set(tool-utils-headers
  "include/tool-utils/filesystem.h"
  "include/tool-utils/log-level.h"
  "include/tool-utils/runtime-timer.h")

set(tool-utils-sources
  "src/filesystem.cc"
  "src/log-level.cc"
  "src/runtime-timer.cc")

add_library(tool-utils STATIC ${tool-utils-headers} ${tool-utils-sources})
set_property(TARGET tool-utils PROPERTY CXX_STANDARD 23)
target_link_libraries(tool-utils PUBLIC spdlog::spdlog flatbuffers igasync toolutils_types_schema toolutils_igasset_schema picosha2)
target_include_directories(tool-utils PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
