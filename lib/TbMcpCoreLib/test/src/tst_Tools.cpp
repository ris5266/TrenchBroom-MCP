#include "mcp/Dispatch.h"

#include "mdl/Brush.h"
#include "mdl/BrushNode.h"
#include "mdl/CatchConfig.h"
#include "mdl/BrushFace.h"
#include "mdl/Entity.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityNode.h"
#include "mdl/Grid.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFixture.h"
#include "mdl/Map_Selection.h"
#include "mdl/Selection.h"
#include "mdl/WorldNode.h"

#include "vm/bbox.h"

#include <algorithm>
#include <string>

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tb::mcp
{
using Catch::Approx;

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

/**
 * Stocks the definition manager the way upstream's own entity tests do. The headless
 * fixture has no game filesystem, so the real FGDs cannot be parsed here; in the editor
 * these come from the game's own files.
 */
void installEntityDefinitions(mdl::Map& map)
{
  map.entityDefinitionManager().setDefinitions({
    {"info_player_start",
     Color{},
     "player spawn point",
     {},
     mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}}},
    {"light",
     Color{},
     "a light",
     {},
     mdl::PointEntityDefinition{vm::bbox3d{8.0}, {}, {}}},
    {"func_door", Color{}, "a door", {}, std::nullopt},
  });
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

TEST_CASE("translate")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  const auto boxBounds = [&]() {
    return dynamic_cast<mdl::BrushNode*>(map.worldNode().defaultLayer()->children().front())
      ->logicalBounds();
  };

  SECTION("moves the editor selection")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);

    const auto response = dispatch(
      map, nlohmann::json{{"tool", "translate"}, {"params", {{"delta", {0, 0, 128}}}}});

    REQUIRE(response["ok"] == true);
    CHECK(response["result"]["transformed"] == 1);
    CHECK(boxBounds() == vm::bbox3d{{0, 0, 128}, {64, 64, 192}});
  }

  SECTION("moves what the same batch just created")
  {
    const auto response = dispatch(
      map,
      nlohmann::json{
        {"tool", "batch"},
        {"params",
         {{"ops",
           nlohmann::json::array(
             {createBrushRequest({0, 0, 0}, {64, 64, 64}),
              nlohmann::json{
                {"tool", "translate"}, {"params", {{"delta", {256, 0, 0}}}}}})}}}});

    REQUIRE(response["ok"] == true);
    CHECK(boxBounds() == vm::bbox3d{{256, 0, 0}, {320, 64, 64}});
  }

  SECTION("build then move is a single undo step")
  {
    REQUIRE(
      dispatch(
        map,
        nlohmann::json{
          {"tool", "batch"},
          {"params",
           {{"ops",
             nlohmann::json::array(
               {createBrushRequest({0, 0, 0}, {64, 64, 64}),
                nlohmann::json{
                  {"tool", "translate"},
                  {"params", {{"delta", {256, 0, 0}}}}}})}}}})["ok"]
      == true);
    REQUIRE(brushCount(map) == 1u);

    map.undoCommand();

    CHECK(brushCount(map) == 0u);
  }

  SECTION("reports when there is nothing to move")
  {
    const auto response = dispatch(
      map, nlohmann::json{{"tool", "translate"}, {"params", {{"delta", {0, 0, 128}}}}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
  }

  SECTION("target 'created' does not fall back to the selection")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);

    const auto response = dispatch(
      map,
      nlohmann::json{
        {"tool", "translate"},
        {"params", {{"delta", {0, 0, 128}}, {"target", "created"}}}});

    CHECK(response["ok"] == false);
    CHECK(boxBounds() == vm::bbox3d{{0, 0, 0}, {64, 64, 64}});
  }

  SECTION("requires a delta")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);

    const auto response =
      dispatch(map, nlohmann::json{{"tool", "translate"}, {"params", {{"target", "auto"}}}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
  }
}

TEST_CASE("rotate")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  const auto boxBounds = [&]() {
    return dynamic_cast<mdl::BrushNode*>(map.worldNode().defaultLayer()->children().front())
      ->logicalBounds();
  };

  SECTION("a quarter turn about z swaps the footprint")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {128, 64, 64}))["ok"] == true);

    const auto response = dispatch(
      map, nlohmann::json{{"tool", "rotate"}, {"params", {{"angle", 90}}}});

    REQUIRE(response["ok"] == true);
    // Rotating about the centre of a 128x64 footprint gives a 64x128 one.
    const auto bounds = boxBounds();
    CHECK(bounds.size().x() == Approx(64.0).margin(0.01));
    CHECK(bounds.size().y() == Approx(128.0).margin(0.01));
  }

  SECTION("a full turn is a no-op")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {128, 64, 64}))["ok"] == true);

    REQUIRE(
      dispatch(map, nlohmann::json{{"tool", "rotate"}, {"params", {{"angle", 360}}}})["ok"]
      == true);

    const auto bounds = boxBounds();
    CHECK(bounds.min.x() == Approx(0.0).margin(0.01));
    CHECK(bounds.max.x() == Approx(128.0).margin(0.01));
  }

  SECTION("rotates about an explicit centre")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);

    REQUIRE(
      dispatch(
        map,
        nlohmann::json{
          {"tool", "rotate"},
          {"params", {{"angle", 180}, {"axis", "z"}, {"center", {0, 0, 0}}}}})["ok"]
      == true);

    const auto bounds = boxBounds();
    CHECK(bounds.min.x() == Approx(-64.0).margin(0.01));
    CHECK(bounds.max.x() == Approx(0.0).margin(0.01));
  }

  SECTION("requires an angle")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);

    const auto response =
      dispatch(map, nlohmann::json{{"tool", "rotate"}, {"params", {{"axis", "z"}}}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
  }
}

TEST_CASE("scale")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  const auto boxBounds = [&]() {
    return dynamic_cast<mdl::BrushNode*>(map.worldNode().defaultLayer()->children().front())
      ->logicalBounds();
  };

  SECTION("scales uniformly from a single number")
  {
    REQUIRE(dispatch(map, createBrushRequest({-32, -32, -32}, {32, 32, 32}))["ok"] == true);

    REQUIRE(
      dispatch(map, nlohmann::json{{"tool", "scale"}, {"params", {{"factors", 2}}}})["ok"]
      == true);

    CHECK(boxBounds() == vm::bbox3d{{-64, -64, -64}, {64, 64, 64}});
  }

  SECTION("scales per axis")
  {
    REQUIRE(dispatch(map, createBrushRequest({-32, -32, -32}, {32, 32, 32}))["ok"] == true);

    REQUIRE(
      dispatch(
        map,
        nlohmann::json{{"tool", "scale"}, {"params", {{"factors", {1, 1, 4}}}}})["ok"]
      == true);

    CHECK(boxBounds() == vm::bbox3d{{-32, -32, -128}, {32, 32, 128}});
  }

  SECTION("fits objects into a target box")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);

    REQUIRE(
      dispatch(
        map,
        nlohmann::json{
          {"tool", "scale"},
          {"params",
           {{"bounds", {{"min", {0, 0, 0}}, {"max", {256, 128, 32}}}}}}})["ok"]
      == true);

    CHECK(boxBounds() == vm::bbox3d{{0, 0, 0}, {256, 128, 32}});
  }

  SECTION("rejects a zero factor that would collapse the geometry")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);

    const auto response = dispatch(
      map, nlohmann::json{{"tool", "scale"}, {"params", {{"factors", {1, 1, 0}}}}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
    CHECK(boxBounds() == vm::bbox3d{{0, 0, 0}, {64, 64, 64}});
  }

  SECTION("requires factors or bounds")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);

    const auto response =
      dispatch(map, nlohmann::json{{"tool", "scale"}, {"params", nlohmann::json::object()}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
  }
}

TEST_CASE("transforms leave the user's selection alone")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);
  REQUIRE(dispatch(map, createBrushRequest({256, 0, 0}, {320, 64, 64}))["ok"] == true);
  const auto selectionBefore = map.selection().nodes;
  REQUIRE(selectionBefore.size() == 1u);

  REQUIRE(
    dispatch(map, nlohmann::json{{"tool", "translate"}, {"params", {{"delta", {0, 0, 64}}}}})
      ["ok"]
    == true);

  CHECK(map.selection().nodes == selectionBefore);
}

TEST_CASE("csg_subtract")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  SECTION("carves a notch out of a wall and consumes the tool brush")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {256, 32, 128}))["ok"] == true);
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 32, 64}))["ok"] == true);
    REQUIRE(brushCount(map) == 2u);

    const auto response =
      dispatch(map, nlohmann::json{{"tool", "csg_subtract"}, {"params", {}}});

    REQUIRE(response["ok"] == true);
    CHECK(response["result"]["created"] > 0);
    CHECK(brushCount(map) == size_t(response["result"]["created"]));

    const auto corner = vm::vec3d{32, 16, 32};
    for (const auto* child : map.worldNode().defaultLayer()->children())
    {
      const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(child);
      REQUIRE(brushNode != nullptr);
      CHECK_FALSE(brushNode->brush().containsPoint(corner));
    }
  }

  SECTION("the whole carve is a single undo step")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {256, 32, 128}))["ok"] == true);
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 32, 64}))["ok"] == true);

    REQUIRE(dispatch(map, nlohmann::json{{"tool", "csg_subtract"}, {"params", {}}})["ok"]
            == true);

    map.undoCommand();

    CHECK(brushCount(map) == 2u);
  }

  SECTION("build a wall and carve it in one batch, then transform the result")
  {
    const auto response = dispatch(
      map,
      nlohmann::json{
        {"tool", "batch"},
        {"params",
         {{"name", "carve and move"},
          {"ops",
           nlohmann::json::array(
             {createBrushRequest({0, 0, 0}, {256, 32, 128}),
              createBrushRequest({0, 0, 0}, {64, 32, 64}),
              nlohmann::json{
                {"tool", "csg_subtract"}, {"params", {{"target", "last"}}}},
              nlohmann::json{
                {"tool", "translate"}, {"params", {{"delta", {0, 0, 512}}}}}})}}}});

    REQUIRE(response["ok"] == true);
    REQUIRE(brushCount(map) > 0u);

    for (const auto* child : map.worldNode().defaultLayer()->children())
    {
      CHECK(child->logicalBounds().min.z() >= 512.0);
    }
  }

  SECTION("target 'last' subtracts only the most recent brush")
  {
    REQUIRE(
      dispatch(
        map,
        nlohmann::json{
          {"tool", "batch"},
          {"params",
           {{"ops",
             nlohmann::json::array(
               {createBrushRequest({0, 0, 0}, {256, 32, 128}),
                createBrushRequest({0, 0, 0}, {64, 32, 64}),
                nlohmann::json{
                  {"tool", "csg_subtract"},
                  {"params", {{"target", "last"}}}}})}}}})["ok"]
      == true);

    REQUIRE(brushCount(map) > 0u);
    for (const auto* child : map.worldNode().defaultLayer()->children())
    {
      const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(child);
      REQUIRE(brushNode != nullptr);
      CHECK_FALSE(brushNode->brush().containsPoint(vm::vec3d{32, 16, 32}));
    }
  }

  SECTION("reports when nothing is selected")
  {
    const auto response =
      dispatch(map, nlohmann::json{{"tool", "csg_subtract"}, {"params", {}}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
  }
}

TEST_CASE("csg_merge")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  SECTION("merges two boxes into one convex brush")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);
    REQUIRE(dispatch(map, createBrushRequest({64, 0, 0}, {128, 64, 64}))["ok"] == true);
    mdl::selectAllNodes(map);
    REQUIRE(brushCount(map) == 2u);

    const auto response = dispatch(
      map,
      nlohmann::json{
        {"tool", "csg_merge"}, {"params", {{"target", "selection"}}}});

    REQUIRE(response["ok"] == true);
    CHECK(brushCount(map) == 1u);

    const auto* merged =
      dynamic_cast<mdl::BrushNode*>(map.worldNode().defaultLayer()->children().front());
    REQUIRE(merged != nullptr);
    CHECK(merged->logicalBounds() == vm::bbox3d{{0, 0, 0}, {128, 64, 64}});
  }
}

TEST_CASE("csg_intersect")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  SECTION("keeps only the shared volume")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {128, 128, 128}))["ok"] == true);
    REQUIRE(dispatch(map, createBrushRequest({64, 64, 0}, {192, 192, 128}))["ok"] == true);
    mdl::selectAllNodes(map);

    const auto response = dispatch(
      map,
      nlohmann::json{
        {"tool", "csg_intersect"}, {"params", {{"target", "selection"}}}});

    REQUIRE(response["ok"] == true);
    REQUIRE(brushCount(map) == 1u);

    const auto* result =
      dynamic_cast<mdl::BrushNode*>(map.worldNode().defaultLayer()->children().front());
    REQUIRE(result != nullptr);
    CHECK(result->logicalBounds() == vm::bbox3d{{64, 64, 0}, {128, 128, 128}});
  }

  SECTION("needs at least two brushes")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);

    const auto response =
      dispatch(map, nlohmann::json{{"tool", "csg_intersect"}, {"params", {}}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
    CHECK(brushCount(map) == 1u);
  }
}

TEST_CASE("csg_hollow")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  SECTION("turns one box into a sealed shell")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {512, 512, 256}))["ok"] == true);

    const auto response = dispatch(
      map, nlohmann::json{{"tool", "csg_hollow"}, {"params", {{"thickness", 16}}}});

    REQUIRE(response["ok"] == true);
    CHECK(brushCount(map) == 6u);

    for (const auto* child : map.worldNode().defaultLayer()->children())
    {
      const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(child);
      REQUIRE(brushNode != nullptr);
      CHECK_FALSE(brushNode->brush().containsPoint(vm::vec3d{256, 256, 128}));
    }
  }

  SECTION("thickness does not depend on the editor's grid")
  {
    map.grid().setSize(6);

    REQUIRE(
      dispatch(map, createBrushRequest({0, 0, 0}, {512, 512, 256}))["ok"] == true);
    REQUIRE(
      dispatch(map, nlohmann::json{{"tool", "csg_hollow"}, {"params", {{"thickness", 16}}}})
        ["ok"]
      == true);

    auto insideAnyBrush = false;
    for (const auto* child : map.worldNode().defaultLayer()->children())
    {
      const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(child);
      REQUIRE(brushNode != nullptr);
      insideAnyBrush =
        insideAnyBrush || brushNode->brush().containsPoint(vm::vec3d{256, 256, 32});
    }
    CHECK_FALSE(insideAnyBrush);
  }

  SECTION("restores the grid afterwards")
  {
    map.grid().setSize(5);
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {512, 512, 256}))["ok"] == true);

    REQUIRE(
      dispatch(map, nlohmann::json{{"tool", "csg_hollow"}, {"params", {{"thickness", 16}}}})
        ["ok"]
      == true);

    CHECK(map.grid().size() == 5);
  }

  SECTION("rejects a thickness that is not a power of two")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {512, 512, 256}))["ok"] == true);

    const auto response = dispatch(
      map, nlohmann::json{{"tool", "csg_hollow"}, {"params", {{"thickness", 24}}}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
    CHECK(brushCount(map) == 1u);
  }
}

TEST_CASE("list_entity_definitions")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);
  installEntityDefinitions(map);

  const auto response =
    dispatch(map, nlohmann::json{{"tool", "list_entity_definitions"}});

  REQUIRE(response["ok"] == true);
  CHECK(response["result"]["game"] == "Quake");
  const auto& point = response["result"]["point"];
  CHECK(std::ranges::find(point, "info_player_start") != point.end());
  CHECK(std::ranges::find(point, "light") != point.end());
}

TEST_CASE("create_entity")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);
  installEntityDefinitions(map);

  const auto entityCount = [&]() {
    auto count = size_t{0};
    for (const auto* child : map.worldNode().defaultLayer()->children())
    {
      if (dynamic_cast<const mdl::EntityNode*>(child))
      {
        ++count;
      }
    }
    return count;
  };

  SECTION("creates a point entity at an origin")
  {
    const auto response = dispatch(
      map,
      nlohmann::json{
        {"tool", "create_entity"},
        {"params",
         {{"classname", "info_player_start"}, {"origin", {64, 128, 24}}}}});

    REQUIRE(response["ok"] == true);
    CHECK(response["result"]["kind"] == "point");
    REQUIRE(entityCount() == 1u);

    const auto* entityNode =
      dynamic_cast<mdl::EntityNode*>(map.worldNode().defaultLayer()->children().front());
    REQUIRE(entityNode != nullptr);
    CHECK(entityNode->entity().classname() == "info_player_start");
    CHECK(entityNode->entity().origin() == vm::vec3d{64, 128, 24});
  }

  SECTION("applies extra properties")
  {
    REQUIRE(
      dispatch(
        map,
        nlohmann::json{
          {"tool", "create_entity"},
          {"params",
           {{"classname", "light"},
            {"origin", {0, 0, 64}},
            {"properties", {{"light", "300"}, {"targetname", "lamp1"}}}}}})["ok"]
      == true);

    const auto* entityNode =
      dynamic_cast<mdl::EntityNode*>(map.worldNode().defaultLayer()->children().front());
    REQUIRE(entityNode != nullptr);
    const auto* light = entityNode->entity().property("light");
    REQUIRE(light != nullptr);
    CHECK(*light == "300");
    const auto* targetname = entityNode->entity().property("targetname");
    REQUIRE(targetname != nullptr);
    CHECK(*targetname == "lamp1");
  }

  SECTION("turns brushes into a brush entity")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 128, 128}))["ok"] == true);

    const auto response = dispatch(
      map,
      nlohmann::json{
        {"tool", "create_entity"},
        {"params", {{"classname", "func_door"}, {"target", "selection"}}}});

    REQUIRE(response["ok"] == true);
    CHECK(response["result"]["kind"] == "brush");

    // The brush now belongs to the door rather than to worldspawn.
    REQUIRE(entityCount() == 1u);
    const auto* entityNode =
      dynamic_cast<mdl::EntityNode*>(map.worldNode().defaultLayer()->children().front());
    REQUIRE(entityNode != nullptr);
    CHECK(entityNode->entity().classname() == "func_door");
    CHECK(entityNode->childCount() == 1u);
  }

  SECTION("rejects a classname the game does not define")
  {
    const auto response = dispatch(
      map,
      nlohmann::json{
        {"tool", "create_entity"},
        {"params", {{"classname", "not_a_real_entity"}, {"origin", {0, 0, 0}}}}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
  }

  SECTION("requires an origin for a point entity")
  {
    const auto response = dispatch(
      map,
      nlohmann::json{
        {"tool", "create_entity"}, {"params", {{"classname", "info_player_start"}}}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
  }

  SECTION("creating an entity is a single undo step")
  {
    REQUIRE(
      dispatch(
        map,
        nlohmann::json{
          {"tool", "create_entity"},
          {"params",
           {{"classname", "light"},
            {"origin", {0, 0, 64}},
            {"properties", {{"light", "300"}}}}}})["ok"]
      == true);
    REQUIRE(entityCount() == 1u);

    map.undoCommand();

    CHECK(entityCount() == 0u);
  }
}

TEST_CASE("selectors")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);
  installEntityDefinitions(map);

  const auto selectRequest = [](nlohmann::json selector) {
    return nlohmann::json{
      {"tool", "select_objects"}, {"params", {{"select", std::move(selector)}}}};
  };

  SECTION("by material")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}, "stone"))["ok"] == true);
    REQUIRE(dispatch(map, createBrushRequest({128, 0, 0}, {192, 64, 64}, "wood"))["ok"] == true);
    REQUIRE(dispatch(map, createBrushRequest({256, 0, 0}, {320, 64, 64}, "stone"))["ok"] == true);

    const auto response = dispatch(map, selectRequest({{"material", "stone"}}));

    REQUIRE(response["ok"] == true);
    CHECK(response["result"]["selected"] == 2);
    CHECK(map.selection().brushes.size() == 2u);
  }

  SECTION("by material prefix")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}, "wall_brick"))["ok"] == true);
    REQUIRE(dispatch(map, createBrushRequest({128, 0, 0}, {192, 64, 64}, "wall_stone"))["ok"] == true);
    REQUIRE(dispatch(map, createBrushRequest({256, 0, 0}, {320, 64, 64}, "floor"))["ok"] == true);

    const auto response = dispatch(map, selectRequest({{"material", "wall_*"}}));

    REQUIRE(response["ok"] == true);
    CHECK(response["result"]["selected"] == 2);
  }

  SECTION("by classname")
  {
    for (const auto* classname : {"light", "light", "info_player_start"})
    {
      REQUIRE(
        dispatch(
          map,
          nlohmann::json{
            {"tool", "create_entity"},
            {"params", {{"classname", classname}, {"origin", {0, 0, 0}}}}})["ok"]
        == true);
    }

    const auto response = dispatch(map, selectRequest({{"classname", "light"}}));

    REQUIRE(response["ok"] == true);
    CHECK(response["result"]["selected"] == 2);
  }

  SECTION("by bounds, contained versus touching")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);
    REQUIRE(dispatch(map, createBrushRequest({100, 0, 0}, {300, 64, 64}))["ok"] == true);

    const auto box = nlohmann::json{{"min", {-16, -16, -16}}, {"max", {128, 128, 128}}};

    const auto contained = dispatch(map, selectRequest({{"bounds", box}}));
    REQUIRE(contained["ok"] == true);
    CHECK(contained["result"]["selected"] == 1);

    const auto touching =
      dispatch(map, selectRequest({{"bounds", box}, {"mode", "touching"}}));
    REQUIRE(touching["ok"] == true);
    CHECK(touching["result"]["selected"] == 2);
  }

  SECTION("combining fields narrows the match")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}, "stone"))["ok"] == true);
    REQUIRE(dispatch(map, createBrushRequest({500, 0, 0}, {564, 64, 64}, "stone"))["ok"] == true);

    const auto response = dispatch(
      map,
      selectRequest(
        {{"material", "stone"},
         {"bounds", {{"min", {-16, -16, -16}}, {"max", {128, 128, 128}}}}}));

    REQUIRE(response["ok"] == true);
    CHECK(response["result"]["selected"] == 1);
  }

  SECTION("all")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);
    REQUIRE(dispatch(map, createBrushRequest({128, 0, 0}, {192, 64, 64}))["ok"] == true);

    const auto response = dispatch(map, selectRequest({{"all", true}}));

    REQUIRE(response["ok"] == true);
    CHECK(response["result"]["selected"] == 2);
  }

  SECTION("reports a selector that matches nothing")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}, "stone"))["ok"] == true);

    const auto response = dispatch(map, selectRequest({{"material", "lava"}}));

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
  }

  SECTION("rejects an empty selector rather than matching everything")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);

    const auto response = dispatch(map, selectRequest(nlohmann::json::object()));

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
  }

  SECTION("a transform can address geometry with a selector")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}, "stone"))["ok"] == true);
    REQUIRE(dispatch(map, createBrushRequest({128, 0, 0}, {192, 64, 64}, "wood"))["ok"] == true);

    REQUIRE(
      dispatch(
        map,
        nlohmann::json{
          {"tool", "translate"},
          {"params", {{"delta", {0, 0, 256}}, {"select", {{"material", "stone"}}}}}})["ok"]
      == true);

    // Only the stone brush moved.
    for (const auto* child : map.worldNode().defaultLayer()->children())
    {
      const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(child);
      REQUIRE(brushNode != nullptr);
      const auto isStone =
        brushNode->brush().faces().front().materialName() == "stone";
      CHECK(brushNode->logicalBounds().min.z() == (isStone ? 256.0 : 0.0));
    }
  }
}

TEST_CASE("flip")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  SECTION("mirrors about an explicit point")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);

    REQUIRE(
      dispatch(
        map,
        nlohmann::json{
          {"tool", "flip"}, {"params", {{"axis", "x"}, {"center", {0, 0, 0}}}}})["ok"]
      == true);

    const auto* brushNode =
      dynamic_cast<mdl::BrushNode*>(map.worldNode().defaultLayer()->children().front());
    REQUIRE(brushNode != nullptr);
    CHECK(brushNode->logicalBounds() == vm::bbox3d{{-64, 0, 0}, {0, 64, 64}});
  }

  SECTION("mirroring about its own centre leaves a symmetric box where it was")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {64, 64, 64}))["ok"] == true);

    REQUIRE(
      dispatch(map, nlohmann::json{{"tool", "flip"}, {"params", {{"axis", "x"}}}})["ok"]
      == true);

    const auto* brushNode =
      dynamic_cast<mdl::BrushNode*>(map.worldNode().defaultLayer()->children().front());
    REQUIRE(brushNode != nullptr);
    CHECK(brushNode->logicalBounds() == vm::bbox3d{{0, 0, 0}, {64, 64, 64}});
  }
}

TEST_CASE("shear")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  SECTION("slants a box")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {128, 128, 128}))["ok"] == true);

    const auto response = dispatch(
      map,
      nlohmann::json{
        {"tool", "shear"}, {"params", {{"axis", "z"}, {"delta", {64, 0, 0}}}}});

    REQUIRE(response["ok"] == true);

    // The top slid along X, so the footprint now spans further than the original box.
    const auto* brushNode =
      dynamic_cast<mdl::BrushNode*>(map.worldNode().defaultLayer()->children().front());
    REQUIRE(brushNode != nullptr);
    CHECK(brushNode->logicalBounds().max.x() == Approx(192.0).margin(0.01));
  }

  SECTION("requires a delta")
  {
    REQUIRE(dispatch(map, createBrushRequest({0, 0, 0}, {128, 128, 128}))["ok"] == true);

    const auto response =
      dispatch(map, nlohmann::json{{"tool", "shear"}, {"params", {{"axis", "z"}}}});

    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_parameters");
  }
}

TEST_CASE("tools that need the editor UI")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create(mdl::QuakeFixtureConfig);

  // Headless, so there is no host: these must fail with an explanation rather than
  // appearing to work or crashing.
  const auto tool = GENERATE(
    std::string{"invoke_action"}, "list_actions", "capture_viewport");
  CAPTURE(tool);

  const auto response =
    dispatch(map, nlohmann::json{{"tool", tool}, {"params", {{"path", "Menu/Edit/Delete"}}}});

  CHECK(response["ok"] == false);
  CHECK(response["error"]["code"] == "operation_failed");
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
