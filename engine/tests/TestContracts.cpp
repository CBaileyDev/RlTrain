#include "env/ObsBuilder.h"

#include <catch2/catch_test_macros.hpp>
#include <array>

TEST_CASE("Team perspective is an involution and keeps up unchanged", "[contracts]") {
    const rls::Vec3 point{100, -200, 300};
    const auto orange = rls::Perspective::For(rls::Team::Orange);
    CHECK(orange.Apply(point) == rls::Vec3{-100, 200, 300});
    CHECK(orange.Apply(orange.Apply(point)) == point);
    CHECK(rls::Perspective::For(rls::Team::Blue).Apply(point) == point);
}

TEST_CASE("Pad mirroring maps each pad back to itself", "[contracts]") {
    for (int i = 0; i < rls::kMaxBoostPads; ++i) {
        const int mirrored = rls::MirroredPad(rls::Team::Orange, i);
        REQUIRE(mirrored >= 0);
        REQUIRE(mirrored < rls::kMaxBoostPads);
        CHECK(rls::MirroredPad(rls::Team::Orange, mirrored) == i);
    }
}

TEST_CASE("Observation writer normalizes and clears reused padding", "[contracts]") {
    std::array<float, 8> row;
    row.fill(99.0f);
    rls::ObsWriter writer(row, rls::Perspective::For(rls::Team::Orange));
    writer.PushPosition({rls::field::kExtentX, rls::field::kExtentY, 0});
    writer.PushBoost(50);
    writer.Finish(4);
    CHECK(row[0] == -1.0f);
    CHECK(row[1] == -1.0f);
    CHECK(row[2] == 0.0f);
    CHECK(row[3] == 0.5f);
    for (int i = 4; i < 8; ++i) CHECK(row[i] == 0.0f);
}

TEST_CASE("Seeded random streams replay exactly and floats exclude one", "[contracts]") {
    rls::Rng first(42), second(42);
    for (int i = 0; i < 1000; ++i) {
        CHECK(first.NextU64() == second.NextU64());
        const float value = first.NextFloat();
        CHECK(value == second.NextFloat());
        CHECK(value >= 0.0f);
        CHECK(value < 1.0f);
    }
}
