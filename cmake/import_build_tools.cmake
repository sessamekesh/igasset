if (EMSCRIPTEN)
  # WASM builds cannot (and should not) build flatc or any of the tools/* targets,
  #  so the generator tools - flatc above all - have to be resolved from a native
  #  build. They are resolved in the following preference order:
  #
  #   1. IGASSET_TOOL_WRANGLE_PATH config var
  #   2. A native build from any of the presets in `CMakePresets.txt`
  #   3. flatc discovered on the user's PATH.
  #   4. flatc built in isolation via ExternalProject (last-resort fallback).

  set(_igasset_wrangle_candidates "")
  if (IGASSET_TOOL_WRANGLE_PATH)
    list(APPEND _igasset_wrangle_candidates "${IGASSET_TOOL_WRANGLE_PATH}")
  endif ()

  set(_igasset_native_presets
    "windows-clang-cl-debug|Windows|Debug"
    "windows-clang-cl-release|Windows|Release"
    "windows-msvc-debug|Windows|Debug"
    "windows-msvc-release|Windows|Release"
    "linux-clang-debug|Linux|Debug"
    "linux-clang-release|Linux|Release")

  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_igasset_preferred_config "Debug")
  else ()
    set(_igasset_preferred_config "Release")
  endif ()


  # Create a _igasset_wrangle_candidates containing a list of platform-matching candidates
  #  in order of preference (matching debug/release or any)
  foreach (_pass IN ITEMS "${_igasset_preferred_config}" "ANY")
    foreach (_entry IN LISTS _igasset_native_presets)
      string(REPLACE "|" ";" _parts "${_entry}")
      list(GET _parts 0 _preset_name)
      list(GET _parts 1 _preset_host)
      list(GET _parts 2 _preset_config)
      if (NOT _preset_host STREQUAL CMAKE_HOST_SYSTEM_NAME)
        continue ()
      endif ()
      if (_pass STREQUAL "ANY" OR _pass STREQUAL _preset_config)
        list(APPEND _igasset_wrangle_candidates
          "${CMAKE_SOURCE_DIR}/build/${_preset_name}/wrangle-tools.cmake")
      endif ()
    endforeach ()
  endforeach ()

  # De-dup in case the user provides a candidate path that exists later...
  list(REMOVE_DUPLICATES _igasset_wrangle_candidates)

  # Try each candidate tool wrangling path, use it and break if one is found
  set(IGASSET_RESOLVED_TOOL_WRANGLE_PATH "")
  foreach (_candidate IN LISTS _igasset_wrangle_candidates)
    if (TARGET flatc)
      break ()
    endif ()
    if (NOT EXISTS "${_candidate}")
      continue ()
    endif ()

    # OPTIONAL keeps a malformed/empty file from aborting configuration; the
    # authoritative validity check is whether a flatc target actually appeared.
    include("${_candidate}" OPTIONAL RESULT_VARIABLE _wrangle_rsl)
    if (_wrangle_rsl STREQUAL "NOTFOUND")
      message(STATUS "igasset tools: could not load wrangle file ${_candidate}")
    elseif (TARGET flatc)
      message(STATUS "igasset tools: loaded native build tools from ${_candidate}")
      set(IGASSET_RESOLVED_TOOL_WRANGLE_PATH "${_candidate}")
    else ()
      message(STATUS "igasset tools: ${_candidate} loaded but defines no flatc target")
    endif ()
  endforeach ()


  #
  # Flatc on PATH fallback
  if (NOT TARGET flatc)
    find_program(IGASSET_FLATC_EXECUTABLE NAMES flatc)
    if (IGASSET_FLATC_EXECUTABLE)
      message(STATUS "igasset tools: using flatc from PATH: ${IGASSET_FLATC_EXECUTABLE}")
      add_executable(flatc IMPORTED GLOBAL)
      set_target_properties(flatc PROPERTIES
        IMPORTED_LOCATION "${IGASSET_FLATC_EXECUTABLE}")
    endif ()
  endif ()

  # (4) Last resort: build a standalone flatc via ExternalProject.
  #
  # This is intentionally isolated from the extern/CMakeLists.txt FetchContent
  # import of flatbuffers: ExternalProject runs a completely separate CMake
  # build at build-time, so it never adds in-tree targets and therefore cannot
  # collide with the in-tree `flatbuffers` runtime library target. The cost is
  # that flatbuffers' sources are built a second time, but only the flatc tool.
  #
  # CAVEAT (flagged for a larger, cross-file design pass): we clear
  # CMAKE_TOOLCHAIN_FILE so the sub-build uses the host compiler instead of the
  # Emscripten toolchain. That covers the preset-based emscripten builds in this
  # repo (which set the toolchain via CMakePresets.json). If you instead drive
  # the configure through `emcmake cmake`, CC/CXX in the environment will still
  # point at emcc/em++ and this sub-build will try to use them - in that case
  # prefer a native build, a flatc on PATH, or an explicit IGASSET_TOOL_WRANGLE_PATH.
  if (NOT TARGET flatc)
    message(STATUS "igasset tools: no prebuilt flatc found - building it via ExternalProject")
    include(ExternalProject)

    # Keep this Git tag in sync with the flatbuffers FetchContent_Declare in
    # extern/CMakeLists.txt.
    set(_igasset_flatc_git_tag "v25.12.19")
    set(_igasset_flatc_prefix "${CMAKE_BINARY_DIR}/_deps/flatc-ep")

    # The sub-build is native, so use the host's executable suffix - NOT this
    # WASM build's CMAKE_EXECUTABLE_SUFFIX (which would be .js / .wasm).
    if (CMAKE_HOST_WIN32)
      set(_igasset_host_exe_suffix ".exe")
    else ()
      set(_igasset_host_exe_suffix "")
    endif ()
    set(_igasset_flatc_binary
      "${_igasset_flatc_prefix}/build/flatc${_igasset_host_exe_suffix}")

    # STAMP_DIR (and DOWNLOAD_DIR) must live outside SOURCE_DIR. ExternalProject's
    # git-clone step runs `cmake -E rm -rf <SOURCE_DIR>` before cloning; CMake's
    # default stamp path is <prefix>/src/<name>-stamp, which sits inside
    # SOURCE_DIR when we set SOURCE_DIR to <prefix>/src. The wipe deletes the
    # stamp files, the clone succeeds, then copying gitinfo -> gitclone-lastrun
    # fails with "No such file or directory" (seen on Windows CI/dev).
    ExternalProject_Add(flatc_external
      GIT_REPOSITORY "https://github.com/google/flatbuffers"
      GIT_TAG "${_igasset_flatc_git_tag}"
      PREFIX "${_igasset_flatc_prefix}"
      TMP_DIR "${_igasset_flatc_prefix}/tmp"
      STAMP_DIR "${_igasset_flatc_prefix}/stamp"
      DOWNLOAD_DIR "${_igasset_flatc_prefix}/download"
      SOURCE_DIR "${_igasset_flatc_prefix}/src"
      BINARY_DIR "${_igasset_flatc_prefix}/build"
      CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=
        -DCMAKE_BUILD_TYPE=Release
        -DFLATBUFFERS_BUILD_FLATC=ON
        -DFLATBUFFERS_BUILD_TESTS=OFF
        -DFLATBUFFERS_BUILD_FLATLIB=OFF
        -DFLATBUFFERS_BUILD_FLATHASH=OFF
        -DFLATBUFFERS_BUILD_SHAREDLIB=OFF
        -DFLATBUFFERS_INSTALL=OFF
      BUILD_BYPRODUCTS "${_igasset_flatc_binary}"
      INSTALL_COMMAND ""
      EXCLUDE_FROM_ALL TRUE)

    add_executable(flatc IMPORTED GLOBAL)
    set_target_properties(flatc PROPERTIES
      IMPORTED_LOCATION "${_igasset_flatc_binary}")
    # add_dependencies on an imported target is the supported way to order an
    # imported tool behind its ExternalProject build.
    add_dependencies(flatc flatc_external)
  endif ()

  #
  # Final check for flatc, fatal abort if it still does not exist
  if (NOT TARGET flatc)
    message(FATAL_ERROR
      "Tool wrangling failed: could not resolve a flatc binary for this WASM build.\n"
      "Tried (in order): IGASSET_TOOL_WRANGLE_PATH, native preset build dirs, "
      "flatc on PATH, and an ExternalProject build.\n"
      "Run one of the native presets first (e.g. windows-clang-cl-debug or "
      "linux-clang-debug), put flatc on PATH, or pass "
      "-DIGASSET_TOOL_WRANGLE_PATH=<exported wrangle-tools.cmake>.")
  endif ()

  message(STATUS "Tool wrangling succeeded!")

  # Report the location of every tool that was resolved. Steps 1-2 provide the
  # full tool set; steps 3-4 provide only flatc, so report defensively.
  foreach (_tool flatc igasset-gen igpack-bundle
                 enumerate-assimp enumerate-igasset enumerate-igpack)
    if (TARGET ${_tool})
      get_property(_tool_loc TARGET ${_tool} PROPERTY LOCATION)
      message(STATUS "--- ${_tool}: ${_tool_loc}")
    else ()
      message(STATUS "--- ${_tool}: <not available from this tool source>")
    endif ()
  endforeach ()
endif ()
