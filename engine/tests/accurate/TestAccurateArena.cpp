// Drives a car on the real Rocket League walls and ceiling.
//
// This lives in its own executable because RocketSim::Init is process-wide and the
// main test suite already initializes it with the generated practice geometry. It
// only runs when the dumped soccar meshes are present (they are not redistributable).
#include <RocketSim.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>

namespace {

std::filesystem::path MeshRoot() {
    if (const char* env = std::getenv("RLS_MESH_PATH")) return env;
    return RLS_ACCURATE_MESH_PATH;
}

RocketSim::Arena* SharedArena() {
    static std::unique_ptr<RocketSim::Arena> arena = [] {
        RocketSim::Init(MeshRoot(), true);
        return std::unique_ptr<RocketSim::Arena>(RocketSim::Arena::Create(RocketSim::GameMode::SOCCAR));
    }();
    return arena.get();
}

// Drops a car onto a surface with the given orientation, holds full throttle, and
// returns the state after `ticks` simulated ticks (120 per second).
RocketSim::CarState Drive(RocketSim::Arena* arena, RocketSim::Car* car, RocketSim::Vec pos,
                          RocketSim::Vec forward, RocketSim::Vec up, int ticks) {
    RocketSim::CarState state;
    state.pos = pos;
    state.rotMat = RocketSim::RotMat(forward, up.Cross(forward), up);
    state.vel = forward * 800.f;
    state.isOnGround = true;
    car->SetState(state);
    car->controls = {};
    car->controls.throttle = 1.f;
    arena->Step(ticks);
    return car->GetState();
}

int WheelsTouching(const RocketSim::CarState& s) {
    int n = 0;
    for (bool w : s.wheelsWithContact) n += w;
    return n;
}

}  // namespace

TEST_CASE("Accurate soccar meshes load and are the real arena size", "[accurate-arena]") {
    REQUIRE(std::filesystem::is_directory(MeshRoot() / "soccar"));
    auto* arena = SharedArena();
    REQUIRE(arena != nullptr);

    // A ball dropped from the middle settles on the floor at its radius, and one fired at the
    // side wall bounces back rather than escaping: the arena is closed at |x| = 4096.
    RocketSim::BallState ball;
    ball.pos = RocketSim::Vec(0, 0, 600);
    ball.vel = RocketSim::Vec(0, 0, -1);
    arena->ball->SetState(ball);
    arena->Step(120 * 5);
    CHECK(arena->ball->GetState().pos.z > 85);
    CHECK(arena->ball->GetState().pos.z < 100);

    ball.pos = RocketSim::Vec(0, 0, 300);
    ball.vel = RocketSim::Vec(3000, 0, 0);
    arena->ball->SetState(ball);
    arena->Step(120 * 3);
    const auto after = arena->ball->GetState();
    CHECK(std::fabs(after.pos.x) < 4096 - 90);
    CHECK(after.vel.x < 0);  // came back off the wall
}

TEST_CASE("A car drives up the side wall and stays attached", "[accurate-arena]") {
    auto* arena = SharedArena();
    auto* car = arena->AddCar(RocketSim::Team::BLUE);

    // Side wall at x = +4096: car's up points into the field (-x), driving upward (+z).
    const float wallX = 4096.f - 17.f;  // 17 ≈ octane rest height above the surface
    const auto s = Drive(arena, car, RocketSim::Vec(wallX, 0, 600), RocketSim::Vec(0, 0, 1),
                         RocketSim::Vec(-1, 0, 0), 120);

    INFO("pos " << s.pos.x << ' ' << s.pos.y << ' ' << s.pos.z << " wheels " << WheelsTouching(s));
    CHECK(s.isOnGround);
    CHECK(WheelsTouching(s) == 4);
    CHECK(s.pos.x > 4096 - 40);      // still hugging the wall, not fallen inward
    CHECK(s.pos.z > 600 + 400);      // climbed at least 400 uu in one second
    CHECK(s.rotMat.up.x < -0.9f);    // still oriented with up pointing into the field
    arena->RemoveCar(car);
}

// Rocket League's sticky force is (0.5 + (1 - |up.z|)) * g toward the surface the wheels
// touch (RocketSim Car.cpp, _UpdateWheels). On a vertical wall that is 1.5 g into the wall,
// so cars drive freely. On the flat ceiling it is only 0.5 g against 1 g of gravity, so a
// car makes wheel contact, gets its flip reset, and peels off within about a second. That
// matches the real game: ceiling driving is a brief touch, not a sustained drive.

TEST_CASE("A car drives over the curved wall-to-ceiling transition onto the ceiling", "[accurate-arena]") {
    auto* arena = SharedArena();
    auto* car = arena->AddCar(RocketSim::Team::BLUE);

    RocketSim::CarState state;
    state.pos = RocketSim::Vec(4096.f - 17.f, 0, 1200);
    state.rotMat = RocketSim::RotMat(RocketSim::Vec(0, 0, 1), RocketSim::Vec(0, 1, 0), RocketSim::Vec(-1, 0, 0));
    state.vel = RocketSim::Vec(0, 0, 1500);
    state.isOnGround = true;
    car->SetState(state);
    car->controls = {};
    car->controls.throttle = 1.f;

    bool touchedCeilingInverted = false;
    float highest = 0;
    for (int tick = 0; tick < 120 * 3; ++tick) {
        arena->Step(1);
        const auto s = car->GetState();
        highest = std::max(highest, s.pos.z);
        if (s.rotMat.up.z < -0.9f && WheelsTouching(s) >= 2 && s.pos.z > 1950) touchedCeilingInverted = true;
    }
    const auto s = car->GetState();
    INFO("highest " << highest << " final pos " << s.pos.x << ' ' << s.pos.y << ' ' << s.pos.z);
    CHECK(highest > 1950);            // reached the ceiling plane (z = 2044 minus car height)
    CHECK(touchedCeilingInverted);    // wheels on the ceiling while upside down
    CHECK(s.pos.z < 1950);            // and, like the real game, gravity pulled it back off
    arena->RemoveCar(car);
}

TEST_CASE("A car placed on the ceiling touches it, then peels off like the real game", "[accurate-arena]") {
    auto* arena = SharedArena();
    auto* car = arena->AddCar(RocketSim::Team::BLUE);

    // Ceiling at z = 2044: car inverted, up = -z, driving along +x.
    const float ceilZ = 2044.f - 17.f;
    auto s = Drive(arena, car, RocketSim::Vec(-1500, 0, ceilZ), RocketSim::Vec(1, 0, 0),
                   RocketSim::Vec(0, 0, -1), 4);
    INFO("after 4 ticks: pos " << s.pos.x << ' ' << s.pos.y << ' ' << s.pos.z << " wheels " << WheelsTouching(s));
    CHECK(WheelsTouching(s) == 4);    // ceiling collision geometry is really there
    CHECK(s.isOnGround);
    CHECK(s.pos.z > 2044 - 25);

    arena->Step(120 - 4);
    s = car->GetState();
    INFO("after 1 s: pos " << s.pos.x << ' ' << s.pos.y << ' ' << s.pos.z << " wheels " << WheelsTouching(s));
    CHECK(s.pos.x > -1500 + 400);     // travelled along the ceiling before dropping
    CHECK(s.pos.z < 2044 - 40);       // net 0.5 g downward: it has left the ceiling
    CHECK(!s.isOnGround);
    arena->RemoveCar(car);
}

TEST_CASE("A car dropped in the air lands on the floor, not the ceiling", "[accurate-arena]") {
    // Sanity check that the sticky-surface behaviour above is real contact, not a
    // rigid body that simply ignores gravity.
    auto* arena = SharedArena();
    auto* car = arena->AddCar(RocketSim::Team::BLUE);
    RocketSim::CarState state;
    state.pos = RocketSim::Vec(1500, 1500, 1000);  // away from the kickoff ball
    state.vel = RocketSim::Vec(0, 0, -1);
    state.isOnGround = false;
    car->SetState(state);
    car->controls = {};
    arena->Step(120 * 3);
    const auto s = car->GetState();
    CHECK(s.pos.z < 40);
    CHECK(s.isOnGround);
    arena->RemoveCar(car);
}
