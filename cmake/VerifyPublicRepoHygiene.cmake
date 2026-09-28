if(NOT DEFINED CODECORTEX_SOURCE_DIR)
  message(FATAL_ERROR "CODECORTEX_SOURCE_DIR is required")
endif()
set(FILES)
foreach(pattern
    "*.md" "*.txt" "*.json" "*.yml" "*.yaml" "*.cmake" "*.sh" "*.py"
    "src/*.h" "src/*.hpp" "src/*.cpp" "src/*.c"
    "ui/*.js" "ui/*.html" "ui/*.css"
    "scripts/*.sh" "scripts/*.py" "skills/*.md" "skills/*.sh" "hooks/*.sh"
    "test/*.sh" "test/*.cpp" "test/*.h")
  file(GLOB_RECURSE chunk "${CODECORTEX_SOURCE_DIR}/${pattern}")
  list(APPEND FILES ${chunk})
endforeach()
list(REMOVE_DUPLICATES FILES)
foreach(f IN LISTS FILES)
  # Development-only benchmark, presentation, and paper artifacts are not part
  # of the public runtime release surface. They may retain historical corpus
  # names and are excluded from this public-surface scan.
  if(f MATCHES "/LEGAL/" OR f MATCHES "/LICENSES/" OR f MATCHES "/third_party/" OR f MATCHES "/bench/" OR f MATCHES "/present/" OR f MATCHES "/paper/" OR f MATCHES "/prompts/" OR f MATCHES "/build/" OR f MATCHES "VerifyPublicRepoHygiene.cmake$")
    continue()
  endif()
  # Compatibility migration code must recognize the retired executable/server name in order to remove stale host registrations.
  if(f MATCHES "/scripts/onboard\\.sh$" OR f MATCHES "/test/p2h15runtimequalitycheck\\.sh$")
    continue()
  endif()
  file(READ "${f}" content)
  string(REGEX MATCH "[Cc][Oo][Dd][Ee][Cc][Oo][Dd][Ee][Xx]|[Rr][Ii][Pp][Ww][Ii][Rr][Ee]|[Aa][Gg][Ee][Nn][Tt][Mm][Ee][Mm][Oo][Rr][Yy]|redhat-et|/Users/(sun-arv-ai|400219126)/|deepagentframework_vNext|[Aa][Gg][Ee][Nn][Tt][Mm][Ee][Mm][Oo][Rr][Yy]-demo|claude/amazing" bad "${content}")
  if(bad)
    message(FATAL_ERROR "public-repo hygiene violation: ${f}: ${bad}")
  endif()
endforeach()
message(STATUS "CodeCortex public-repo hygiene check passed")
