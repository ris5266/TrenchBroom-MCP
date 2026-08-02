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

class HostContext
{
public:
  virtual ~HostContext();

  virtual Result<nlohmann::json, ToolError> invokeAction(const std::string& path) = 0;
  virtual Result<nlohmann::json, ToolError> listActions(const std::string& filter) = 0;
  virtual Result<nlohmann::json, ToolError> captureViewport(
    const nlohmann::json& params) = 0;
};

struct ToolContext
{
  mdl::Map& map;

  HostContext* host = nullptr;

  std::vector<mdl::Node*> createdNodes;

  std::vector<mdl::Node*> lastOpNodes;
};

using ToolHandler =
  std::function<Result<nlohmann::json, ToolError>(ToolContext&, const nlohmann::json&)>;

enum class ToolKind
{
  ReadOnly,
  Mutating,
  Direct,
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
