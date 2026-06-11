# Draco 1.5.7's ply_reader.cc uses std::all_of without #include <algorithm>.
# libc++ under Emscripten does not pull it in transitively, so add it once after
# fetch. Safe to re-run: skips if the include is already present.
cmake_minimum_required(VERSION 3.24)

if (NOT DEFINED PLY_READER_CC)
  message(FATAL_ERROR "PLY_READER_CC must point at draco's ply_reader.cc")
endif ()

file(READ "${PLY_READER_CC}" _content)
if (_content MATCHES "#include <algorithm>")
  return()
endif ()

string(REPLACE "#include <array>" "#include <algorithm>\n#include <array>" _content "${_content}")
file(WRITE "${PLY_READER_CC}" "${_content}")
