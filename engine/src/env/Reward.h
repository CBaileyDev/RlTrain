#pragma once
#include "sim/GameState.h"
#include "learner/Config.h"
namespace rls {
/// Dense shaping rates are integrated over time; events pay once per decision.
inline float Reward(const GameState& state, int slot, const Json& config) {
    const auto& car = state.cars[slot];
    const float sign = Perspective::For(car.team).sign;
    const Vec3 toBall = (state.ball.pos - car.pos).Normalized();
    float reward = state.deltaTime * (
        config["velocityToBall"].get<float>() * car.vel.Dot(toBall) * field::kInvCarMaxSpeed
        + config["faceBall"].get<float>() * car.forward.Dot(toBall)
        + config["ballToGoal"].get<float>() * state.ball.vel.y * sign * field::kInvBallMaxSpeed
        + config["saveBoost"].get<float>() * car.boost * field::kInvBoostMax
        + config["airTime"].get<float>() * ((car.flags & CarFlag::kOnGround) ? 0.f : 1.f));
    if (car.flags & CarFlag::kTouchedBall) reward += config["touch"].get<float>();
    reward += config["boostPickup"].get<float>() * car.boostPickedUp * field::kInvBoostMax;
    if (state.GoalScored()) reward += config["goal"].get<float>() * (state.GoalScoredBy() == car.team ? 1.f : -1.f);
    return reward;
}
}
