# Embeds the full text of a flatbuffers schema file (and any other schema
# files it transitively `include`s) into a generated C++ header, as a table
# of {filename, contents} pairs plus the root schema's filename. Pair with
# toolutils::ParseEmbeddedSchema (tool-utils/embedded-schema.h) to parse the
# schema at runtime with no *.fbs files on disk.
function (embed_fbs_schema)
  set(options)
  set(oneValueArgs TARGET_NAME ROOT_SCHEMA_FILE BIN_PATH_NAME CPP_NAMESPACE)
  set(multiValueArgs SCHEMA_FILES)
  cmake_parse_arguments(EFS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  get_filename_component(generated_file_name ${EFS_ROOT_SCHEMA_FILE} NAME_WE)
  get_filename_component(root_schema_filename ${EFS_ROOT_SCHEMA_FILE} NAME)

  set(out_include_dir "${PROJECT_BINARY_DIR}/flatbuffer/include")
  set(out_dir "${out_include_dir}/${EFS_BIN_PATH_NAME}")
  set(out_header "${out_dir}/${generated_file_name}-embedded.h")

  file(MAKE_DIRECTORY ${out_dir})

  set(_abs_schema_files "")
  foreach (_f IN LISTS EFS_SCHEMA_FILES)
    get_filename_component(_abs "${_f}" ABSOLUTE)
    list(APPEND _abs_schema_files "${_abs}")
  endforeach ()

  # Written out (rather than inlined in add_custom_command) so the embedded
  # text can contain anything - quotes, semicolons, etc - without fighting
  # CMake's command-line argument parsing.
  set(_embed_script "${CMAKE_CURRENT_BINARY_DIR}/${EFS_TARGET_NAME}_embed.cmake")
  file(WRITE "${_embed_script}" [=[
set(_out "")
string(APPEND _out "#pragma once\n\n")
string(APPEND _out "#include <string_view>\n#include <utility>\n\n")
string(APPEND _out "namespace ${NS} {\n\n")
string(APPEND _out "inline constexpr std::string_view kRootSchemaFilename = \"${ROOT_NAME}\";\n\n")
string(APPEND _out "inline constexpr std::pair<std::string_view, std::string_view> kEmbeddedSchemaFiles[] = {\n")
foreach (_f IN LISTS SCHEMA_FILES)
  get_filename_component(_name "${_f}" NAME)
  file(READ "${_f}" _content)
  string(FIND "${_content}" ")IGA_FBS\"" _collision_pos)
  if (NOT _collision_pos EQUAL -1)
    message(FATAL_ERROR "embed_fbs_schema: ${_f} contains the raw-string terminator - pick a new delimiter in cmake/embed_fbs_schema.cmake")
  endif ()
  string(APPEND _out "  {\"${_name}\", R\"IGA_FBS(${_content})IGA_FBS\"},\n")
endforeach ()
string(APPEND _out "};\n\n")
string(APPEND _out "}  // namespace ${NS}\n")
file(WRITE "${OUT_HEADER}" "${_out}")
]=])

  add_custom_command(
      OUTPUT "${out_header}"
      COMMAND ${CMAKE_COMMAND}
          "-DNS=${EFS_CPP_NAMESPACE}"
          "-DROOT_NAME=${root_schema_filename}"
          "-DSCHEMA_FILES=${_abs_schema_files}"
          "-DOUT_HEADER=${out_header}"
          -P "${_embed_script}"
      DEPENDS ${_abs_schema_files}
      COMMENT "Embedding ${root_schema_filename} (+ includes) as text for ${EFS_TARGET_NAME}"
      VERBATIM)

  add_library(${EFS_TARGET_NAME} INTERFACE ${out_header})
  target_include_directories(${EFS_TARGET_NAME} INTERFACE ${out_include_dir})
  set_target_properties(${EFS_TARGET_NAME} PROPERTIES FOLDER flatbuffer_libs)
endfunction ()
