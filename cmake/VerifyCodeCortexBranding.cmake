cmake_policy(SET CMP0053 NEW)
if(NOT DEFINED CODECORTEX_SOURCE_DIR)
  message(FATAL_ERROR "CODECORTEX_SOURCE_DIR is required")
endif()
set(FILES)
foreach(pattern
    "src/*.h" "src/*.hpp" "src/*.cpp" "src/*.c"
    "ui/*.js" "ui/*.html" "ui/*.css"
    "test/*.sh" "test/*.cpp" "test/*.h" "test/*.md" "test/*.json"
    "cmake/*.cmake" "scripts/*.sh" "scripts/*.py"
    "skills/*.md" "skills/*.sh" "hooks/*.sh" "hooks/*.json")
  file(GLOB_RECURSE chunk "${CODECORTEX_SOURCE_DIR}/${pattern}")
  list(APPEND FILES ${chunk})
endforeach()
list(APPEND FILES
  "${CODECORTEX_SOURCE_DIR}/README.md"
  "${CODECORTEX_SOURCE_DIR}/INSTALL.md"
  "${CODECORTEX_SOURCE_DIR}/ONBOARDING.md"
  "${CODECORTEX_SOURCE_DIR}/AGENTS.md"
  "${CODECORTEX_SOURCE_DIR}/CLAUDE.md"
  "${CODECORTEX_SOURCE_DIR}/CMakeLists.txt"
  "${CODECORTEX_SOURCE_DIR}/install.sh"
  "${CODECORTEX_SOURCE_DIR}/.mcp.json")
list(REMOVE_DUPLICATES FILES)
foreach(f IN LISTS FILES)
  if(NOT EXISTS "${f}")
    continue()
  endif()
  # Compatibility-only migration surfaces intentionally name the retired product so old host registrations can be removed.
  # They are not user-facing branding and are covered by migration regressions.
  if(f MATCHES "/scripts/onboard\\.sh$" OR f MATCHES "/test/p2h15runtimequalitycheck\\.sh$")
    continue()
  endif()
  file(READ "${f}" content)
  string(REGEX MATCH "[Rr][Ii][Pp][Ww][Ii][Rr][Ee]|[Cc][Oo][Dd][Ee][Cc][Oo][Dd][Ee][Xx]|[Aa][Gg][Ee][Nn][Tt][Mm][Ee][Mm][Oo][Rr][Yy]" bad "${content}")
  if(bad)
    message(FATAL_ERROR "legacy/donor branding leaked into product surface: ${f}: ${bad}")
  endif()
endforeach()
message(STATUS "CodeCortex product-surface branding check passed")
