cmake_minimum_required(VERSION 3.31.0)

project(
  IgAsset
  VERSION 0.1.0
  DESCRIPTION "IgAsset is a game asset bundle format and decoder optimized for the web"
  HOMEPAGE_URL "https://git.indigocode.dev/sessamekesh/igasset"
  LANGUAGES C CXX)

#
# Build configuration settings
set(IGASSET_BUILD_GEN_TOOLS "ON" CACHE BOOL "Build the generator tools")
set(IGASSET_BUILD_IGASSET_DECODER "ON" CACHE BOOL "Build the igasset decoder")
set(IGASSET_ENABLE_BASISU_SUPPORT "ON" CACHE BOOL "Build support for the Basis Universal image format")

set(IGASSET_TOOL_WRANGLE_PATH "${CMAKE_BINARY_DIR}/wrangle-tools.cmake" CACHE FILEPATH "Location of the native tool wrangling CMake file - native builds write to this path for neighboring WASM builds to use")

#
# Global configuration (not intended to be modified)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/import_build_tools.cmake)

add_subdirectory(extern)
include(cmake/gen_flatbuffer_cpp.cmake)
include(cmake/embed_fbs_schema.cmake)

add_subdirectory(igasset)
add_subdirectory(utils)

if (IGASSET_BUILD_GEN_TOOLS)
  add_subdirectory(tools)
endif ()

include(cmake/export_build_tools.cmake)
