#pragma once
#include "env/ObsBuilder.h"
#include <memory>
#include <filesystem>
#include <span>

namespace rls {
/// Owns one simulator and translates its mutable state into stable plain snapshots.
class RocketSimBackend {
public:
    static void Initialize(const std::filesystem::path& meshes, bool practice);
    RocketSimBackend(EnvSpec spec, u64 seed);
    ~RocketSimBackend();
    void Reset();
    void Step(std::span<const int64_t> actions);
    const GameState& State() const;
    const BoostPadLayout& Pads() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
