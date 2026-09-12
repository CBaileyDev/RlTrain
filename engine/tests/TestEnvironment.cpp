#include "env/ActionParser.h"
#include "env/ObsBuilder.h"
#include "env/Reward.h"
#include <catch2/catch_test_macros.hpp>
#include <set>
#include <vector>

TEST_CASE("Action vocabulary is bounded and unique", "[environment]") {
    const auto& table = rls::ActionTable();
    std::set<rls::Controls> unique(table.begin(), table.end());
    CHECK(unique.size() == 90);
    for (const auto& action : table) for (float value : action) {
        CHECK(value >= -1); CHECK(value <= 1);
    }
    CHECK(unique.contains(rls::Controls{}));
}
TEST_CASE("Built-in observations fill finite rows for every team size", "[environment]") {
    for (const char* id : {"advanced_v1", "basic_v1"}) {
        auto builder = rls::MakeObsBuilder(id);
        REQUIRE(builder);
        for (int teamSize = 1; teamSize <= 4; ++teamSize) {
            rls::EnvSpec spec; spec.teamSize = teamSize;
            rls::GameState state; state.carCount = static_cast<rls::u8>(spec.CarsPerArena());
            const int width = builder->ObsSize(spec);
            std::vector<float> output(width * state.carCount + 1, 12345.f);
            builder->BuildBatch(state, state, {}, spec, {output.data(), width, state.carCount});
            CHECK(output.back() == 12345.f);
            for (size_t i = 0; i + 1 < output.size(); ++i) {
                CHECK(std::isfinite(output[i])); CHECK(output[i] != 12345.f);
            }
            CHECK((width * state.carCount * sizeof(float)) % 64 == 0);
        }
    }
    CHECK_FALSE(rls::MakeObsBuilder("unknown"));
}
TEST_CASE("Configuration rejects unsafe and incompatible shapes", "[config]") {
    auto config = rls::DefaultConfig();
    CHECK(rls::ValidateConfig(config) == config);
    config["arenas"] = 0; CHECK_THROWS(rls::ValidateConfig(config));
    config = rls::DefaultConfig(); config["actionDelayTicks"] = config["tickSkip"];
    CHECK_THROWS(rls::ValidateConfig(config));
    config = rls::DefaultConfig(); config["hiddenSize"] = 32.5;
    CHECK_THROWS(rls::ValidateConfig(config));
    config = rls::DefaultConfig(); config["typo"] = true;
    CHECK_THROWS(rls::ValidateConfig(config));
}
TEST_CASE("Goal reward is signed by team and paid once per transition", "[rewards]") {
    auto config = rls::DefaultConfig();
    for (const char* key : {"velocityToBall", "faceBall", "ballToGoal", "saveBoost", "airTime", "touch", "boostPickup"}) config[key] = 0;
    rls::GameState state; state.carCount = 2; state.cars[1].team = rls::Team::Orange;
    state.goalScoredTeam = 0;
    CHECK(rls::Reward(state, 0, config) == 150);
    CHECK(rls::Reward(state, 1, config) == -150);
    state.goalScoredTeam = -1;
    CHECK(rls::Reward(state, 0, config) == 0);
}
TEST_CASE("Cached reward weights match JSON lookups", "[rewards]") {
    auto config = rls::DefaultConfig();
    rls::GameState state;
    state.carCount = 1;
    state.deltaTime = 1.f / 15.f;
    state.cars[0].vel = {0, 1000, 0};
    state.cars[0].pos = {0, 0, 17};
    state.ball.pos = {0, 1000, 93};
    state.cars[0].flags = rls::CarFlag::kTouchedBall;
    const auto weights = rls::RewardWeights::From(config);
    CHECK(rls::Reward(state, 0, weights) == rls::Reward(state, 0, config));
    config["touch"] = 9;
    CHECK(rls::Reward(state, 0, rls::RewardWeights::From(config)) == rls::Reward(state, 0, config));
    CHECK(rls::Reward(state, 0, weights) != rls::Reward(state, 0, config));
}
TEST_CASE("Masked observation encode leaves continuing arena rows unchanged", "[environment]") {
    auto builder = rls::MakeObsBuilder("basic_v1");
    rls::EnvSpec spec;
    spec.teamSize = 1;
    const int cars = spec.CarsPerArena(), width = builder->ObsSize(spec), arenas = 2;
    std::vector<float> buffer(static_cast<size_t>(arenas * cars * width), 12345.f);
    rls::GameState first, second;
    first.carCount = second.carCount = static_cast<rls::u8>(cars);
    first.ball.pos = {100, 0, 100};
    second.ball.pos = {200, 0, 100};
    builder->BuildBatch(first, first, {}, spec, {buffer.data(), width, cars});
    builder->BuildBatch(second, second, {}, spec, {buffer.data() + cars * width, width, cars});
    const auto before = buffer;
    first.ball.pos = {999, 999, 999};
    second.ball.pos = {300, 0, 100};
    const uint8_t reset[] = {0, 1};
    for (int arena = 0; arena < arenas; ++arena) {
        if (!reset[arena]) continue;
        auto& state = arena == 0 ? first : second;
        builder->BuildBatch(state, state, {}, spec, {buffer.data() + arena * cars * width, width, cars});
    }
    for (int i = 0; i < cars * width; ++i) CHECK(buffer[static_cast<size_t>(i)] == before[static_cast<size_t>(i)]);
    CHECK(buffer[static_cast<size_t>(cars * width)] != before[static_cast<size_t>(cars * width)]);
}
