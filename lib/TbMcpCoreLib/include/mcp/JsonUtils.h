#pragma once

#include "mcp/Tool.h"

#include "vm/bbox.h"
#include "vm/vec.h"

#include <nlohmann/json.hpp>

#include <string>

namespace tb::mcp
{

Result<vm::vec3d, ToolError> readVec3(
  const nlohmann::json& params, const std::string& key);

Result<vm::bbox3d, ToolError> readBounds(
  const nlohmann::json& params, const std::string& key);

std::string readString(
  const nlohmann::json& params, const std::string& key, const std::string& fallback = {});

nlohmann::json toJson(const vm::vec3d& value);
nlohmann::json toJson(const vm::bbox3d& value);

}
