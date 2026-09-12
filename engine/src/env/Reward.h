#pragma once
#include "sim/GameState.h"
#include "learner/Config.h"
namespace rls {
/// Reward terms pulled out of JSON once per decision, not once per car.
/// nlohmann lookups on the OpenMP step path were a documented hot-path tax.
struct RewardWeights {
    float velocityToBall = 0, faceBall = 0, ballToGoal = 0, saveBoost = 0;
    float airTime = 0, touch = 0, boostPickup = 0, goal = 0;
    static RewardWeights From(const Json& config) {
        return {
            config.at("velocityToBall").get<float>(), config.at("faceBall").get<float>(),
            config.at("ballToGoal").get<float>(), config.at("saveBoost").get<float>(),
            config.at("airTime").get<float>(), config.at("touch").get<float>(),
            config.at("boostPickup").get<float>(), config.at("goal").get<float>()};
    }
};
/// Dense shaping rates are integrated over time; events pay once per decision.
inline float Reward(const GameState& state, int slot, const RewardWeights& weights) {
    const auto& car = state.cars[slot];
    const float sign = Perspective::For(car.team).sign;
    const Vec3 toBall = (state.ball.pos - car.pos).Normalized();
    float reward = state.deltaTime * (
        weights.velocityToBall * car.vel.Dot(toBall) * field::kInvCarMaxSpeed
        + weights.faceBall * car.forward.Dot(toBall)
        + weights.ballToGoal * state.ball.vel.y * sign * field::kInvBallMaxSpeed
        + weights.saveBoost * car.boost * field::kInvBoostMax
        + weights.airTime * ((car.flags & CarFlag::kOnGround) ? 0.f : 1.f));
    if (car.flags & CarFlag::kTouchedBall) reward += weights.touch;
    reward += weights.boostPickup * car.boostPickedUp * field::kInvBoostMax;
    if (state.GoalScored()) reward += weights.goal * (state.GoalScoredBy() == car.team ? 1.f : -1.f);
    return reward;
}
inline float Reward(const GameState& state, int slot, const Json& config) {
    return Reward(state, slot, RewardWeights::From(config));
}
}
