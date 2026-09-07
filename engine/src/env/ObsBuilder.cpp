#include "ObsBuilder.h"
#include "ActionParser.h"

namespace rls {
namespace {
void PushCar(ObsWriter& w, const CarSnapshot& car, bool self) {
    w.PushPosition(car.pos);
    w.PushCarVelocity(car.vel);
    w.PushAngularVelocity(car.angVel);
    w.PushUnitVector(car.forward);
    w.PushUnitVector(car.up);
    w.PushBoost(car.boost);
    w.PushFlag(car.flags, CarFlag::kOnGround);
    w.PushFlag(car.flags, CarFlag::kHasFlipOrJump);
    if (self) w.PushFlag(car.flags, CarFlag::kHasFlipReset);
    w.PushFlag(car.flags, CarFlag::kIsDemoed);
    w.PushFlag(car.flags, CarFlag::kIsSupersonic);
    if (self) w.PushSeconds(car.airTime, 5.f);
}
/// Pure, allocation-free feature encoder; layout versions protect checkpoints.
class BuiltinObs final : public ObsBuilder {
public:
    explicit BuiltinObs(bool advanced) : advanced(advanced) {}
    std::string_view Id() const noexcept override { return advanced ? "advanced_v1" : "basic_v1"; }
    int NaturalSize(const EnvSpec& spec) const noexcept {
        return advanced ? 81 + 27 * (spec.CarsPerArena() - 1) : 47;
    }
    int ObsSize(const EnvSpec& spec) const noexcept override {
        return AlignObsSize(NaturalSize(spec), spec.CarsPerArena());
    }
    void BuildBatch(const GameState& cur, const GameState&, const BoostPadLayout& pads,
                    const EnvSpec& spec, const ObsBatchView& out) const noexcept override {
        for (int i = 0; i < cur.carCount; ++i) {
            const auto& self = cur.cars[i];
            ObsWriter w(out.Row(i), Perspective::For(self.team));
            w.PushPosition(cur.ball.pos);
            w.PushBallVelocity(cur.ball.vel);
            w.PushBallAngularVelocity(cur.ball.angVel);
            PushCar(w, self, true);
            const Vec3 delta = cur.ball.pos - self.pos;
            w.PushPosition(delta);
            w.PushBallVelocity(cur.ball.vel - self.vel);
            w.PushDistance(delta.Length());
            w.PushRaw(self.forward.Dot(delta.Normalized()));
            if (advanced) {
                for (int p = 0; p < kMaxBoostPads; ++p)
                    w.PushBool(p < pads.padCount && cur.pads.IsActive(MirroredPad(self.team, p)));
            }
            for (float value : ActionTable()[self.lastActionIndex]) w.PushRaw(value);
            if (advanced) {
                // Stable slot order, teammates first. Never iterate RocketSim's hash set.
                for (bool teammate : {true, false})
                    for (int j = 0; j < cur.carCount; ++j) {
                        if (j == i || (cur.cars[j].team == self.team) != teammate) continue;
                        const auto& other = cur.cars[j];
                        PushCar(w, other, false);
                        w.PushPosition(other.pos - self.pos);
                        w.PushCarVelocity(other.vel - self.vel);
                        w.PushBool(teammate);
                    }
            }
            w.Finish(NaturalSize(spec));
        }
    }
private:
    bool advanced;
};
}
std::unique_ptr<ObsBuilder> MakeObsBuilder(std::string_view id) {
    if (id == "advanced_v1") return std::make_unique<BuiltinObs>(true);
    if (id == "basic_v1") return std::make_unique<BuiltinObs>(false);
    return nullptr;
}
}
