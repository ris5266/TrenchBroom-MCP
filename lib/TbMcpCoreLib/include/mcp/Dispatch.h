#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace tb::mdl
{
class Map;
}

namespace tb::mcp
{

nlohmann::json dispatch(mdl::Map& map, const nlohmann::json& request);

nlohmann::json dispatch(mdl::Map& map, const std::string& requestText);

nlohmann::json toolSchema();

}
