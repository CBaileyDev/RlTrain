#include "ppo/Gae.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <array>
TEST_CASE("GAE terminates at goals and bootstraps time limits", "[ppo]") {
    const std::array<float, 2> rewards{1,2}, values{.5f,.6f}, continues{1,0};
    std::array<float, 2> next{.6f,0}, out{};
    rls::ComputeGae(rewards, values, next, continues, 2, 1, 1, 1, out);
    CHECK(out[0] == Catch::Approx(2.5)); CHECK(out[1] == Catch::Approx(1.4));
    next[1] = 10;
    rls::ComputeGae(rewards, values, next, continues, 2, 1, 1, 1, out);
    CHECK(out[0] == Catch::Approx(12.5)); CHECK(out[1] == Catch::Approx(11.4));
    // First step truncates: its own endpoint value is retained but the next
    // episode's advantage must not leak backward through the trace.
    const std::array<float, 2> cut{0,0};
    rls::ComputeGae(rewards, values, next, cut, 2, 1, 1, 1, out);
    CHECK(out[0] == Catch::Approx(1.1));
}
