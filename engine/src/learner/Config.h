#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include "env/ObsBuilder.h"

namespace rls {
using Json = nlohmann::json;
/// Validated run settings. The schema is embedded at build time from configs/schema.
Json DefaultConfig();
Json ValidateConfig(const Json& input);
EnvSpec SpecFromConfig(const Json& config);
Json ReadJson(const std::filesystem::path& path);
void WriteJson(const std::filesystem::path& path, const Json& value);
}
