message(STATUS "Fetching nlohmann_json...")
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json
  GIT_TAG        v3.11.3
  SYSTEM
)

set(JSON_BuildTests OFF)
set(JSON_Install OFF)
FetchContent_MakeAvailable(nlohmann_json)
