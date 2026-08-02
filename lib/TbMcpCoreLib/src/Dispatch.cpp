#include "mcp/Dispatch.h"

#include "mcp/Tool.h"

#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/PushSelection.h"
#include "mdl/Transaction.h"

#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <variant>

namespace tb::mcp
{
namespace
{

nlohmann::json errorResponse(const ErrorCode code, const std::string& message)
{
  return nlohmann::json{
    {"ok", false}, {"error", {{"code", toString(code)}, {"message", message}}}};
}

nlohmann::json errorResponse(const std::variant<ToolError>& error)
{
  const auto& toolError = std::get<ToolError>(error);
  return errorResponse(toolError.code, toolError.message);
}

nlohmann::json runMutating(
  mdl::Map& map, const Tool& tool, const nlohmann::json& params, const std::string& name)
{
  auto context = ToolContext{map, {}};
  auto transaction = mdl::Transaction{map, name};

  auto result = tool.handler(context, params);
  if (result.is_error())
  {
    transaction.cancel();
    return errorResponse(std::move(result).error());
  }

  if (!context.createdNodes.empty())
  {
    mdl::deselectAll(map);
    mdl::selectNodes(map, context.createdNodes);
  }

  if (!transaction.commit())
  {
    return errorResponse(ErrorCode::OperationFailed, "the map rejected the transaction");
  }

  return nlohmann::json{{"ok", true}, {"result", std::move(result).value()}};
}

nlohmann::json runReadOnly(mdl::Map& map, const Tool& tool, const nlohmann::json& params)
{
  auto context = ToolContext{map, {}};
  const auto pushSelection = mdl::PushSelection{map};

  auto result = tool.handler(context, params);
  return result.is_error()
           ? errorResponse(std::move(result).error())
           : nlohmann::json{{"ok", true}, {"result", std::move(result).value()}};
}

}

nlohmann::json dispatch(mdl::Map& map, const nlohmann::json& request)
{
  auto response = [&]() -> nlohmann::json {
    if (!request.is_object())
    {
      return errorResponse(ErrorCode::InvalidRequest, "request must be a JSON object");
    }
    if (!request.contains("tool") || !request.at("tool").is_string())
    {
      return errorResponse(ErrorCode::InvalidRequest, "missing 'tool'");
    }

    const auto toolName = request.at("tool").get<std::string>();
    const auto* tool = findTool(toolName);
    if (!tool)
    {
      return errorResponse(
        ErrorCode::UnknownTool, fmt::format("unknown tool '{}'", toolName));
    }

    const auto params = request.contains("params") && request.at("params").is_object()
                          ? request.at("params")
                          : nlohmann::json::object();

    if (tool->kind == ToolKind::ReadOnly)
    {
      return runReadOnly(map, *tool, params);
    }

    const auto name = params.contains("name") && params.at("name").is_string()
                        ? fmt::format("MCP: {}", params.at("name").get<std::string>())
                        : fmt::format("MCP: {}", toolName);

    return runMutating(map, *tool, params, name);
  }();

  if (request.is_object() && request.contains("id"))
  {
    response["id"] = request.at("id");
  }
  return response;
}

nlohmann::json dispatch(mdl::Map& map, const std::string& requestText)
{
  auto request = nlohmann::json{};
  try
  {
    request = nlohmann::json::parse(requestText);
  }
  catch (const nlohmann::json::parse_error& e)
  {
    return errorResponse(ErrorCode::InvalidRequest, fmt::format("invalid JSON: {}", e.what()));
  }
  return dispatch(map, request);
}

nlohmann::json toolSchema()
{
  auto result = nlohmann::json::array();
  for (const auto& tool : tools())
  {
    result.push_back(nlohmann::json{
      {"name", tool.name},
      {"description", tool.description},
      {"readOnly", tool.kind == ToolKind::ReadOnly},
      {"inputSchema", tool.paramsSchema}});
  }
  return result;
}

}
