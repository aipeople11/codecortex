# Generate version.h at BUILD time, not only configure time. Agent worktrees are edited between
# configure/build constantly; a configure-time SHA becomes false provenance after the first edit.
execute_process(
  COMMAND git -C "${CODECORTEX_SOURCE_DIR}" rev-parse --short=9 HEAD
  RESULT_VARIABLE _git_rc
  OUTPUT_VARIABLE _git_sha
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT _git_rc EQUAL 0 OR _git_sha STREQUAL "")
  set(_git_stamp "unknown")
else()
  set(_git_stamp "${_git_sha}")
  execute_process(COMMAND git -C "${CODECORTEX_SOURCE_DIR}" diff --quiet --ignore-submodules -- RESULT_VARIABLE _work_dirty ERROR_QUIET)
  execute_process(COMMAND git -C "${CODECORTEX_SOURCE_DIR}" diff --cached --quiet --ignore-submodules -- RESULT_VARIABLE _index_dirty ERROR_QUIET)
  if(NOT _work_dirty EQUAL 0 OR NOT _index_dirty EQUAL 0)
    string(APPEND _git_stamp "+dirty")
  endif()
endif()

set(_body "#pragma once\n\n// Generated at build time by cmake/version_stamp.cmake — do not edit.\nnamespace rw\n{\ninline constexpr const char* kCodeCortexVersion     = \"${CODECORTEX_VERSION}\";\ninline constexpr const char* kCodeCortexBuildType   = \"${CODECORTEX_BUILD_TYPE}\";\ninline constexpr const char* kCodeCortexCompilerId  = \"${CODECORTEX_COMPILER_ID}\";\ninline constexpr const char* kCodeCortexCompilerVer = \"${CODECORTEX_COMPILER_VER}\";\ninline constexpr const char* kCodeCortexGitStamp    = \"${_git_stamp}\";\n} // namespace rw\n")

# A tmp name of its own, next to the output: two concurrent builds of the same configuration in one tree shared
# "<output>.tmp" (the same race cmake/source_identity.cmake closes). string(RANDOM) is seeded per process.
string(RANDOM LENGTH 12 _tmp_tag)
set(_tmp "${CODECORTEX_OUTPUT}.${_tmp_tag}.tmp")
file(WRITE "${_tmp}" "${_body}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_tmp}" "${CODECORTEX_OUTPUT}" RESULT_VARIABLE _copy_rc)
file(REMOVE "${_tmp}")
if(NOT _copy_rc EQUAL 0)
  message(FATAL_ERROR "could not write ${CODECORTEX_OUTPUT}")
endif()
