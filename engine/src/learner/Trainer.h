#pragma once
#include "Config.h"
#include <functional>
namespace rls {
/// Emits newline JSON events; control is polled only at safe rollout boundaries.
using Emit = std::function<void(const Json&)>;
using Control = std::function<Json()>;
void Train(Json config, const std::filesystem::path& run, const std::filesystem::path& checkpoint,
           const Emit& emit, const Control& control);
void Play(const std::filesystem::path& checkpoint, const std::filesystem::path& opponent,
          int matches, bool realtime, const Emit& emit, const Control& control);
void Bench(Json config, const Emit& emit);
}
