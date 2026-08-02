#include "mcp/JsonUtils.h"

#include <fmt/format.h>

namespace tb::mcp
{
namespace
{

Result<vm::vec3d, ToolError> readVec3Value(
  const nlohmann::json& value, const std::string& key)
{
  if (!value.is_array() || value.size() != 3)
  {
    return ToolError{
      ErrorCode::InvalidParameters,
      fmt::format("'{}' must be an array of 3 numbers", key)};
  }

  for (const auto& component : value)
  {
    if (!component.is_number())
    {
      return ToolError{
        ErrorCode::InvalidParameters,
        fmt::format("'{}' must contain only numbers", key)};
    }
  }

  return vm::vec3d{
    value[0].get<double>(), value[1].get<double>(), value[2].get<double>()};
}

}

Result<vm::vec3d, ToolError> readVec3(const nlohmann::json& params, const std::string& key)
{
  if (!params.contains(key))
  {
    return ToolError{ErrorCode::InvalidParameters, fmt::format("missing '{}'", key)};
  }
  return readVec3Value(params.at(key), key);
}

Result<vm::bbox3d, ToolError> readBounds(
  const nlohmann::json& params, const std::string& key)
{
  if (!params.contains(key))
  {
    return ToolError{ErrorCode::InvalidParameters, fmt::format("missing '{}'", key)};
  }

  const auto& value = params.at(key);
  if (!value.is_object() || !value.contains("min") || !value.contains("max"))
  {
    return ToolError{
      ErrorCode::InvalidParameters,
      fmt::format("'{}' must be an object with 'min' and 'max'", key)};
  }

  return readVec3Value(value.at("min"), key + ".min")
         | kdl::and_then([&](const auto& min) {
             return readVec3Value(value.at("max"), key + ".max")
                    | kdl::and_then([&](const auto& max) -> Result<vm::bbox3d, ToolError> {
                        const auto bounds = vm::bbox3d{vm::min(min, max), vm::max(min, max)};
                        if (bounds.is_empty())
                        {
                          return ToolError{
                            ErrorCode::InvalidParameters,
                            fmt::format(
                              "'{}' has zero extent along at least one axis", key)};
                        }
                        return bounds;
                      });
           });
}

std::string readString(
  const nlohmann::json& params, const std::string& key, const std::string& fallback)
{
  if (!params.contains(key))
  {
    return fallback;
  }
  const auto& value = params.at(key);
  return value.is_string() ? value.get<std::string>() : fallback;
}

nlohmann::json toJson(const vm::vec3d& value)
{
  return nlohmann::json::array({value.x(), value.y(), value.z()});
}

nlohmann::json toJson(const vm::bbox3d& value)
{
  return nlohmann::json{{"min", toJson(value.min)}, {"max", toJson(value.max)}};
}

}
