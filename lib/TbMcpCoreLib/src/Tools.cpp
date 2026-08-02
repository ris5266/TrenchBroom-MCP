#include "mcp/Tool.h"

#include "mcp/JsonUtils.h"

#include "mdl/Brush.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/CircleShape.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/GameInfo.h"
#include "mdl/Grid.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Geometry.h"
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

#include "vm/scalar.h"

#include <algorithm>
#include <functional>
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

nlohmann::json axisSchema()
{
  return nlohmann::json{
    {"type", "string"},
    {"enum", nlohmann::json::array({"x", "y", "z"})},
    {"description", "Axis the shape is revolved around. Defaults to \"z\" (upright)."}};
}

nlohmann::json circleSchema()
{
  return nlohmann::json{
    {"type", "object"},
    {"description",
     "How the cross-section is approximated. Defaults to 8 edge-aligned sides."},
    {"properties",
     {{"alignment",
       {{"type", "string"},
        {"enum", nlohmann::json::array({"edge", "vertex", "scalable"})},
        {"description",
         "\"edge\" puts a flat face on the axes, \"vertex\" puts a corner there, "
         "\"scalable\" subdivides so the shape survives non-uniform scaling."}}},
      {"sides",
       {{"type", "integer"},
        {"minimum", 3},
        {"description", "Number of sides, for edge and vertex alignment."}}},
      {"precision",
       {{"type", "integer"},
        {"minimum", 0},
        {"description", "Subdivision level, for scalable alignment."}}}}}};
}

nlohmann::json materialSchema()
{
  return nlohmann::json{
    {"type", "string"},
    {"description",
     "Material to apply to every face. Defaults to the editor's current material."}};
}

// -- shared parameter reading ----------------------------------------------------------
Result<vm::bbox3d, ToolError> readSceneBounds(
  const mdl::Map& map, const nlohmann::json& params)
{
  return readBounds(params, "bounds")
         | kdl::and_then([&](const auto& bounds) -> Result<vm::bbox3d, ToolError> {
             const auto& worldBounds = map.worldBounds();
             if (!worldBounds.contains(bounds))
             {
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
             return bounds;
           });
}

Result<vm::axis::type, ToolError> readAxis(const nlohmann::json& params)
{
  const auto value = readString(params, "axis", "z");
  if (value == "x")
  {
    return vm::axis::x;
  }
  if (value == "y")
  {
    return vm::axis::y;
  }
  if (value == "z")
  {
    return vm::axis::z;
  }
  return ToolError{
    ErrorCode::InvalidParameters, fmt::format("'axis' must be x, y or z, got '{}'", value)};
}

Result<mdl::CircleShape, ToolError> readCircleShape(const nlohmann::json& params)
{
  if (!params.contains("circle"))
  {
    return mdl::CircleShape{mdl::EdgeAlignedCircle{}};
  }

  const auto& circle = params.at("circle");
  if (!circle.is_object())
  {
    return ToolError{ErrorCode::InvalidParameters, "'circle' must be an object"};
  }

  const auto alignment = readString(circle, "alignment", "edge");

  if (alignment == "scalable")
  {
    const auto precision = circle.value("precision", 0);
    if (precision < 0)
    {
      return ToolError{ErrorCode::InvalidParameters, "'circle.precision' must be >= 0"};
    }
    return mdl::CircleShape{mdl::ScalableCircle{size_t(precision)}};
  }

  const auto sides = circle.value("sides", 8);
  if (sides < 3)
  {
    return ToolError{
      ErrorCode::InvalidParameters, "'circle.sides' must be at least 3"};
  }

  if (alignment == "edge")
  {
    return mdl::CircleShape{mdl::EdgeAlignedCircle{size_t(sides)}};
  }
  if (alignment == "vertex")
  {
    return mdl::CircleShape{mdl::VertexAlignedCircle{size_t(sides)}};
  }
  return ToolError{
    ErrorCode::InvalidParameters,
    fmt::format(
      "'circle.alignment' must be edge, vertex or scalable, got '{}'", alignment)};
}

Result<double, ToolError> readThickness(const nlohmann::json& params)
{
  if (!params.contains("thickness") || !params.at("thickness").is_number())
  {
    return ToolError{ErrorCode::InvalidParameters, "missing 'thickness' number"};
  }
  const auto thickness = params.at("thickness").get<double>();
  if (thickness <= 0.0)
  {
    return ToolError{ErrorCode::InvalidParameters, "'thickness' must be positive"};
  }
  return thickness;
}

mdl::BrushBuilder makeBuilder(const mdl::Map& map)
{
  const auto& faceAttribsConfig = map.gameInfo().gameConfig.faceAttribsConfig;
  return mdl::BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    faceAttribsConfig.defaultUvAttributes,
    faceAttribsConfig.defaultSurfaceAttributes};
}

Result<nlohmann::json, ToolError> addBrushes(
  ToolContext& context, std::vector<mdl::Brush> brushes)
{
  auto& map = context.map;

  if (brushes.empty())
  {
    return ToolError{ErrorCode::OperationFailed, "the shape produced no brushes"};
  }

  auto nodes = std::vector<mdl::Node*>{};
  nodes.reserve(brushes.size());
  for (auto& brush : brushes)
  {
    nodes.push_back(new mdl::BrushNode{std::move(brush)});
  }

  auto* parent = mdl::parentForNodes(map);
  if (mdl::addNodes(map, {{parent, nodes}}).empty())
  {
    for (auto* node : nodes)
    {
      delete node;
    }
    return ToolError{ErrorCode::OperationFailed, "could not add the brushes to the map"};
  }

  auto bounds = nodes.front()->logicalBounds();
  for (auto* node : nodes)
  {
    bounds = vm::merge(bounds, node->logicalBounds());
    context.createdNodes.push_back(node);
  }

  return nlohmann::json{{"created", nodes.size()}, {"bounds", toJson(bounds)}};
}

/** Maps a BrushBuilder failure onto a tool error without losing its message. */
Result<nlohmann::json, ToolError> toToolError(
  Result<std::vector<mdl::Brush>> result, ToolContext& context)
{
  return std::move(result)
         | kdl::and_then([&](auto brushes) { return addBrushes(context, std::move(brushes)); })
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
}

Result<nlohmann::json, ToolError> toToolError(
  Result<mdl::Brush> result, ToolContext& context)
{
  return toToolError(
    std::move(result) | kdl::transform([](auto brush) {
      auto brushes = std::vector<mdl::Brush>{};
      brushes.push_back(std::move(brush));
      return brushes;
    }),
    context);
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

  return readSceneBounds(map, params)
         | kdl::and_then([&](const auto& bounds) {
             const auto materialName =
               readString(params, "material", map.currentMaterialName());
             return toToolError(
               makeBuilder(map).createCuboid(bounds, materialName), context);
           });
}

// -- create_cylinder -------------------------------------------------------------------

Result<nlohmann::json, ToolError> createCylinder(
  ToolContext& context, const nlohmann::json& params)
{
  auto& map = context.map;

  return readSceneBounds(map, params) | kdl::and_then([&](const auto& bounds) {
           return readAxis(params) | kdl::and_then([&](const auto axis) {
                    return readCircleShape(params)
                           | kdl::and_then(
                             [&](const auto& circleShape)
                               -> Result<nlohmann::json, ToolError> {
                               const auto materialName = readString(
                                 params, "material", map.currentMaterialName());
                               const auto builder = makeBuilder(map);

                               if (params.contains("thickness"))
                               {
                                 return readThickness(params)
                                        | kdl::and_then([&](const auto thickness) {
                                            return toToolError(
                                              builder.createHollowCylinder(
                                                bounds,
                                                thickness,
                                                circleShape,
                                                axis,
                                                materialName),
                                              context);
                                          });
                               }

                               return toToolError(
                                 builder.createCylinder(
                                   bounds, circleShape, axis, materialName),
                                 context);
                             });
                  });
         });
}

// -- create_cone -----------------------------------------------------------------------

Result<nlohmann::json, ToolError> createCone(
  ToolContext& context, const nlohmann::json& params)
{
  auto& map = context.map;

  return readSceneBounds(map, params) | kdl::and_then([&](const auto& bounds) {
           return readAxis(params) | kdl::and_then([&](const auto axis) {
                    return readCircleShape(params) | kdl::and_then([&](const auto& circle) {
                             const auto materialName = readString(
                               params, "material", map.currentMaterialName());
                             return toToolError(
                               makeBuilder(map).createCone(
                                 bounds, circle, axis, materialName),
                               context);
                           });
                  });
         });
}

// -- create_sphere ---------------------------------------------------------------------

Result<nlohmann::json, ToolError> createSphere(
  ToolContext& context, const nlohmann::json& params)
{
  auto& map = context.map;

  return readSceneBounds(map, params)
         | kdl::and_then([&](const auto& bounds) -> Result<nlohmann::json, ToolError> {
             const auto materialName =
               readString(params, "material", map.currentMaterialName());
             const auto kind = readString(params, "kind", "uv");
             const auto builder = makeBuilder(map);

             // The icosphere has uniform triangles and takes a subdivision count; the UV
             // sphere is a stack of rings and takes a cross-section like the cylinder.
             if (kind == "ico")
             {
               const auto iterations = params.value("iterations", 1);
               if (iterations < 0)
               {
                 return ToolError{
                   ErrorCode::InvalidParameters, "'iterations' must be >= 0"};
               }
               return toToolError(
                 builder.createIcoSphere(bounds, size_t(iterations), materialName),
                 context);
             }

             if (kind != "uv")
             {
               return ToolError{
                 ErrorCode::InvalidParameters,
                 fmt::format("'kind' must be uv or ico, got '{}'", kind)};
             }

             const auto rings = params.value("rings", 8);
             if (rings < 1)
             {
               return ToolError{ErrorCode::InvalidParameters, "'rings' must be >= 1"};
             }

             return readAxis(params) | kdl::and_then([&](const auto axis) {
                      return readCircleShape(params) | kdl::and_then([&](const auto& c) {
                               return toToolError(
                                 builder.createUvSphere(
                                   bounds, c, size_t(rings), axis, materialName),
                                 context);
                             });
                    });
           });
}

// -- create_arch -----------------------------------------------------------------------

Result<nlohmann::json, ToolError> createArch(
  ToolContext& context, const nlohmann::json& params)
{
  auto& map = context.map;

  return readSceneBounds(map, params) | kdl::and_then([&](const auto& bounds) {
           return readAxis(params) | kdl::and_then([&](const auto axis) {
                    return readThickness(params) | kdl::and_then([&](const auto thickness) {
                             return readCircleShape(params)
                                    | kdl::and_then([&](const auto& circle) {
                                        const auto materialName = readString(
                                          params, "material", map.currentMaterialName());
                                        return toToolError(
                                          makeBuilder(map).createArch(
                                            bounds,
                                            thickness,
                                            circle,
                                            axis,
                                            materialName),
                                          context);
                                      });
                           });
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

// -- transforms ------------------------------------------------------------------------

nlohmann::json targetSchema()
{
  return nlohmann::json{
    {"type", "string"},
    {"enum", nlohmann::json::array({"auto", "created", "selection"})},
    {"description",
     "What to act on. \"created\" is whatever earlier operations in this same call "
     "produced, \"selection\" is what is selected in the editor, and \"auto\" (the "
     "default) means created-if-any, otherwise the selection. Inside a batch, \"auto\" "
     "lets you build a shape and then transform it."}};
}

Result<std::vector<mdl::Node*>, ToolError> resolveTargets(
  ToolContext& context, const nlohmann::json& params)
{
  const auto target = readString(params, "target", "auto");
  const auto& selected = context.map.selection().nodes;

  const auto nodes = target == "created"    ? context.createdNodes
                     : target == "selection" ? selected
                     : target == "auto"
                       ? (!context.createdNodes.empty() ? context.createdNodes : selected)
                       : std::vector<mdl::Node*>{};

  if (target != "auto" && target != "created" && target != "selection")
  {
    return ToolError{
      ErrorCode::InvalidParameters,
      fmt::format("'target' must be auto, created or selection, got '{}'", target)};
  }

  if (nodes.empty())
  {
    return ToolError{
      ErrorCode::InvalidParameters,
      target == "created"
        ? "nothing was created earlier in this call to transform"
        : "nothing is selected in the editor to transform"};
  }

  return nodes;
}

vm::bbox3d boundsOf(const std::vector<mdl::Node*>& nodes)
{
  auto bounds = nodes.front()->logicalBounds();
  for (const auto* node : nodes)
  {
    bounds = vm::merge(bounds, node->logicalBounds());
  }
  return bounds;
}

Result<nlohmann::json, ToolError> applyToTargets(
  ToolContext& context,
  const std::vector<mdl::Node*>& nodes,
  const std::string& what,
  const std::function<bool(mdl::Map&)>& apply)
{
  auto& map = context.map;

  const auto pushSelection = mdl::PushSelection{map};
  mdl::deselectAll(map);
  mdl::selectNodes(map, nodes);

  if (!apply(map))
  {
    return ToolError{
      ErrorCode::OperationFailed,
      fmt::format("could not {} the selected objects", what)};
  }

  return nlohmann::json{{"transformed", nodes.size()}, {"bounds", toJson(boundsOf(nodes))}};
}

Result<vm::vec3d, ToolError> readCenter(
  const nlohmann::json& params, const std::vector<mdl::Node*>& nodes)
{
  if (!params.contains("center"))
  {
    return boundsOf(nodes).center();
  }
  return readVec3(params, "center");
}

Result<nlohmann::json, ToolError> translate(
  ToolContext& context, const nlohmann::json& params)
{
  return resolveTargets(context, params) | kdl::and_then([&](const auto& nodes) {
           return readVec3(params, "delta") | kdl::and_then([&](const auto& delta) {
                    return applyToTargets(
                      context, nodes, "move", [&](auto& map) {
                        return mdl::translateSelection(map, delta);
                      });
                  });
         });
}

Result<nlohmann::json, ToolError> rotate(ToolContext& context, const nlohmann::json& params)
{
  return resolveTargets(context, params)
         | kdl::and_then([&](const auto& nodes) -> Result<nlohmann::json, ToolError> {
             if (!params.contains("angle") || !params.at("angle").is_number())
             {
               return ToolError{ErrorCode::InvalidParameters, "missing 'angle' number"};
             }
             const auto angle = vm::to_radians(params.at("angle").get<double>());

             return readAxis(params) | kdl::and_then([&](const auto axisIndex) {
                      auto axis = vm::vec3d{0, 0, 0};
                      axis[axisIndex] = 1.0;

                      return readCenter(params, nodes)
                             | kdl::and_then([&](const auto& center) {
                                 return applyToTargets(
                                   context, nodes, "rotate", [&](auto& map) {
                                     return mdl::rotateSelection(map, center, axis, angle);
                                   });
                               });
                    });
           });
}

Result<nlohmann::json, ToolError> scale(ToolContext& context, const nlohmann::json& params)
{
  return resolveTargets(context, params)
         | kdl::and_then([&](const auto& nodes) -> Result<nlohmann::json, ToolError> {
             if (params.contains("bounds"))
             {
               return readSceneBounds(context.map, params)
                      | kdl::and_then([&](const auto& newBounds) {
                          const auto oldBounds = boundsOf(nodes);
                          return applyToTargets(
                            context, nodes, "scale", [&](auto& map) {
                              return mdl::scaleSelection(map, oldBounds, newBounds);
                            });
                        });
             }

             if (!params.contains("factors"))
             {
               return ToolError{
                 ErrorCode::InvalidParameters, "give either 'factors' or 'bounds'"};
             }

             const auto& raw = params.at("factors");
             const auto factorsResult = raw.is_number()
                                          ? Result<vm::vec3d, ToolError>{vm::vec3d{
                                              raw.get<double>(),
                                              raw.get<double>(),
                                              raw.get<double>()}}
                                          : readVec3(params, "factors");

             return factorsResult
                    | kdl::and_then(
                      [&](const auto& factors) -> Result<nlohmann::json, ToolError> {
                        if (factors.x() == 0.0 || factors.y() == 0.0 || factors.z() == 0.0)
                        {
                          return ToolError{
                            ErrorCode::InvalidParameters,
                            "'factors' must not contain zero; it would collapse the "
                            "geometry"};
                        }

                        return readCenter(params, nodes)
                               | kdl::and_then([&](const auto& center) {
                                   return applyToTargets(
                                     context, nodes, "scale", [&](auto& map) {
                                       return mdl::scaleSelection(map, center, factors);
                                     });
                                 });
                      });
           });
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
       {{"bounds", boundsSchema()}, {"material", materialSchema()}}},
      {"required", nlohmann::json::array({"bounds"})}},
    createBrush},
  Tool{
    "create_cylinder",
    "Create a cylinder inscribed in the given bounds. Pass 'thickness' to make it a "
    "hollow tube instead, which produces a ring of brushes.",
    ToolKind::Mutating,
    nlohmann::json{
      {"type", "object"},
      {"properties",
       {{"bounds", boundsSchema()},
        {"axis", axisSchema()},
        {"circle", circleSchema()},
        {"thickness",
         {{"type", "number"},
          {"exclusiveMinimum", 0},
          {"description",
           "Wall thickness. Omit for a solid cylinder."}}},
        {"material", materialSchema()}}},
      {"required", nlohmann::json::array({"bounds"})}},
    createCylinder},
  Tool{
    "create_cone",
    "Create a cone inscribed in the given bounds, tapering along the given axis.",
    ToolKind::Mutating,
    nlohmann::json{
      {"type", "object"},
      {"properties",
       {{"bounds", boundsSchema()},
        {"axis", axisSchema()},
        {"circle", circleSchema()},
        {"material", materialSchema()}}},
      {"required", nlohmann::json::array({"bounds"})}},
    createCone},
  Tool{
    "create_sphere",
    "Create a sphere inscribed in the given bounds. 'uv' builds it from stacked rings, "
    "'ico' from subdivided triangles with more even faces.",
    ToolKind::Mutating,
    nlohmann::json{
      {"type", "object"},
      {"properties",
       {{"bounds", boundsSchema()},
        {"kind",
         {{"type", "string"},
          {"enum", nlohmann::json::array({"uv", "ico"})},
          {"description", "Defaults to \"uv\"."}}},
        {"rings",
         {{"type", "integer"},
          {"minimum", 1},
          {"description", "Horizontal ring count, for kind \"uv\". Defaults to 8."}}},
        {"iterations",
         {{"type", "integer"},
          {"minimum", 0},
          {"description",
           "Subdivision count, for kind \"ico\". Defaults to 1. Each step multiplies the "
           "face count by four, so keep it low."}}},
        {"axis", axisSchema()},
        {"circle", circleSchema()},
        {"material", materialSchema()}}},
      {"required", nlohmann::json::array({"bounds"})}},
    createSphere},
  Tool{
    "create_arch",
    "Create a semicircular arch as a band of wedge brushes. It springs from the bottom "
    "of the bounds and rises to fill them; 'axis' is the direction the opening runs "
    "through, as for a tunnel.",
    ToolKind::Mutating,
    nlohmann::json{
      {"type", "object"},
      {"properties",
       {{"bounds", boundsSchema()},
        {"thickness",
         {{"type", "number"},
          {"exclusiveMinimum", 0},
          {"description", "Wall thickness of the band."}}},
        {"axis", axisSchema()},
        {"circle", circleSchema()},
        {"material", materialSchema()}}},
      {"required", nlohmann::json::array({"bounds", "thickness"})}},
    createArch},
  Tool{
    "translate",
    "Move objects by a delta. Acts on what this call has created so far, or on the "
    "editor's selection.",
    ToolKind::Mutating,
    nlohmann::json{
      {"type", "object"},
      {"properties",
       {{"delta", vec3Schema("Offset in world units, as [x, y, z].")},
        {"target", targetSchema()}}},
      {"required", nlohmann::json::array({"delta"})}},
    translate},
  Tool{
    "rotate",
    "Rotate objects around an axis, in degrees, counter-clockwise looking down that "
    "axis. Defaults to spinning about the centre of what is being rotated.",
    ToolKind::Mutating,
    nlohmann::json{
      {"type", "object"},
      {"properties",
       {{"angle", {{"type", "number"}, {"description", "Angle in degrees."}}},
        {"axis", axisSchema()},
        {"center",
         vec3Schema(
           "Point to rotate around. Defaults to the centre of the objects' bounds.")},
        {"target", targetSchema()}}},
      {"required", nlohmann::json::array({"angle"})}},
    rotate},
  Tool{
    "scale",
    "Scale objects, either by factors about a centre, or by fitting them into a target "
    "box. Give one of 'factors' or 'bounds'.",
    ToolKind::Mutating,
    nlohmann::json{
      {"type", "object"},
      {"properties",
       {{"factors",
         {{"description",
           "Scale factors as [x, y, z], or one number to scale uniformly."},
          {"oneOf",
           nlohmann::json::array(
             {nlohmann::json{{"type", "number"}},
              nlohmann::json{
                {"type", "array"},
                {"items", {{"type", "number"}}},
                {"minItems", 3},
                {"maxItems", 3}}})}}},
        {"bounds", boundsSchema()},
        {"center",
         vec3Schema(
           "Fixed point for 'factors'. Defaults to the centre of the objects' bounds.")},
        {"target", targetSchema()}}},
      {"required", nlohmann::json::array()}},
    scale},
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
