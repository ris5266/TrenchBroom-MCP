#include "mcp/Tool.h"

#include "mcp/JsonUtils.h"

#include "mdl/Brush.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/GameInfo.h"
#include "mdl/Grid.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/PushSelection.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/Selection.h"
#include "mdl/WorldNode.h"

#include "kd/overload.h"

#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <variant>

namespace tb::mcp
{
namespace
{

nlohmann::json emptySchema()
{
  return nlohmann::json{
    {"type", "object"}, {"properties", nlohmann::json::object()}, {"required", nlohmann::json::array()}};
}

nlohmann::json vec3Schema(const std::string& description)
{
  return nlohmann::json{
    {"type", "array"},
    {"description", description},
    {"items", {{"type", "number"}}},
    {"minItems", 3},
    {"maxItems", 3}};
}

nlohmann::json boundsSchema()
{
  return nlohmann::json{
    {"type", "object"},
    {"description",
     "An axis-aligned box in world units. Corners may be given in either order."},
    {"properties",
     {{"min", vec3Schema("One corner, as [x, y, z].")},
      {"max", vec3Schema("The opposite corner, as [x, y, z].")}}},
    {"required", nlohmann::json::array({"min", "max"})}};
}

// -- ping ------------------------------------------------------------------------------

Result<nlohmann::json, ToolError> ping(ToolContext& context, const nlohmann::json&)
{
  const auto& map = context.map;
  return nlohmann::json{
    {"ok", true},
    {"mapFormat", mdl::formatName(map.worldNode().mapFormat())},
    {"game", map.gameInfo().gameConfig.name},
    {"path", map.path().string()},
    {"modified", map.modified()}};
}

// -- create_brush ----------------------------------------------------------------------

Result<nlohmann::json, ToolError> createBrush(
  ToolContext& context, const nlohmann::json& params)
{
  auto& map = context.map;

  return readBounds(params, "bounds")
         | kdl::and_then([&](const auto& bounds) -> Result<nlohmann::json, ToolError> {
             if (!map.worldBounds().contains(bounds))
             {
               const auto& worldBounds = map.worldBounds();
               return ToolError{
                 ErrorCode::InvalidParameters,
                 fmt::format(
                   "bounds lie outside the world bounds ([{} {} {}] .. [{} {} {}])",
                   worldBounds.min.x(),
                   worldBounds.min.y(),
                   worldBounds.min.z(),
                   worldBounds.max.x(),
                   worldBounds.max.y(),
                   worldBounds.max.z())};
             }

             const auto materialName =
               readString(params, "material", map.currentMaterialName());

             const auto& faceAttribsConfig = map.gameInfo().gameConfig.faceAttribsConfig;
             const auto builder = mdl::BrushBuilder{
               map.worldNode().mapFormat(),
               map.worldBounds(),
               faceAttribsConfig.defaultUvAttributes,
               faceAttribsConfig.defaultSurfaceAttributes};

             return builder.createCuboid(bounds, materialName)
                    | kdl::transform([&](auto brush) {
                        auto* brushNode = new mdl::BrushNode{std::move(brush)};
                        return brushNode;
                      })
                    | kdl::and_then(
                      [&](auto* brushNode) -> Result<nlohmann::json, ToolError> {
                        auto* parent = mdl::parentForNodes(map);
                        if (mdl::addNodes(map, {{parent, {brushNode}}}).empty())
                        {
                          delete brushNode;
                          return ToolError{
                            ErrorCode::OperationFailed, "could not add brush to the map"};
                        }

                        context.createdNodes.push_back(brushNode);
                        return nlohmann::json{
                          {"created", 1}, {"bounds", toJson(brushNode->logicalBounds())}};
                      })
                    | kdl::or_else([](const auto& e) -> Result<nlohmann::json, ToolError> {
                        if constexpr (std::is_same_v<std::decay_t<decltype(e)>, ToolError>)
                        {
                          return e;
                        }
                        else
                        {
                          return ToolError{ErrorCode::OperationFailed, e.msg};
                        }
                      });
           });
}

// -- get_scene -------------------------------------------------------------------------

nlohmann::json summarizeChildren(const std::vector<mdl::Node*>& children)
{
  auto brushCount = size_t{0};
  auto patchCount = size_t{0};
  auto classnameCounts = std::map<std::string, size_t>{};
  auto groups = nlohmann::json::array();

  for (const auto* child : children)
  {
    child->accept(kdl::overload(
      [](const mdl::WorldNode&) {},
      [](const mdl::LayerNode&) {},
      [&](const mdl::GroupNode& groupNode) {
        auto group = nlohmann::json{
          {"name", groupNode.group().name()},
          {"bounds", toJson(groupNode.logicalBounds())},
          {"contents", summarizeChildren(groupNode.children())}};
        if (const auto& id = groupNode.persistentId())
        {
          group["id"] = *id;
        }
        groups.push_back(std::move(group));
      },
      [&](const mdl::EntityNode& entityNode) {
        ++classnameCounts[entityNode.entity().classname()];
      },
      [&](const mdl::BrushNode&) { ++brushCount; },
      [&](const mdl::PatchNode&) { ++patchCount; }));
  }

  auto entities = nlohmann::json::array();
  for (const auto& [classname, count] : classnameCounts)
  {
    entities.push_back({{"classname", classname}, {"count", count}});
  }

  auto result = nlohmann::json{{"brushes", brushCount}, {"entities", std::move(entities)}};
  if (patchCount > 0)
  {
    result["patches"] = patchCount;
  }
  if (!groups.empty())
  {
    result["groups"] = std::move(groups);
  }
  return result;
}

Result<nlohmann::json, ToolError> getScene(ToolContext& context, const nlohmann::json&)
{
  auto& map = context.map;
  const auto& worldNode = map.worldNode();

  auto layers = nlohmann::json::array();
  for (const auto* layerNode : worldNode.allLayers())
  {
    auto layer = nlohmann::json{
      {"name", layerNode->layer().name()},
      {"default", layerNode == worldNode.defaultLayer()},
      {"contents", summarizeChildren(layerNode->children())}};
    if (const auto& id = layerNode->persistentId())
    {
      layer["id"] = *id;
    }
    layers.push_back(std::move(layer));
  }

  const auto& selection = map.selection();
  auto selectionJson = nlohmann::json{
    {"nodes", selection.nodes.size()},
    {"brushes", selection.brushes.size()},
    {"entities", selection.entities.size()},
    {"groups", selection.groups.size()},
    {"brushFaces", selection.brushFaces.size()}};
  if (const auto& bounds = map.selectionBounds())
  {
    selectionJson["bounds"] = toJson(*bounds);
  }

  return nlohmann::json{
    {"mapFormat", mdl::formatName(worldNode.mapFormat())},
    {"game", map.gameInfo().gameConfig.name},
    {"path", map.path().string()},
    {"modified", map.modified()},
    {"worldBounds", toJson(map.worldBounds())},
    {"gridSize", map.grid().actualSize()},
    {"currentMaterial", map.currentMaterialName()},
    {"selection", std::move(selectionJson)},
    {"layers", std::move(layers)}};
}

// -- set_worldspawn_property -----------------------------------------------------------

Result<nlohmann::json, ToolError> setWorldspawnProperty(
  ToolContext& context, const nlohmann::json& params)
{
  auto& map = context.map;

  const auto key = readString(params, "key");
  if (key.empty())
  {
    return ToolError{ErrorCode::InvalidParameters, "missing 'key'"};
  }
  if (!params.contains("value") || !params.at("value").is_string())
  {
    return ToolError{ErrorCode::InvalidParameters, "missing 'value' string"};
  }
  const auto value = params.at("value").get<std::string>();

  const auto pushSelection = mdl::PushSelection{map};
  mdl::deselectAll(map);

  if (!mdl::setEntityProperty(map, key, value))
  {
    return ToolError{
      ErrorCode::OperationFailed, fmt::format("could not set '{}' on worldspawn", key)};
  }

  return nlohmann::json{{"key", key}, {"value", value}};
}

// -- batch -----------------------------------------------------------------------------

Result<nlohmann::json, ToolError> batch(ToolContext& context, const nlohmann::json& params)
{
  if (!params.contains("ops") || !params.at("ops").is_array())
  {
    return ToolError{ErrorCode::InvalidParameters, "'ops' must be an array"};
  }

  auto results = nlohmann::json::array();
  const auto& ops = params.at("ops");

  for (size_t i = 0; i < ops.size(); ++i)
  {
    const auto& op = ops[i];
    if (!op.is_object() || !op.contains("tool") || !op.at("tool").is_string())
    {
      return ToolError{
        ErrorCode::InvalidRequest,
        fmt::format("ops[{}] must be an object with a 'tool' string", i)};
    }

    const auto toolName = op.at("tool").get<std::string>();
    if (toolName == "batch")
    {
      return ToolError{
        ErrorCode::InvalidRequest, fmt::format("ops[{}]: batch cannot nest", i)};
    }

    const auto* tool = findTool(toolName);
    if (!tool)
    {
      return ToolError{
        ErrorCode::UnknownTool, fmt::format("ops[{}]: unknown tool '{}'", i, toolName)};
    }

    const auto opParams =
      op.contains("params") ? op.at("params") : nlohmann::json::object();

    auto opResult = tool->handler(context, opParams);
    if (opResult.is_error())
    {
      const auto error = std::get<ToolError>(std::move(opResult).error());
      return ToolError{
        error.code, fmt::format("ops[{}] ({}): {}", i, toolName, error.message)};
    }
    results.push_back(std::move(opResult).value());
  }

  return nlohmann::json{{"ops", results.size()}, {"results", std::move(results)}};
}

const auto toolTable = std::vector<Tool>{
  Tool{
    "ping",
    "Check that TrenchBroom is reachable and report which map is open.",
    ToolKind::ReadOnly,
    emptySchema(),
    ping},
  Tool{
    "get_scene",
    "Summarise the open map: layers, groups, brush counts, entity classname counts, "
    "world bounds, grid size and the current selection. Drill into a group for detail "
    "rather than asking for every brush.",
    ToolKind::ReadOnly,
    emptySchema(),
    getScene},
  Tool{
    "create_brush",
    "Create an axis-aligned box brush in the current layer.",
    ToolKind::Mutating,
    nlohmann::json{
      {"type", "object"},
      {"properties",
       {{"bounds", boundsSchema()},
        {"material",
         {{"type", "string"},
          {"description",
           "Material to apply to all six faces. Defaults to the editor's current "
           "material."}}}}},
      {"required", nlohmann::json::array({"bounds"})}},
    createBrush},
  Tool{
    "set_worldspawn_property",
    "Set a key on the worldspawn entity, such as 'wad' to attach a texture WAD, or "
    "'message' to name the level.",
    ToolKind::Mutating,
    nlohmann::json{
      {"type", "object"},
      {"properties",
       {{"key", {{"type", "string"}, {"description", "Property key, e.g. \"wad\"."}}},
        {"value", {{"type", "string"}, {"description", "Property value."}}}}},
      {"required", nlohmann::json::array({"key", "value"})}},
    setWorldspawnProperty},
  Tool{
    "batch",
    "Run several operations as a single undo step. Either all of them apply or, if any "
    "fails, none do. Use this for anything built from more than one or two brushes.",
    ToolKind::Mutating,
    nlohmann::json{
      {"type", "object"},
      {"properties",
       {{"name",
         {{"type", "string"},
          {"description", "Name shown in TrenchBroom's undo history."}}},
        {"ops",
         {{"type", "array"},
          {"description", "Operations to run in order."},
          {"items",
           {{"type", "object"},
            {"properties",
             {{"tool", {{"type", "string"}}}, {"params", {{"type", "object"}}}}},
            {"required", nlohmann::json::array({"tool"})}}}}}}},
      {"required", nlohmann::json::array({"ops"})}},
    batch},
};

} // namespace

std::string toString(const ErrorCode errorCode)
{
  switch (errorCode)
  {
  case ErrorCode::InvalidRequest:
    return "invalid_request";
  case ErrorCode::UnknownTool:
    return "unknown_tool";
  case ErrorCode::InvalidParameters:
    return "invalid_parameters";
  case ErrorCode::OperationFailed:
    return "operation_failed";
  }
  return "unknown";
}

const std::vector<Tool>& tools()
{
  return toolTable;
}

const Tool* findTool(const std::string& name)
{
  const auto it = std::ranges::find(toolTable, name, &Tool::name);
  return it != toolTable.end() ? &*it : nullptr;
}

}
