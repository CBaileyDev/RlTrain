#include "RocketSimBackend.h"
#include "PracticeArena.h"
#include "env/ActionParser.h"
#include <RocketSim.h>
#include <stdexcept>

namespace rls {
namespace {
Vec3 Convert(const RocketSim::Vec& v) { return {v.x, v.y, v.z}; }
}
struct RocketSimBackend::Impl {
    EnvSpec spec;
    Rng rng;
    std::unique_ptr<RocketSim::Arena> arena;
    std::vector<RocketSim::Car*> cars;
    GameState state;
    BoostPadLayout pads;
    std::array<Controls, kMaxCars> previous{};
    void Snapshot(u64 startTick) {
        state.tick = arena->tickCount;
        const auto ball = arena->ball->GetState();
        state.ball.pos = Convert(ball.pos);
        state.ball.vel = Convert(ball.vel);
        state.ball.angVel = Convert(ball.angVel);
        state.ball.forward = Convert(ball.rotMat.forward);
        state.ball.right = Convert(ball.rotMat.right);
        state.ball.up = Convert(ball.rotMat.up);
        for (int i = 0; i < state.carCount; ++i) {
            const auto car = cars[i]->GetState();
            auto& out = state.cars[i];
            out.pos = Convert(car.pos); out.vel = Convert(car.vel); out.angVel = Convert(car.angVel);
            out.forward = Convert(car.rotMat.forward); out.right = Convert(car.rotMat.right); out.up = Convert(car.rotMat.up);
            const float oldBoost = out.boost;
            out.boost = car.boost; out.airTime = car.airTime;
            out.demoRespawnTimer = car.demoRespawnTimer;
            out.carId = cars[i]->id; out.index = static_cast<CarIndex>(i);
            out.team = static_cast<Team>(cars[i]->team);
            out.flags = (car.isOnGround ? CarFlag::kOnGround : 0)
                | (car.HasFlipOrJump() ? CarFlag::kHasFlipOrJump : 0)
                | (car.HasFlipReset() ? CarFlag::kHasFlipReset : 0)
                | (car.isDemoed ? CarFlag::kIsDemoed : 0)
                | (car.isSupersonic ? CarFlag::kIsSupersonic : 0);
            out.boostPickedUp = Max(0.f, car.boost - oldBoost);
            if (TouchedThisStep(car.ballHitInfo.isValid, car.ballHitInfo.tickCountWhenHit, startTick)) {
                out.flags |= CarFlag::kTouchedBall;
                out.lastBallTouchTick = car.ballHitInfo.tickCountWhenHit;
                state.ball.lastTouchTick = out.lastBallTouchTick;
                state.ball.lastTouchCarId = out.carId;
                state.ball.lastTouchCarIndex = out.index;
                state.ball.lastTouchTeam = out.team;
            }
        }
        state.pads.activeMask = 0;
        const auto& boostPads = arena->GetBoostPads();
        for (int i = 0; i < pads.padCount; ++i) {
            const auto pad = boostPads[i]->GetState();
            if (pad.isActive) state.pads.activeMask |= 1ull << i;
            state.pads.cooldown[i] = pad.cooldown;
        }
    }
};
void RocketSimBackend::Initialize(const std::filesystem::path& meshes, bool practice) {
    if (practice) {
        if (std::filesystem::is_directory(meshes / "soccar"))
            for (const auto& entry : std::filesystem::directory_iterator(meshes / "soccar"))
                if (entry.path().extension() == ".cmf" && entry.path().filename() != "practice_arena.cmf")
                    throw std::runtime_error("Practice directory contains other collision meshes; keep accurate geometry separate.");
        std::string error;
        if (!std::filesystem::exists(meshes / "soccar/practice_arena.cmf") && !WritePracticeArena(meshes, error))
            throw std::runtime_error(error);
    }
    bool found = false;
    if (std::filesystem::is_directory(meshes / "soccar"))
        for (const auto& entry : std::filesystem::directory_iterator(meshes / "soccar"))
            found |= entry.is_regular_file() && entry.path().extension() == ".cmf";
    if (!found) throw std::runtime_error("No soccar collision meshes found. Select Practice Arena or supply dumped meshes.");
    RocketSim::Init(meshes, true);
}
RocketSimBackend::RocketSimBackend(EnvSpec spec, u64 seed) : impl(std::make_unique<Impl>()) {
    impl->spec = spec; impl->rng.Reseed(seed);
    RocketSim::ArenaConfig config;
    // HEAVY is RocketSim's default: larger Bullet pools and a tighter broadphase
    // grid, slightly faster ticks at ~1.3 MB/arena. LIGHT was for memory-bound
    // hosts; this workbench already caps host rollouts at 2 GiB and typical
    // arena counts fit in a few hundred MB either way.
    config.memWeightMode = RocketSim::ArenaMemWeightMode::HEAVY;
    impl->arena.reset(RocketSim::Arena::Create(RocketSim::GameMode::SOCCAR, config));
    for (int i = 0; i < spec.CarsPerArena(); ++i)
        impl->cars.push_back(impl->arena->AddCar(i < spec.teamSize ? RocketSim::Team::BLUE : RocketSim::Team::ORANGE));
    impl->arena->SetGoalScoreCallback([this](RocketSim::Arena*, RocketSim::Team team, void*) {
        if (!impl->state.GoalScored()) {
            impl->state.goalScoredTeam = static_cast<i8>(team);
            ++impl->state.score[static_cast<int>(team)];
        }
    });
    const auto& pads = impl->arena->GetBoostPads();
    impl->pads.padCount = static_cast<int>(pads.size());
    for (int i = 0; i < impl->pads.padCount; ++i) {
        impl->pads.positions[i] = Convert(pads[i]->config.pos);
        impl->pads.isBig[i] = pads[i]->config.isBig;
    }
    Reset();
}
RocketSimBackend::~RocketSimBackend() = default;
void RocketSimBackend::Reset() {
    const auto score = impl->state.score;
    impl->arena->ResetToRandomKickoff(static_cast<int>(impl->rng.NextIndex(0x7fffffff)));
    impl->state = {};
    impl->state.score = score;
    impl->state.carCount = static_cast<u8>(impl->cars.size());
    impl->state.teamSize = static_cast<u8>(impl->spec.teamSize);
    impl->state.padCount = static_cast<u8>(impl->pads.padCount);
    impl->previous = {};
    impl->Snapshot(impl->arena->tickCount + 1);
    // Index 8 is the all-zero controller input in discrete90_v1.
    for (int i = 0; i < impl->state.carCount; ++i) impl->state.cars[i].lastActionIndex = 8;
}
void RocketSimBackend::Step(std::span<const int64_t> actions) {
    const auto start = impl->arena->tickCount;
    impl->state.goalScoredTeam = -1;
    for (int phase = 0; phase < 2; ++phase) {
        const int ticks = phase == 0 ? impl->spec.actionDelayTicks : impl->spec.tickSkip - impl->spec.actionDelayTicks;
        if (!ticks) continue;
        for (int i = 0; i < impl->state.carCount; ++i) {
            const auto& c = phase == 0 ? impl->previous[i] : ActionTable().at(static_cast<size_t>(actions[i]));
            auto& controls = impl->cars[i]->controls;
            controls.throttle = c[0]; controls.steer = c[1]; controls.pitch = c[2];
            controls.yaw = c[3]; controls.roll = c[4]; controls.jump = c[5] != 0;
            controls.boost = c[6] != 0; controls.handbrake = c[7] != 0;
        }
        impl->arena->Step(ticks);
    }
    for (int i = 0; i < impl->state.carCount; ++i) {
        impl->previous[i] = ActionTable().at(static_cast<size_t>(actions[i]));
        impl->state.cars[i].lastActionIndex = static_cast<u8>(actions[i]);
    }
    ++impl->state.episodeStep;
    impl->state.deltaTime = impl->spec.SecondsPerStep();
    impl->state.episodeTime += impl->state.deltaTime;
    impl->Snapshot(start);
}
const GameState& RocketSimBackend::State() const { return impl->state; }
const BoostPadLayout& RocketSimBackend::Pads() const { return impl->pads; }
}
