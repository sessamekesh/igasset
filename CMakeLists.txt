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

#
# Global configuration (not intended to be modified)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(extern)

include(cmake/gen_flatbuffer_cpp.cmake)

add_subdirectory(igasset)

if (IGASSET_BUILD_GEN_TOOLS)
  add_subdirectory(tools)
endif ()
