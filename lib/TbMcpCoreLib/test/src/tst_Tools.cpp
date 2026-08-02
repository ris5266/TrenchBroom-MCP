#include "mcp/Dispatch.h"

#include "mdl/Brush.h"
#include "mdl/BrushNode.h"
#include "mdl/CatchConfig.h"
#include "mdl/Entity.h"
#include "mdl/Map.h"
#include "mdl/MapFixture.h"
#include "mdl/Map_Selection.h"
#include "mdl/Selection.h"
#include "mdl/WorldNode.h"

#include "vm/bbox.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tb::mcp
{
namespace
{

nlohmann::json createBrushRequest(
  const vm::vec3d& min, const vm::vec3d& max, const std::string& material = "")
{
  auto params = nlohmann::json{
    {"bounds",
     {{"min", {min.x(), min.y(), min.z()}}, {"max", {max.x(), max.y(), max.z()}}}}};
  if (!material.empty())
  {
    params["material"] = material;
  }
  return nlohmann::json{{"tool", "create_brush"}, {"params", params}};
}

size_t brushCount(const mdl::Map& map)
{
  return map.worldNode().defaultLayer()->childCount();
}

} // namespace

TEST_CASE("ping")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  const auto response = dispatch(map, nlohmann::json{{"tool", "ping"}});

  CHECK(response["ok"] == true);
  CHECK(response["result"]["mapFormat"] == "Valve");
}

TEST_CASE("create_brush")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  SECTION("creates a brush with the requested bounds")
  {
    const auto response =
      dispatch(map, createBrushRequest({0, 0, 0}, {64, 128, 16}, "someMaterial"));

    REQUIRE(response["ok"] == true);
    CHECK(response["result"]["created"] == 1);
    CHECK(brushCount(map) == 1u);

    const auto* brushNode =
      dynamic_cast<mdl::BrushNode*>(map.worldNode().defaultLayer()->children().front());
    REQUIRE(brushNode != nullptr);
    CHECK(brushNode->logicalBounds() == vm::bbox3d{{0, 0, 0}, {64, 128, 16}});
  }

  SECTION("accepts corners given in either order")
  {
    const auto response = dispatch(map, createBrushRequest({64, 128, 16}, {0, 0, 0}));

    REQUIRE(response["ok"] == true);
    CHECK(brushCount(map) == 1u);
  }

  SECTION("leaves the new brush selected")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);

    CHECK(map.selection().brushes.size() == 1u);
  }

  SECTION("is a single undo step")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);
    REQUIRE(brushCount(map) == 1u);

    map.undoCommand();

    CHECK(brushCount(map) == 0u);
  }

  SECTION("rejects a box with no volume")
  {
    const auto response = dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 0}));

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
    CHECK(brushCount(map) == 0u);
  }

  SECTION("rejects bounds outside the world")
  {
    const auto huge = map.worldBounds().max.x() * 2.0;
    const auto response = dispatch(map, createBrushRequest({0, 0, 0}, {huge, huge, huge}));

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
    CHECK(brushCount(map) == 0u);
  }

  SECTION("reports missing bounds rather than defaulting them")
  {
    const auto response = dispatch(
      map, nlohmann::json{{"tool", "create_brush"}, {"params", nlohmann::json::object()}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
  }
}

TEST_CASE("get_scene")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  SECTION("reports an empty map")
  {
    const auto response = dispatch(map, nlohmann::json{{"tool", "get_scene"}});

    REQUIRE(response["ok"] == true);
    const auto& result = response["result"];
    CHECK(result["mapFormat"] == "Valve");
    CHECK(result["layers"].size() == 1u);
    CHECK(result["layers"][0]["default"] == true);
    CHECK(result["layers"][0]["contents"]["brushes"] == 0);
  }

  SECTION("counts brushes")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);
    REQUIRE(dispatch(map, createBrushRequest({128, 0, 0}, {192, 64, 64}))["ok"] == true);

    const auto response = dispatch(map, nlohmann::json{{"tool", "get_scene"}});

    REQUIRE(response["ok"] == true);
    CHECK(response["result"]["layers"][0]["contents"]["brushes"] == 2);
  }

  SECTION("does not disturb the selection or the undo stack")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);
    const auto selectionBefore = map.selection().nodes;
    const auto modificationCountBefore = map.modificationCount();

    REQUIRE(dispatch(map, nlohmann::json{{"tool", "get_scene"}})["ok"] == true);

    CHECK(map.selection().nodes == selectionBefore);
    CHECK(map.modificationCount() == modificationCountBefore);
  }
}

TEST_CASE("create_cylinder")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  const auto request = [](nlohmann::json params) {
    params["bounds"] = {{"min", {0, 0, 0}}, {"max", {128, 128, 256}}};
    return nlohmann::json{{"tool", "create_cylinder"}, {"params", params}};
  };

  SECTION("creates one brush by default")
  {
    const auto response = dispatch(map, request({}));

    REQUIRE(response["ok"] == true);
    CHECK(response["result"]["created"] == 1);
    CHECK(brushCount(map) == 1u);
  }

  SECTION("honours the requested side count")
  {
    REQUIRE(dispatch(map, request({{"circle", {{"sides", 16}}}}))["ok"] == true);

    const auto* brushNode =
      dynamic_cast<mdl::BrushNode*>(map.worldNode().defaultLayer()->children().front());
    REQUIRE(brushNode != nullptr);
    // Sides plus the two caps.
    CHECK(brushNode->brush().faceCount() == 18u);
  }

  SECTION("a thickness produces a ring of brushes")
  {
    const auto response = dispatch(map, request({{"thickness", 16}}));

    REQUIRE(response["ok"] == true);
    CHECK(response["result"]["created"] > 1);
    CHECK(brushCount(map) > 1u);
  }

  SECTION("the whole tube is one undo step")
  {
    REQUIRE(dispatch(map, request({{"thickness", 16}}))["ok"] == true);
    REQUIRE(brushCount(map) > 1u);

    map.undoCommand();

    CHECK(brushCount(map) == 0u);
  }

  SECTION("rejects a bad axis")
  {
    const auto response = dispatch(map, request({{"axis", "w"}}));

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
    CHECK(brushCount(map) == 0u);
  }

  SECTION("rejects fewer than three sides")
  {
    const auto response = dispatch(map, request({{"circle", {{"sides", 2}}}}));

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
  }

  SECTION("rejects a non-positive thickness")
  {
    const auto response = dispatch(map, request({{"thickness", 0}}));

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
  }
}

TEST_CASE("create_cone")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  const auto request = [](nlohmann::json params) {
    params["bounds"] = {{"min", {0, 0, 0}}, {"max", {128, 128, 256}}};
    return nlohmann::json{{"tool", "create_cone"}, {"params", params}};
  };

  SECTION("creates a cone")
  {
    REQUIRE(dispatch(map, request({}))["ok"] == true);
    CHECK(brushCount(map) == 1u);
  }

  SECTION("builds along the requested axis")
  {
    REQUIRE(dispatch(map, request({{"axis", "x"}}))["ok"] == true);
    CHECK(brushCount(map) == 1u);
  }

  SECTION("accepts vertex alignment")
  {
    REQUIRE(
      dispatch(map, request({{"circle", {{"alignment", "vertex"}, {"sides", 12}}}}))["ok"]
      == true);
    CHECK(brushCount(map) == 1u);
  }
}

TEST_CASE("create_sphere")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  const auto request = [](nlohmann::json params) {
    params["bounds"] = {{"min", {0, 0, 0}}, {"max", {128, 128, 128}}};
    return nlohmann::json{{"tool", "create_sphere"}, {"params", params}};
  };

  SECTION("builds a uv sphere by default")
  {
    REQUIRE(dispatch(map, request({}))["ok"] == true);
    CHECK(brushCount(map) == 1u);
  }

  SECTION("builds an icosphere")
  {
    REQUIRE(dispatch(map, request({{"kind", "ico"}, {"iterations", 1}}))["ok"] == true);
    CHECK(brushCount(map) == 1u);
  }

  SECTION("rejects an unknown kind")
  {
    const auto response = dispatch(map, request({{"kind", "geodesic"}}));

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
    CHECK(brushCount(map) == 0u);
  }

  SECTION("rejects a ring count below one")
  {
    const auto response = dispatch(map, request({{"rings", 0}}));

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
  }
}

TEST_CASE("create_arch")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  const auto request = [](nlohmann::json params) {
    params["bounds"] = {{"min", {0, 0, 0}}, {"max", {256, 128, 256}}};
    return nlohmann::json{{"tool", "create_arch"}, {"params", params}};
  };

  SECTION("produces a band of several brushes")
  {
    const auto response = dispatch(map, request({{"thickness", 24}, {"axis", "y"}}));

    REQUIRE(response["ok"] == true);
    CHECK(response["result"]["created"] > 1);
    CHECK(brushCount(map) > 1u);
  }

  SECTION("the whole arch is one undo step")
  {
    REQUIRE(dispatch(map, request({{"thickness", 24}, {"axis", "y"}}))["ok"] == true);
    const auto count = brushCount(map);
    REQUIRE(count > 1u);

    map.undoCommand();

    CHECK(brushCount(map) == 0u);
  }

  SECTION("leaves every voussoir selected")
  {
    REQUIRE(dispatch(map, request({{"thickness", 24}, {"axis", "y"}}))["ok"] == true);

    CHECK(map.selection().brushes.size() == brushCount(map));
  }

  SECTION("requires a thickness")
  {
    const auto response = dispatch(map, request({{"axis", "y"}}));

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
  }
}

TEST_CASE("shapes reject bounds outside the world")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  const auto huge = map.worldBounds().max.x() * 2.0;
  const auto tool =
    GENERATE(std::string{"create_cylinder"}, "create_cone", "create_sphere");
  CAPTURE(tool);

  const auto response = dispatch(
    map,
    nlohmann::json{
      {"tool", tool},
      {"params",
       {{"bounds", {{"min", {0, 0, 0}}, {"max", {huge, huge, huge}}}}, {"thickness", 8}}}});

  CHECK(response["ok"] == false);
  CHECK(response["error"]["code"] == "invalid_parameters");
  CHECK(brushCount(map) == 0u);
}

TEST_CASE("set_worldspawn_property")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  const auto setWad = nlohmann::json{
    {"tool", "set_worldspawn_property"},
    {"params", {{"key", "wad"}, {"value", "/textures/scene.wad"}}}};

  SECTION("sets a key on worldspawn with nothing selected")
  {
    REQUIRE(dispatch(map, setWad)["ok"] == true);

    const auto* value = map.worldNode().entity().property("wad");
    REQUIRE(value != nullptr);
    CHECK(*value == "/textures/scene.wad");
  }

  SECTION("preserves the user's selection")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);
    const auto selectionBefore = map.selection().nodes;
    REQUIRE(selectionBefore.size() == 1u);

    REQUIRE(dispatch(map, setWad)["ok"] == true);

    CHECK(map.selection().nodes == selectionBefore);
  }

  SECTION("rejects a missing key")
  {
    const auto response = dispatch(
      map,
      nlohmann::json{
        {"tool", "set_worldspawn_property"}, {"params", {{"value", "x"}}}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
  }
}

TEST_CASE("batch")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  const auto roomOps = nlohmann::json::array({
    createBrushRequest({0, 0, 0}, {256, 256, 16}),
    createBrushRequest({0, 0, 240}, {256, 256, 256}),
    createBrushRequest({0, 0, 0}, {16, 256, 256}),
    createBrushRequest({240, 0, 0}, {256, 256, 256}),
  });

  SECTION("applies every operation")
  {
    const auto response = dispatch(
      map,
      nlohmann::json{
        {"tool", "batch"}, {"params", {{"name", "build room"}, {"ops", roomOps}}}});

    REQUIRE(response["ok"] == true);
    CHECK(response["result"]["ops"] == 4);
    CHECK(brushCount(map) == 4u);
  }

  SECTION("collapses into one undo step")
  {
    REQUIRE(
      dispatch(map, nlohmann::json{{"tool", "batch"}, {"params", {{"ops", roomOps}}}})["ok"]
      == true);
    REQUIRE(brushCount(map) == 4u);

    map.undoCommand();

    CHECK(brushCount(map) == 0u);
  }

  SECTION("leaves everything it created selected")
  {
    REQUIRE(
      dispatch(map, nlohmann::json{{"tool", "batch"}, {"params", {{"ops", roomOps}}}})["ok"]
      == true);

    CHECK(map.selection().brushes.size() == 4u);
  }

  SECTION("rolls the whole batch back when one operation fails")
  {
    auto ops = roomOps;
    ops.push_back(createBrushRequest({0, 0, 0}, {64, 64, 0})); // degenerate

    const auto response =
      dispatch(map, nlohmann::json{{"tool", "batch"}, {"params", {{"ops", ops}}}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
    CHECK(brushCount(map) == 0u);
  }

  SECTION("refuses to nest")
  {
    const auto response = dispatch(
      map,
      nlohmann::json{
        {"tool", "batch"},
        {"params",
         {{"ops",
           nlohmann::json::array(
             {nlohmann::json{{"tool", "batch"}, {"params", {{"ops", roomOps}}}}})}}}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_request");
  }
}

}
