add_library(TbMcpLib STATIC)

target_sources(TbMcpLib
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/McpServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/QtHostContext.cpp

  PUBLIC FILE_SET headers TYPE HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/mcp/McpServer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/mcp/QtHostContext.h
)

target_link_libraries(TbMcpLib
  PRIVATE
    CompilerConfig
    PrecompileStdHeaders
  PUBLIC
    Qt6::Widgets
    TbMcpCoreLib
    TbUiLib
  )

set_target_properties(TbMcpLib PROPERTIES AUTOMOC TRUE)
