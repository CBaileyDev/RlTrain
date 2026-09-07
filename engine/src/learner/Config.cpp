#include "Config.h"
#include "ConfigSchema.h"
#include <fstream>
#include <stdexcept>

namespace rls {
Json DefaultConfig() {
    Json result = Json::object();
    const Json schema = Json::parse(kConfigSchema);
    for (auto& [key, rule] : schema["properties"].items()) result[key] = rule["default"];
    return result;
}
Json ValidateConfig(const Json& input) {
    if (!input.is_object()) throw std::invalid_argument("Configuration must be a JSON object");
    const Json schema = Json::parse(kConfigSchema);
    Json result = DefaultConfig();
    for (auto& [key, value] : input.items()) {
        if (!schema["properties"].contains(key)) throw std::invalid_argument("Unknown setting: " + key);
        const auto& rule = schema["properties"][key];
        const auto type = rule["type"].get<std::string>();
        bool valid = (type == "integer" && value.is_number_integer()) || (type == "number" && value.is_number())
            || (type == "string" && value.is_string()) || (type == "boolean" && value.is_boolean());
        if (!valid) throw std::invalid_argument("Wrong type for " + key);
        if (rule.contains("enum") && std::find(rule["enum"].begin(), rule["enum"].end(), value) == rule["enum"].end())
            throw std::invalid_argument("Unsupported value for " + key);
        if (value.is_number()) {
            const double n = value.get<double>();
            if (!std::isfinite(n) || (rule.contains("minimum") && n < rule["minimum"].get<double>())
                || (rule.contains("maximum") && n > rule["maximum"].get<double>()))
                throw std::invalid_argument("Value out of range for " + key);
        }
        result[key] = value;
    }
    if (result["actionDelayTicks"].get<int>() >= result["tickSkip"].get<int>())
        throw std::invalid_argument("actionDelayTicks must be smaller than tickSkip");
    const int batch = result["arenas"].get<int>() * result["teamSize"].get<int>() * 2 * result["rolloutSteps"].get<int>();
    if (result["minibatchSize"].get<int>() > batch) throw std::invalid_argument("minibatchSize exceeds rollout batch size");
    const auto spec = SpecFromConfig(result);
    const auto builder = MakeObsBuilder(result["observation"].get<std::string>());
    const auto rolloutBytes = static_cast<uint64_t>(batch) * (builder->ObsSize(spec) + 12) * sizeof(float);
    if (rolloutBytes > 2ull * 1024 * 1024 * 1024)
        throw std::invalid_argument("Rollout exceeds the 2 GiB host-buffer limit. Reduce arenas or rolloutSteps.");
    return result;
}
EnvSpec SpecFromConfig(const Json& config) {
    EnvSpec spec;
    spec.teamSize = config["teamSize"];
    spec.tickSkip = config["tickSkip"];
    spec.actionDelayTicks = config["actionDelayTicks"];
    return spec;
}
Json ReadJson(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot read " + path.string());
    Json result; stream >> result; return result;
}
void WriteJson(const std::filesystem::path& path, const Json& value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    if (!(stream << value.dump(2))) throw std::runtime_error("Cannot write " + path.string());
}
}
