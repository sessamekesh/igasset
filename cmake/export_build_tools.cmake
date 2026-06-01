IF (IGASSET_BUILD_GEN_TOOLS AND NOT EMSCRIPTEN AND IGASSET_TOOL_WRANGLE_PATH)
  message(STATUS "Writing wrangle-tool CMake file to ${IGASSET_TOOL_WRANGLE_PATH}")
  export(TARGETS
          flatc igasset-gen igpack-bundle
          enumerate-assimp enumerate-igasset enumerate-igpack
        FILE "${IGASSET_TOOL_WRANGLE_PATH}")
endif ()
