add_library(TbMcpCoreLib STATIC)

target_sources(TbMcpCoreLib
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Dispatch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/JsonUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Tools.cpp

  PUBLIC FILE_SET headers TYPE HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/mcp/Dispatch.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/mcp/JsonUtils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/mcp/Tool.h
)

target_link_libraries(TbMcpCoreLib
  PRIVATE
    CompilerConfig
    PrecompileStdHeaders
  PUBLIC
    fmt::fmt-header-only
    nlohmann_json::nlohmann_json
    KdLib
    TbBaseLib
    TbMdlLib
  )

add_subdirectory(test)
add_subdirectory(tool)
