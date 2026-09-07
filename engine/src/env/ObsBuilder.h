#pragma once

// ============================================================================
// ObsBuilder.h -- what the agent gets to see.
//
// A reinforcement learning agent does not perceive Rocket League. It perceives
// a fixed-length vector of numbers that WE choose, and it can only ever learn
// behaviour that is a function of those numbers. Leave your own boost out of
// the observation and your bot can never learn to conserve boost -- not because
// it is a bad learner, but because the information is not there. That makes
// this the highest-leverage file in the project alongside RewardFunction.h.
//
// Three rules the interface is shaped to enforce rather than merely document:
//
//   1. FIXED WIDTH. Every call writes exactly ObsSize() floats. The network's
//      input layer is that wide and cannot change mid-run. ObsWriter counts for
//      you and asserts on the first step if you miscount, instead of letting
//      you find out after an hour of divergent training.
//   2. ONE FRAME. Build the whole vector through one Perspective. Mixing world
//      and mirrored coordinates produces geometry that is subtly, silently
//      wrong.
//   3. NORMALISE. Networks train badly on inputs of wildly different scale.
//      Positions are ~5000, velocities ~2300, booleans ~1. Every ObsWriter
//      push overload carries its own physical constant, so "divided by the
//      wrong thing" is structurally hard rather than merely discouraged.
// ============================================================================

#include <memory>
#include <span>
#include <string_view>

#include "../sim/GameState.h"

namespace rls {

// ---------------------------------------------------------------------------
// EnvSpec -- the shape of the match, fixed for a whole run
// ---------------------------------------------------------------------------

/// Everything an observation builder needs to compute its own width without a
/// live state. The learner calls ObsSize(spec) before any episode exists, to
/// size the network and the rollout buffers.
struct EnvSpec {
    GameMode gameMode = GameMode::Soccar;
    int teamSize = 2;
    bool spawnOpponents = true;
    int tickSkip = 8;
    int actionDelayTicks = 7;
    int actionCount = 90;

    [[nodiscard]] constexpr int CarsPerArena() const noexcept {
        return spawnOpponents ? teamSize * 2 : teamSize;
    }

    [[nodiscard]] constexpr f32 SecondsPerStep() const noexcept {
        return static_cast<f32>(tickSkip) * field::kTickTime;
    }
};

// ---------------------------------------------------------------------------
// The obsSize alignment invariant
// ---------------------------------------------------------------------------

/// Required multiple for ObsSize, given how many cars share an arena.
///
/// THE PROOF, because this buys two properties at once and both matter.
///
/// Whole arenas are assigned to worker threads in contiguous chunks, and arena
/// `a`'s rows start at float index `a * carsPerArena * obsSize`. So:
///
///   obsSize % 8 == 0  =>  obsSize * 4 bytes is a multiple of 32.
///   carsPerArena >= 2 in any run with two teams.
///   Therefore carsPerArena * obsSize * 4 is a multiple of 64,
///   so EVERY arena's row block starts on a cache-line boundary
///   and no two worker threads ever write the same line.
///
/// The premise `carsPerArena >= 2` FAILS in solo-drill mode
/// (spawnOpponents = false, teamSize = 1), which is a legal and useful
/// configuration for practising mechanics. Rather than forbid odd car counts,
/// we raise the alignment to 16 there, which restores the multiple of 64 with
/// carsPerArena == 1.
///
/// The second property is the one that is easy to lose. Because we pad
/// obsSize ITSELF and not the per-row stride, `stride == obsSize` stays true.
/// That keeps the batch a plain contiguous [B, obsSize] tensor: torch::from_blob
/// needs no explicit strides, and the host-to-device transfer is a single
/// cudaMemcpy rather than a strided one. Padding each ROW up to 64 bytes would
/// buy the same false-sharing guarantee and then quietly make the tensor
/// non-contiguous, so the first matmul triggers an ATen copy of the whole
/// ~1.7 MB batch on every single forward pass.
///
/// A second premise that is easy to leave implicit: the BASE allocation of the
/// observation buffer must itself be 64-byte aligned, or no block is. The
/// rollout buffer is allocated with that alignment and the runtime validator
/// checks it.
[[nodiscard]] constexpr int RequiredObsAlignment(int carsPerArena) noexcept {
    return (carsPerArena % 2 == 0) ? 8 : 16;
}

/// Rounds a natural feature count up to a legal ObsSize. Every built-in builder
/// ends its ObsSize with a call to this, and the environment re-checks the
/// result at registration.
[[nodiscard]] constexpr int AlignObsSize(int naturalSize, int carsPerArena) noexcept {
    return RoundUpToMultiple(naturalSize, RequiredObsAlignment(carsPerArena));
}

// ---------------------------------------------------------------------------
// ObsWriter -- a bounds-checked, self-normalising cursor
// ---------------------------------------------------------------------------

/// Writes an observation into a caller-owned span, one typed push at a time.
///
/// This class is teaching by API, which is worth more than a comment. The two
/// most common observation bugs in a hand-written RL codebase are:
///
///   (a) a raw 5120.0 sitting next to a 0.3, because someone divided a position
///       by CAR_MAX_SPEED instead of by the arena extent. Four orders of
///       magnitude of scale mismatch, which will dominate the first layer's
///       gradients and drown everything else out. No error, just training that
///       never gets going.
///   (b) adding a feature and forgetting to update ObsSize, so the last few
///       floats of every row are stale data from the previous step -- or, worse,
///       the write runs past the row and corrupts the next agent's observation.
///
/// (a) is prevented because there is no "divide by whatever you like" overload:
/// PushPosition divides by arena extents, PushCarVelocity by CAR_MAX_SPEED,
/// PushBallVelocity by BALL_MAX_SPEED. (b) is caught by Finish(), which asserts
/// the cursor landed exactly on ObsSize and zero-fills the alignment padding.
///
/// The ORANGE mirror is folded into the stored reciprocals at construction, so
/// mirroring costs zero extra instructions in the write loop: `signX` and
/// `signY` are already baked into `invX` and `invY`.
class ObsWriter {
public:
    /// `target` must be exactly ObsSize floats. `perspective` is the observing
    /// agent's frame -- pass Perspective::For(self.team) and never mix.
    ObsWriter(std::span<f32> target, Perspective perspective) noexcept
        : out(target), sign(perspective.sign) {}

    // --- raw ---

    /// Escape hatch for a value that is already dimensionless. If you find
    /// yourself dividing before calling this, the divisor belongs in a new
    /// overload instead.
    void PushRaw(f32 value) noexcept {
        RLS_ASSERT(cursor < out.size(), "ObsWriter overflow: ObsSize() is too small");
        out[cursor++] = value;
    }

    void PushBool(bool value) noexcept { PushRaw(value ? 1.0f : 0.0f); }

    /// A flag out of a CarFlag bitfield, in one masked test.
    void PushFlag(u32 flags, u32 mask) noexcept { PushBool((flags & mask) != 0u); }

    /// A value in [0, 1] that is already normalised, clamped for safety. Use for
    /// things like boost fraction where an out-of-range value would mean a bug
    /// upstream and should not be allowed to poison the batch.
    void PushUnit(f32 value) noexcept { PushRaw(Clamp(value, 0.0f, 1.0f)); }

    // --- normalising vector pushes ---

    /// A world position. x by arena half-width, y by arena half-length, z by
    /// arena height. Mirrored for ORANGE.
    void PushPosition(const Vec3& v) noexcept {
        PushRaw(v.x * field::kInvExtentX * sign);
        PushRaw(v.y * field::kInvExtentY * sign);
        PushRaw(v.z * field::kInvArenaHeight);
    }

    /// A car's linear velocity, by CAR_MAX_SPEED.
    void PushCarVelocity(const Vec3& v) noexcept {
        PushScaledMirrored(v, field::kInvCarMaxSpeed);
    }

    /// The ball's linear velocity, by BALL_MAX_SPEED. The ball can move much
    /// faster than a car, so using the car constant here would clip every hard
    /// shot to a saturated 1.0 and make the network unable to tell a rocket from
    /// a rolling pass.
    void PushBallVelocity(const Vec3& v) noexcept {
        PushScaledMirrored(v, field::kInvBallMaxSpeed);
    }

    /// A car's angular velocity, by CAR_MAX_ANG_SPEED (5.5 rad/s).
    void PushAngularVelocity(const Vec3& v) noexcept {
        PushScaledMirrored(v, field::kInvCarMaxAngSpeed);
    }

    /// The ball's angular velocity, by BALL_MAX_ANG_SPEED (6.0 rad/s).
    void PushBallAngularVelocity(const Vec3& v) noexcept {
        PushScaledMirrored(v, field::kInvBallMaxAngSpeed);
    }

    /// A vector that is already unit length -- a basis vector, a normalised
    /// direction. Mirrored but not scaled.
    void PushUnitVector(const Vec3& v) noexcept { PushScaledMirrored(v, 1.0f); }

    /// An orientation, as forward and up only -- SIX floats, not nine.
    ///
    /// `right` is the cross product of the other two, so shipping it spends
    /// three of the network's inputs on a value it can compute exactly. Those
    /// three inputs are better spent on something the network cannot derive.
    void PushOrientation(const Mat3& m) noexcept {
        PushUnitVector(m.forward);
        PushUnitVector(m.up);
    }

    /// A distance in uu, expressed as a fraction of the field diagonal and
    /// clamped to [0, 1].
    void PushDistance(f32 distanceUu) noexcept {
        constexpr f32 kInvDiagonal = 1.0f / 6600.0f;  // ~sqrt(4096^2 + 5120^2)
        PushRaw(Min(distanceUu * kInvDiagonal, 1.0f));
    }

    /// Seconds, expressed as a fraction of `clampSeconds` and capped at 1.
    /// Unbounded time values (air time, demo timers) must be capped or a long
    /// airborne recovery produces an input the network has never seen at that
    /// magnitude.
    void PushSeconds(f32 seconds, f32 clampSeconds) noexcept {
        PushRaw(Min(seconds, clampSeconds) / clampSeconds);
    }

    /// Boost, 0..100, as 0..1.
    void PushBoost(f32 boost) noexcept { PushUnit(boost * field::kInvBoostMax); }

    // --- finishing ---

    [[nodiscard]] usize Written() const noexcept { return cursor; }

    /// Zero-fills any alignment padding and asserts the row was filled exactly.
    ///
    /// The zero-fill is not cosmetic: leaving the padding uninitialised means
    /// the tensor contains whatever the previous step left there, which is a
    /// non-determinism source that is agony to track down and which makes two
    /// runs from the same seed diverge.
    void Finish(int naturalSize) noexcept {
        RLS_ASSERT(cursor == static_cast<usize>(naturalSize),
                   "ObsBuilder wrote a different number of floats than ObsSize() promised -- "
                   "you almost certainly added a feature and forgot to update ObsSize()");
        (void)naturalSize;
        while (cursor < out.size()) out[cursor++] = 0.0f;
    }

private:
    void PushScaledMirrored(const Vec3& v, f32 scale) noexcept {
        const f32 s = scale * sign;
        PushRaw(v.x * s);
        PushRaw(v.y * s);
        PushRaw(v.z * scale);   // z is never mirrored: up is up for everyone
    }

    std::span<f32> out;
    f32 sign = 1.0f;
    usize cursor = 0;
};

// ---------------------------------------------------------------------------
// ObsBatchView -- where the floats actually go
// ---------------------------------------------------------------------------

/// A window into the one big observation tensor, scoped to a single arena.
///
/// The point of this type: the builder writes its floats DIRECTLY into the
/// buffer that libtorch will wrap with torch::from_blob. No intermediate
/// std::vector<float>, no per-agent buffer, no gather pass. One write, in the
/// final location.
///
/// Returning a std::vector from BuildObs instead would be one heap allocation
/// per agent per step -- 50,000 allocations a second from 16 threads through
/// one allocator -- plus a copy of every row into the tensor. This is the single
/// most performance-relevant decision in the contract layer, and the reason
/// BuildBatch returns void.
struct ObsBatchView {
    f32* base = nullptr;   ///< first float of this arena's row block
    int stride = 0;        ///< floats between rows; EQUALS obsSize, see the invariant
    int rowCount = 0;      ///< == carsPerArena

    [[nodiscard]] std::span<f32> Row(int carIndex) const noexcept {
        RLS_ASSERT(carIndex >= 0 && carIndex < rowCount, "row index out of range");
        return std::span<f32>(base + static_cast<usize>(carIndex) * static_cast<usize>(stride),
                              static_cast<usize>(stride));
    }
};

// ---------------------------------------------------------------------------
// ObsBuilder
// ---------------------------------------------------------------------------

class ObsBuilder : public NonCopyable {
public:
    virtual ~ObsBuilder() = default;

    /// Stable identifier, written into every checkpoint.
    ///
    /// Versioned (`advanced_v1`) on purpose. Loading a checkpoint whose obs id
    /// differs from the current config must be a hard error: silently feeding a
    /// network observations laid out differently from the ones it trained on
    /// produces a bot that looks broken for no visible reason. Bump the version
    /// suffix whenever you change the layout, even if the WIDTH is unchanged --
    /// reordering two features while keeping the count identical is the subtle
    /// case that a size check alone will not catch.
    [[nodiscard]] virtual std::string_view Id() const noexcept = 0;

    /// Floats per agent. Must be a pure function of `spec` and must satisfy
    /// the alignment invariant above; the environment re-checks at registration
    /// with an error message that spells out the proof.
    [[nodiscard]] virtual int ObsSize(const EnvSpec& spec) const noexcept = 0;

    /// New episode. Clear any per-agent history (a frame stack, a running
    /// normaliser). Not called between steps.
    virtual void Reset(const GameState& initialState) noexcept { (void)initialState; }

    /// Write observations for EVERY car in this arena.
    ///
    /// ONE virtual call per arena per step, not one per agent, so that per-arena
    /// invariants -- the ball's normalised position, the pad mask, the goal
    /// positions -- can be hoisted out of the per-car loop by the implementation
    /// and the compiler can see, hoist and vectorise the whole thing. That is
    /// the entire reason the boundary is batched: the indirect call itself costs
    /// a couple of nanoseconds, but it also prevents inlining, so it is worth
    /// crossing once rather than six times.
    ///
    /// const, because an observation is a pure view of the state. If you want to
    /// mutate here, what you actually want is Reset or a new GameState field.
    ///
    /// noexcept because an exception escaping here would unwind through a worker
    /// thread mid-rollout and leave the batch tensor half-written.
    virtual void BuildBatch(const GameState& cur,
                            const GameState& prev,
                            const BoostPadLayout& pads,
                            const EnvSpec& spec,
                            const ObsBatchView& out) const noexcept = 0;

protected:
    ObsBuilder() = default;
};

// ---------------------------------------------------------------------------
// Built-ins
// ---------------------------------------------------------------------------

/// The default. AdvancedObs-style: ball, self, self-relative-to-ball, all boost
/// pads (mirrored by index), the previous action's control values, and every
/// other car both absolutely and relative to self.
///
/// FEATURE BUDGET, written out so a student can answer "what does my bot know?"
/// without running anything:
///
///   ball                     9   pos3 vel3 angVel3
///   self                    22   pos3 vel3 angVel3 fwd3 up3
///                                boost onGround hasFlipOrJump hasFlipReset
///                                demoed supersonic airTime
///   self relative to ball    8   relPos3 relVel3 distance facingAlignment
///   boost pads              34   one per pad, 1 = available, MIRRORED BY INDEX
///   previous action          8   the eight control floats
///   per other car           27   pos3 vel3 angVel3 fwd3 up3
///                                boost onGround hasFlipOrJump demoed supersonic
///                                relPosToSelf3 relVelToSelf3 isTeammate
///
/// 1v1 (2 cars, 1 other):   9+22+8+34+8+ 27 = 108  ->  112 (aligned to 8)
/// 2v2 (4 cars, 3 others):  9+22+8+34+8+ 81 = 162  ->  168
/// 3v3 (6 cars, 5 others):  9+22+8+34+8+135 = 216  ->  216
/// solo drill (1 car):      9+22+8+34+8+  0 =  81  ->   96 (aligned to 16)
///
/// Check the false-sharing rule on each: 2*112*4 = 896 = 14 lines;
/// 4*168*4 = 2688 = 42 lines; 6*216*4 = 5184 = 81 lines; 1*96*4 = 384 = 6 lines.
///
/// A NOTE ON THE PREVIOUS ACTION, because it is a real design choice. We feed
/// the eight CONTROL FLOATS, not a 90-wide one-hot of the action index. The
/// lookup table is injective, so the one-hot carries exactly the same
/// information in 11x the space -- and it is worse than merely wasteful: a
/// one-hot destroys the similarity structure. It makes "throttle 1, steer 0.5"
/// and "throttle 1, steer 1" orthogonal to the network when they should be near
/// neighbours, so every generalisation between similar actions has to be learned
/// from scratch. At 3v3 the one-hot version would spend 90 of 306 floats -- 29%
/// of the whole vector -- doing that.
///
/// Feeding the previous action at all is standard practice and matters more here
/// than usual: with action delay on, the previous action is STILL EXECUTING when
/// the next one is chosen, so it is genuinely part of the current situation.
class AdvancedObs;

/// A much smaller vector: ball and self only, no pads, no other cars. Trains a
/// visibly improving 1v1 bot in minutes rather than hours, which is the right
/// first experiment. It cannot learn anything about opponents or boost routing,
/// and that limitation is the lesson.
class BasicObs;

/// Creates a built-in observation builder by id, or nullptr for an unknown id.
/// The catalog IPC message enumerates the same ids, so the GUI never hardcodes
/// a list.
[[nodiscard]] std::unique_ptr<ObsBuilder> MakeObsBuilder(std::string_view id);

}  // namespace rls
