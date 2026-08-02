#include "mcp/QtHostContext.h"

#include <QAction>
#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QString>

#include "mdl/Map.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/WorldNode.h"
#include "mdl/Map_Selection.h"
#include "mdl/PushSelection.h"
#include "mdl/Selection.h"

#include "ui/Action.h"
#include "ui/ActionManager.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionMenu.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapView2D.h"
#include "ui/MapView3D.h"
#include "ui/MapViewBase.h"
#include "ui/MapWindow.h"

#include "gl/Camera.h"

#include "vm/bbox.h"
#include "vm/vec.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "kd/overload.h"

#include <fmt/format.h>

#include <nlohmann/json.hpp>

namespace tb::mcp
{
namespace
{

void collectActions(
  const ui::ActionManager& actionManager, const std::function<void(const ui::Action&)>& f)
{
  const auto visitor = kdl::overload(
    [](const ui::MenuSeparator&) {},
    [&](const ui::MenuAction& menuAction) { f(menuAction.action); },
    [](auto&& thisLambda, const ui::Menu& menu) { menu.visitEntries(thisLambda); });

  actionManager.visitMainMenu(visitor);
  actionManager.visitToolBar(visitor);
  actionManager.visitMapViewActions([&](const auto& action) { f(action); });
}

std::string pathOf(const ui::Action& action)
{
  return action.preference().path.generic_string();
}

const ui::Action* findAction(
  const ui::ActionManager& actionManager, const std::string& path)
{
  const ui::Action* found = nullptr;
  collectActions(actionManager, [&](const auto& action) {
    if (!found && pathOf(action) == path)
    {
      found = &action;
    }
  });
  return found;
}

std::optional<vm::bbox3d> contentBounds(const mdl::Map& map)
{
  auto bounds = std::optional<vm::bbox3d>{};
  const auto add = [&](const mdl::Node& node) {
    bounds = bounds ? vm::merge(*bounds, node.logicalBounds()) : node.logicalBounds();
  };

  for (const auto* layerNode : map.worldNode().allLayers())
  {
    for (const auto* child : layerNode->children())
    {
      child->accept(kdl::overload(
        [](const mdl::WorldNode&) {},
        [](const mdl::LayerNode&) {},
        [&](const mdl::GroupNode& node) { add(node); },
        [&](const mdl::EntityNode& node) { add(node); },
        [&](const mdl::BrushNode& node) { add(node); },
        [&](const mdl::PatchNode& node) { add(node); }));
    }
  }
  return bounds;
}

ui::MapViewBase* findView(ui::MapWindow& mapWindow, const std::string& view)
{
  const auto views = mapWindow.findChildren<ui::MapViewBase*>();

  for (auto* candidate : views)
  {
    const auto is3d = dynamic_cast<ui::MapView3D*>(candidate) != nullptr;
    if (view == "3d")
    {
      if (is3d)
      {
        return candidate;
      }
      continue;
    }
    if (is3d)
    {
      continue;
    }

    const auto direction = vm::abs(candidate->camera().direction());
    const auto wanted = view == "top"     ? vm::vec3f{0, 0, 1}
                        : view == "front" ? vm::vec3f{0, 1, 0}
                                          : vm::vec3f{1, 0, 0};
    if (vm::dot(direction, wanted) > 0.9f)
    {
      return candidate;
    }
  }
  return nullptr;
}

} // namespace

QtHostContext::QtHostContext(ui::AppController& appController, ui::MapWindow& mapWindow)
  : m_appController{appController}
  , m_mapWindow{mapWindow}
{
}

ui::ActionExecutionContext QtHostContext::executionContext() const
{
  auto* mapView = m_mapWindow.findChild<ui::MapViewBase*>();
  for (auto* candidate : m_mapWindow.findChildren<ui::MapViewBase*>())
  {
    if (candidate->hasFocus())
    {
      mapView = candidate;
      break;
    }
  }
  return ui::ActionExecutionContext{m_appController, &m_mapWindow, mapView};
}

Result<nlohmann::json, ToolError> QtHostContext::invokeAction(const std::string& path)
{
  const auto* action = findAction(m_appController.actionManager(), path);
  if (!action)
  {
    return ToolError{
      ErrorCode::InvalidParameters,
      fmt::format("no action at '{}'; use list_actions to see the available ones", path)};
  }

  auto context = executionContext();

  if (!action->enabled(context))
  {
    return ToolError{
      ErrorCode::OperationFailed,
      fmt::format(
        "'{}' is not available right now; it usually depends on what is selected", path)};
  }

  action->execute(context);
  return nlohmann::json{{"invoked", path}};
}

Result<nlohmann::json, ToolError> QtHostContext::listActions(const std::string& filter)
{
  auto actions = nlohmann::json::array();
  auto context = executionContext();

  collectActions(m_appController.actionManager(), [&](const auto& action) {
    const auto path = pathOf(action);
    const auto label = action.label().toStdString();
    if (
      !filter.empty() && path.find(filter) == std::string::npos
      && label.find(filter) == std::string::npos)
    {
      return;
    }

    actions.push_back(nlohmann::json{
      {"path", path}, {"label", label}, {"enabled", action.enabled(context)}});
  });

  return nlohmann::json{{"count", actions.size()}, {"actions", std::move(actions)}};
}

Result<nlohmann::json, ToolError> QtHostContext::captureViewport(
  const nlohmann::json& params)
{
  const auto view = params.value("view", std::string{"top"});
  const auto fit = params.value("fit", std::string{"map"});
  const auto width = params.value("width", 768);

  auto* mapView = findView(m_mapWindow, view);
  if (!mapView)
  {
    return ToolError{
      ErrorCode::OperationFailed,
      fmt::format(
        "the '{}' view is not open; switch TrenchBroom to a layout that shows it, or "
        "capture a view that is visible",
        view)};
  }

  auto& map = m_mapWindow.document().map();

  if (fit == "map" || fit == "selection")
  {
    const auto content = contentBounds(map);
    const auto target = fit == "selection" && map.selectionBounds()
                          ? *map.selectionBounds()
                          : content.value_or(vm::bbox3d{512.0});

    if (dynamic_cast<ui::MapView3D*>(mapView))
    {
      const auto pushSelection = mdl::PushSelection{map};
      if (fit == "map")
      {
        mdl::selectAllNodes(map);
      }
      if (map.selection().hasAny())
      {
        mapView->focusCameraOnSelection(false);
      }
    }
    else
    {
      auto& camera = mapView->camera();
      const auto center = vm::vec3f{target.center()};
      const auto diff = center - camera.position();
      const auto delta =
        vm::dot(diff, camera.up()) * camera.up()
        + vm::dot(diff, camera.right()) * camera.right();
      camera.moveTo(camera.position() + delta);

      const auto size = vm::vec3f{target.size()};
      const auto acrossX = std::abs(vm::dot(size, camera.right()));
      const auto acrossY = std::abs(vm::dot(size, camera.up()));
      const auto& viewport = camera.viewport();

      if (acrossX > 0.0f && acrossY > 0.0f && viewport.width > 0 && viewport.height > 0)
      {
        const auto zoom = 0.9f
                          * std::min(
                            float(viewport.width) / acrossX,
                            float(viewport.height) / acrossY);
        camera.setZoom(zoom);
      }
    }
  }

  mapView->repaint();

  auto image = mapView->grabFramebuffer();
  if (image.isNull())
  {
    return ToolError{ErrorCode::OperationFailed, "the view produced no image"};
  }

  if (width > 0 && image.width() != width)
  {
    image = image.scaledToWidth(width, Qt::SmoothTransformation);
  }

  auto bytes = QByteArray{};
  auto buffer = QBuffer{&bytes};
  buffer.open(QIODevice::WriteOnly);
  if (!image.save(&buffer, "PNG"))
  {
    return ToolError{ErrorCode::OperationFailed, "could not encode the image as PNG"};
  }

  return nlohmann::json{
    {"view", view},
    {"format", "png"},
    {"width", image.width()},
    {"height", image.height()},
    {"data", bytes.toBase64().toStdString()}};
}

}
