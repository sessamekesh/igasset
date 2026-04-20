include(FetchContent)

#
# spdlog - used for logging (used in all Indigo Code projects)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY "https://github.com/gabime/spdlog/"
  GIT_TAG "v1.16.0"
)
FetchContent_MakeAvailable(spdlog)

#
# flatbuffers - required for all tools and decoder libraries (igasset / igpack files are encoded using flatbuffers)
if (EMSCRIPTEN)
  set(FLATBUFFERS_BUILD_FLATC OFF CACHE BOOL "" FORCE)
endif ()
FetchContent_Declare(
    flatbuffers
    GIT_REPOSITORY "https://github.com/google/flatbuffers"
    GIT_TAG "v25.12.19"
)
FetchContent_MakeAvailable(flatbuffers)

#
# igasync - browser WebAssembly-friendly async task execution library
if (NOT TARGET igasync)
  set(IGASYNC_BUILD_TESTS "OFF" CACHE BOOL "Build unit tests")
  FetchContent_Declare(
    igasync
    GIT_REPOSITORY "https://git.indigocode.dev/sessamekesh/igasync"
    GIT_TAG "70718c036ae7670e4db3724ec61c6652e9b9b62b"
  )
  FetchContent_MakeAvailable(igasync)
endif ()

#
# Draco (compressed format for 3D geometry data)
if (NOT TARGET draco)
  set(DRACO_JS_GLUE OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
      draco
      GIT_REPOSITORY "https://github.com/google/draco"
      GIT_TAG "1.5.7"
  )
  FetchContent_MakeAvailable(draco)
  if (MSVC)
    add_library(draco_lib ALIAS draco)
  else ()
    add_library(draco_lib ALIAS draco_static)
  endif ()
endif ()

#
# Basisu (compressed format for GPU textures)
if (NOT TARGET basisu AND IGASSET_ENABLE_BASISU_SUPPORT)
  # Basisu's MSVC path enables SSE but doesn't pass -msse4.1, which clang-cl requires explicitly.
  if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(BASISU_SSE OFF CACHE BOOL "" FORCE)
  endif ()
  FetchContent_Declare(
      basisu
      GIT_REPOSITORY "https://github.com/BinomialLLC/basis_universal"
      GIT_TAG "v2_0_3"
  )
  FetchContent_MakeAvailable(basisu)
endif ()

#
# GLM (math library used for 3D graphics)
if (NOT TARGET glm)
  FetchContent_Declare(
    glm
    GIT_REPOSITORY "https://github.com/g-truc/glm"
    GIT_TAG "1.0.3"
  )
  FetchContent_MakeAvailable(glm)
endif ()

#
# Tooling-only dependencies
if (NOT EMSCRIPTEN AND IGASSET_BUILD_GEN_TOOLS)
  #
  # CLI11 - Used for parsing command-line arguments in generator tools
  if (NOT TARGET cli11)
    FetchContent_Declare(
      cli11
      GIT_REPOSITORY "https://github.com/CLIUtils/CLI11"
      GIT_TAG "v2.6.1"
    )
    FetchContent_MakeAvailable(cli11)
  endif ()

  if (NOT TARGET assimp)
    set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
    set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    # clang-cl drives llvm-rc, which rejects the \xA9 byte in assimp's
    # VERSIONINFO copyright string. Rewrite revision.h.in to use "(C)".
    set(_assimp_patch_script
        "${CMAKE_CURRENT_LIST_DIR}/patches/assimp-fix-llvm-rc.cmake")
    FetchContent_Declare(
        assimp
        GIT_REPOSITORY "https://github.com/assimp/assimp"
        GIT_TAG "v6.0.4"
        PATCH_COMMAND ${CMAKE_COMMAND}
            "-DREVISION_H_IN=<SOURCE_DIR>/include/assimp/revision.h.in"
            -P "${_assimp_patch_script}"
        UPDATE_DISCONNECTED TRUE
    )
    FetchContent_MakeAvailable(assimp)
  endif ()

  #
  # STB (tooling only - used for basic image loading / simple processing)
  add_subdirectory(stb)

  #
  # PicoSHA2 - header-only SHA256 implementation, used in utilities/testing
  FetchContent_Declare(
    picosha2
    GIT_REPOSITORY "https://github.com/okdshin/PicoSHA2"
    GIT_TAG "v1.0.1"
  )
  FetchContent_MakeAvailable(picosha2)
endif ()
