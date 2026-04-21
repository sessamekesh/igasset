function (gen_flatbuffer_cpp)
  set(options)
  set(oneValueArgs TARGET_NAME SCHEMA_FILE BIN_PATH_NAME)
  set(multiValueArgs)
  cmake_parse_arguments(GFC "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if (NOT TARGET flatc)
    message(FATAL_ERROR "flatc binary not available - this is probably because a tools build was not done on WASM")
  endif ()

  get_filename_component(generated_file_name ${GFC_SCHEMA_FILE} NAME_WE)

  set(proto_include_dir "${PROJECT_BINARY_DIR}/flatbuffer/include")
  set(proto_file_out_dir "${proto_include_dir}/${GFC_BIN_PATH_NAME}")
  set(proto_file_out_name "${proto_file_out_dir}/${generated_file_name}.h")
  get_filename_component(protopath_absolute "${GFC_SCHEMA_FILE}" ABSOLUTE)

  # CMake strips empty-string arguments from add_custom_command COMMAND lists
  # before they reach the build tool, making it impossible to pass
  # --filename-suffix "" directly.  Instead we emit a tiny cmake -P script
  # that contains the empty string as a literal, and invoke it at build time.
  # The script receives the flatc path via -D so it works with generator
  # expressions ($<TARGET_FILE:flatc>).
  set(_flatc_runner "${CMAKE_CURRENT_BINARY_DIR}/${GFC_TARGET_NAME}_run_flatc.cmake")
  file(WRITE "${_flatc_runner}" [=[
execute_process(
    COMMAND "${FLATC}" --no-cpp-direct-copy --filename-suffix "" -o "${OUTDIR}" --cpp "${SCHEMA}"
    COMMAND_ERROR_IS_FATAL ANY
)
]=])

  file(MAKE_DIRECTORY ${proto_file_out_dir})

  add_custom_command(
      OUTPUT ${proto_file_out_name}
      COMMAND ${CMAKE_COMMAND}
          "-DFLATC=$<TARGET_FILE:flatc>"
          "-DOUTDIR=${proto_file_out_dir}"
          "-DSCHEMA=${protopath_absolute}"
          -P "${_flatc_runner}"
      DEPENDS flatc ${protopath_absolute}
  )

  add_library(${GFC_TARGET_NAME} INTERFACE ${proto_file_out_name})
  target_include_directories(${GFC_TARGET_NAME} INTERFACE ${proto_include_dir})
  target_link_libraries(${GFC_TARGET_NAME} INTERFACE flatbuffers)
  set_target_properties(${GFC_TARGET_NAME} PROPERTIES FOLDER flatbuffer_libs)
endfunction ()
