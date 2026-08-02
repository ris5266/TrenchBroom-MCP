#include "mcp/Dispatch.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>

int main(int argc, char* argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: TbMcpSchemaTool <output path>\n";
    return 1;
  }

  auto stream = std::ofstream{argv[1]};
  if (!stream.good())
  {
    std::cerr << "Could not open " << argv[1] << " for writing\n";
    return 1;
  }

  stream << tb::mcp::toolSchema().dump(2) << '\n';
  return 0;
}
