cmake_minimum_required(VERSION 3.31.0)

project(
  IgAsset
  VERSION 0.1.0
  DESCRIPTION "IgAsset is a game asset bundle format and decoder optimized for the web"
  HOMEPAGE_URL "https://git.indigocode.dev/sessamekesh/igasset"
  LANGUAGES C CXX)

#
# Build configuration settings
set(IGASSET_BUILD_IGASSET_GEN "ON" CACHE BOOL "Build the igasset-gen tool")
set(IGASSET_BUILD_IGPACK_GEN "ON" CACHE BOOL "Build the igpack-gen tool")
set(IGASSET_BUILD_IGASSET_DECODER "ON" CACHE BOOL "Build the igasset decoder")
set(IGASSET_ENABLE_BASISU_SUPPORT "ON" CACHE BOOL "Build support for the Basis Universal image format")

add_subdirectory(extern)

include(cmake/gen_flatbuffer_cpp.cmake)

add_subdirectory(igasset)
