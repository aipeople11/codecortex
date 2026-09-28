if(NOT DEFINED CODECORTEX_SOURCE_DIR)
  message(FATAL_ERROR "CODECORTEX_SOURCE_DIR is required")
endif()
file(READ "${CODECORTEX_SOURCE_DIR}/LICENSE" root_license LIMIT 256)
string(FIND "${root_license}" "MIT License" mit_pos)
if(mit_pos EQUAL -1)
  message(FATAL_ERROR "root LICENSE is not the CodeCortex MIT license")
endif()
foreach(required
    "${CODECORTEX_SOURCE_DIR}/LICENSES/Apache-2.0.txt"
    "${CODECORTEX_SOURCE_DIR}/LICENSES/MIT-CodeCortex.txt"
    "${CODECORTEX_SOURCE_DIR}/NOTICE"
    "${CODECORTEX_SOURCE_DIR}/LEGAL/UPSTREAM_ATTRIBUTION.md"
    "${CODECORTEX_SOURCE_DIR}/LICENSING.md")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "required licensing/provenance file missing: ${required}")
  endif()
endforeach()
message(STATUS "CodeCortex licensing boundary check passed")
