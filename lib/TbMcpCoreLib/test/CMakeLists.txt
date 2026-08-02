add_executable(TbMcpCoreLibTest)
EMBED_UTF8_MANIFEST(TbMcpCoreLibTest)

target_sources(TbMcpCoreLibTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_Dispatch.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_ToolSchema.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_Tools.cpp
)

target_compile_definitions(TbMcpCoreLibTest PRIVATE
  TB_MCP_SCHEMA_PATH="${CMAKE_SOURCE_DIR}/app/TrenchBroom/resources/mcp/tools.json"
)

target_link_libraries(TbMcpCoreLibTest
  PRIVATE
    CompilerConfig
    PrecompileStdHeaders
    Catch2::Catch2WithMain
    TbBaseTestUtilsLib
    TbMcpCoreLib
    TbMdlLib
    TbMdlTestUtilsLib
)

set(TEST_FIXTURE_DEST_DIR $<TARGET_FILE_DIR:TbMcpCoreLibTest>/fixture)

add_custom_command(TARGET TbMcpCoreLibTest POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${TEST_FIXTURE_DEST_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${TB_RESOURCE_DIR}/games" "${TEST_FIXTURE_DEST_DIR}/games"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${TB_RESOURCE_DIR}/games-testing" "${TEST_FIXTURE_DEST_DIR}/games"
)

include(Catch)
catch_discover_tests(TbMcpCoreLibTest)
