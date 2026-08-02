#pragma once

#include "mcp/Tool.h"

namespace tb::ui
{
class ActionExecutionContext;
class AppController;
class MapWindow;
}

namespace tb::mcp
{

class QtHostContext : public HostContext
{
private:
  ui::AppController& m_appController;
  ui::MapWindow& m_mapWindow;

public:
  QtHostContext(ui::AppController& appController, ui::MapWindow& mapWindow);

  Result<nlohmann::json, ToolError> invokeAction(const std::string& path) override;
  Result<nlohmann::json, ToolError> listActions(const std::string& filter) override;
  Result<nlohmann::json, ToolError> captureViewport(
    const nlohmann::json& params) override;

private:
  ui::ActionExecutionContext executionContext() const;
};

}
