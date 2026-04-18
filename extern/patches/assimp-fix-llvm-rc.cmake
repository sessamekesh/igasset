# Patch assimp's revision.h.in so llvm-rc (used by clang-cl) can compile
# assimp.rc. Upstream uses "\xA9 2006-YYYY" for the copyright string; llvm-rc
# rejects the 0xA9 byte in a non-Unicode VERSIONINFO string even with
# #pragma code_page(1252). Replacing it with the ASCII "(C)" produces the
# same semantic copyright notice and builds cleanly on both MSVC rc.exe and
# llvm-rc.
#
# Invoked by FetchContent_Declare(assimp ... PATCH_COMMAND ...).
# Expects -DREVISION_H_IN=<path> on the cmake -P command line.

if(NOT DEFINED REVISION_H_IN)
  message(FATAL_ERROR "assimp-fix-llvm-rc: REVISION_H_IN is not set")
endif()

if(NOT EXISTS "${REVISION_H_IN}")
  message(FATAL_ERROR "assimp-fix-llvm-rc: file not found: ${REVISION_H_IN}")
endif()

file(READ "${REVISION_H_IN}" _content)
string(REPLACE "\"\\xA9 " "\"(C) " _patched "${_content}")

if("${_content}" STREQUAL "${_patched}")
  message(STATUS "assimp-fix-llvm-rc: no changes needed (already patched)")
else()
  file(WRITE "${REVISION_H_IN}" "${_patched}")
  message(STATUS "assimp-fix-llvm-rc: patched ${REVISION_H_IN}")
endif()
