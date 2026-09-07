#include "sim/PracticeArena.h"
#include <RocketSim.h>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>

TEST_CASE("RocketSim loads practice geometry and supports cars and ball", "[practice-physics]") {
    // Keep generated fixtures inside the build tree. RocketSim initialization is
    // process-wide; this is the only test that initializes the physics backend.
    const auto path = std::filesystem::current_path() / "practice-physics-fixture";
    std::string error;
    REQUIRE(rls::WritePracticeArena(path, error));
    RocketSim::Init(path, true);
    std::unique_ptr<RocketSim::Arena> arena(
        RocketSim::Arena::Create(RocketSim::GameMode::SOCCAR));
    auto* car = arena->AddCar(RocketSim::Team::BLUE);

    RocketSim::CarState carState;
    carState.pos = RocketSim::Vec(1000, 0, 200);
    car->SetState(carState);
    RocketSim::BallState ballState;
    ballState.pos = RocketSim::Vec(0, 0, 500);
    // SetState only wakes the rigid body when velocity is nonzero.
    // Activate it so this test exercises gravity and floor collisions.
    ballState.vel = RocketSim::Vec(0, 0, -1);
    arena->ball->SetState(ballState);
    arena->Step(120 * 10);

    const auto restingBall = arena->ball->GetState();
    const auto restingCar = car->GetState();
    CHECK(std::isfinite(restingBall.pos.z));
    CHECK(restingBall.pos.z > 80);
    CHECK(restingBall.pos.z < 110);
    CHECK(restingCar.pos.z > 10);
    CHECK(restingCar.pos.z < 40);

    // A shot must pass through the goal opening and fire a scoring callback.
    int goals = 0;
    arena->SetGoalScoreCallback(
        [&goals](RocketSim::Arena*, RocketSim::Team team, void*) {
            CHECK(team == RocketSim::Team::BLUE);
            ++goals;
        });
    ballState.pos = RocketSim::Vec(0, 4900, 100);
    ballState.vel = RocketSim::Vec(0, 1500, 0);
    arena->ball->SetState(ballState);
    arena->Step(60);
    CHECK(goals > 0);
}
