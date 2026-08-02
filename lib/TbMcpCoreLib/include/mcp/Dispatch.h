#pragma once

#include "mcp/Tool.h"

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace tb::mdl
{
class Map;
}

namespace tb::mcp
{

nlohmann::json dispatch(
  mdl::Map& map, const nlohmann::json& request, HostContext* host = nullptr);

nlohmann::json dispatch(
  mdl::Map& map, const std::string& requestText, HostContext* host = nullptr);

nlohmann::json toolSchema();

}
