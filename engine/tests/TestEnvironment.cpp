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
