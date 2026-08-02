#include "mcp/Dispatch.h"
#include "mcp/Tool.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace tb::mcp
{

TEST_CASE("tools.json matches the dispatcher")
{
  auto stream = std::ifstream{TB_MCP_SCHEMA_PATH};
  INFO("regenerate with: cmake --build build --target TbMcpSchema");
  CAPTURE(TB_MCP_SCHEMA_PATH);
  REQUIRE(stream.good());

  const auto committed = nlohmann::json::parse(stream);

  CHECK(committed == toolSchema());
}

TEST_CASE("every tool is well formed")
{
  for (const auto& tool : tools())
  {
    CAPTURE(tool.name);

    CHECK_FALSE(tool.name.empty());
    CHECK_FALSE(tool.description.empty());
    CHECK(tool.handler != nullptr);

    REQUIRE(tool.paramsSchema.is_object());
    CHECK(tool.paramsSchema.at("type") == "object");
  }
}

TEST_CASE("tool names are unique")
{
  auto names = std::set<std::string>{};
  for (const auto& tool : tools())
  {
    CAPTURE(tool.name);
    CHECK(names.insert(tool.name).second);
  }
}

}
