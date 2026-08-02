add_executable(TbMcpSchemaTool)
EMBED_UTF8_MANIFEST(TbMcpSchemaTool)

target_sources(TbMcpSchemaTool PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp
)

target_link_libraries(TbMcpSchemaTool
  PRIVATE
    CompilerConfig
    PrecompileStdHeaders
    TbMcpCoreLib
)

set(TB_MCP_SCHEMA_FILE ${CMAKE_SOURCE_DIR}/app/TrenchBroom/resources/mcp/tools.json)

add_custom_target(TbMcpSchema
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/app/TrenchBroom/resources/mcp
  COMMAND $<TARGET_FILE:TbMcpSchemaTool> ${TB_MCP_SCHEMA_FILE}
  DEPENDS TbMcpSchemaTool
  COMMENT "Generating ${TB_MCP_SCHEMA_FILE}"
  VERBATIM
)
