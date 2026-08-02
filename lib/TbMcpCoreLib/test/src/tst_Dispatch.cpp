#include "mcp/Dispatch.h"

#include "mdl/CatchConfig.h"
#include "mdl/Map.h"
#include "mdl/MapFixture.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

namespace tb::mcp
{

TEST_CASE("dispatch")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  SECTION("rejects a request that is not an object")
  {
    const auto response = dispatch(map, nlohmann::json::array({1, 2, 3}));

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_request");
  }

  SECTION("rejects a request with no tool")
  {
    const auto response = dispatch(map, nlohmann::json{{"params", {{"a", 1}}}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_request");
  }

  SECTION("rejects an unknown tool")
  {
    const auto response = dispatch(map, nlohmann::json{{"tool", "no_such_tool"}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "unknown_tool");
  }

  SECTION("reports malformed JSON instead of throwing")
  {
    const auto response = dispatch(map, std::string{"{not json"});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_request");
  }

  SECTION("parses a request given as text")
  {
    const auto response = dispatch(map, std::string{R"({"tool": "ping"})"});

    CHECK(response["ok"] == true);
  }

  SECTION("echoes the request id so replies can be matched to calls")
  {
    const auto response = dispatch(map, nlohmann::json{{"tool", "ping"}, {"id", 42}});

    CHECK(response["id"] == 42);
  }

  SECTION("echoes the id on failures too")
  {
    const auto response =
      dispatch(map, nlohmann::json{{"tool", "no_such_tool"}, {"id", "abc"}});

    CHECK(response["ok"] == false);
    CHECK(response["id"] == "abc");
  }

  SECTION("treats missing params as empty")
  {
    const auto response = dispatch(map, nlohmann::json{{"tool", "get_scene"}});

    CHECK(response["ok"] == true);
  }
}

}
