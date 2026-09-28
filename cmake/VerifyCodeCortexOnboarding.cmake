if(NOT DEFINED CODECORTEX_SOURCE_DIR)
  message(FATAL_ERROR "CODECORTEX_SOURCE_DIR is required")
endif()

file(READ "${CODECORTEX_SOURCE_DIR}/scripts/onboard.sh" onboarding)
foreach(required
    "codecortex install"
    "codex mcp add codecortex"
    "claude mcp add codecortex"
    "gemini mcp add"
    "--generic"
    "transport: stdio"
    "127.0.0.1:7332"
    "CodeCortex is ready."
    "http://127.0.0.1:7331")
  string(FIND "${onboarding}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "onboarding contract missing: ${required}")
  endif()
endforeach()

file(READ "${CODECORTEX_SOURCE_DIR}/.mcp.json" legacyMcp)
foreach(required "codecortex" "--mcp" "--mcp-ui")
  string(FIND "${legacyMcp}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR ".mcp.json onboarding contract missing: ${required}")
  endif()
endforeach()

file(READ "${CODECORTEX_SOURCE_DIR}/mcp.json" portableMcp)
foreach(required "codecortex" "stdio" "--mcp" "--mcp-ui")
  string(FIND "${portableMcp}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "portable mcp.json contract missing: ${required}")
  endif()
endforeach()

file(READ "${CODECORTEX_SOURCE_DIR}/plugin.json" plugin)
foreach(required "CodeCortex" "Developer Tools" "./mcp.json" "./skills/")
  string(FIND "${plugin}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "plugin packaging contract missing: ${required}")
  endif()
endforeach()

file(READ "${CODECORTEX_SOURCE_DIR}/src/codecortex_mcp_bridge.h" bridge)
foreach(required "codecortex_status" "CodeCortex is connected" "Dashboard:")
  string(FIND "${bridge}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "MCP onboarding/status contract missing: ${required}")
  endif()
endforeach()
message(STATUS "CodeCortex local-first onboarding contract check passed")
