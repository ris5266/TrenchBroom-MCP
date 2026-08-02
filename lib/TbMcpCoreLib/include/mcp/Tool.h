#pragma once

#include "base/Result.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace tb::mdl
{
class Map;
class Node;
}

namespace tb::mcp
{

enum class ErrorCode
{
  InvalidRequest,
  UnknownTool,
  InvalidParameters,
  OperationFailed,
};

std::string toString(ErrorCode errorCode);

struct ToolError
{
  ErrorCode code;
  std::string message;
};

struct ToolContext
{
  mdl::Map& map;

  std::vector<mdl::Node*> createdNodes;

  std::vector<mdl::Node*> lastOpNodes;
};

using ToolHandler =
  std::function<Result<nlohmann::json, ToolError>(ToolContext&, const nlohmann::json&)>;

enum class ToolKind
{
  ReadOnly,
  Mutating,
};

struct Tool
{
  std::string name;
  std::string description;
  ToolKind kind;
  nlohmann::json paramsSchema;
  ToolHandler handler;
};

const std::vector<Tool>& tools();

const Tool* findTool(const std::string& name);

}
