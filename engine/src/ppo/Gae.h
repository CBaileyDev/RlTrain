#pragma once
#include <span>
#include <vector>
namespace rls {
/// Generalized advantage estimation in time-major order. nextValues already
/// contains zero for goals and endpoint values for time-limit truncations.
inline void ComputeGae(std::span<const float> rewards, std::span<const float> values,
                       std::span<const float> nextValues, std::span<const float> continues,
                       int horizon, int agents, float gamma, float lambda, std::span<float> output) {
    std::vector<float> carry(agents, 0.f);
    for (int t = horizon - 1; t >= 0; --t)
        for (int a = 0; a < agents; ++a) {
            const size_t i = static_cast<size_t>(t) * agents + a;
            carry[a] = rewards[i] + gamma * nextValues[i] - values[i] + gamma * lambda * continues[i] * carry[a];
            output[i] = carry[a];
        }
}
}
