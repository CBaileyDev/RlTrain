#pragma once

// ============================================================================
// GameState.h -- the boundary of the project.
//
// Above this file everything is plain data: observation builders, reward
// functions, terminal conditions, state setters and the viewer stream all read
// `rls::GameState` and nothing else. Below it live RocketSim, Bullet Physics
// and the collision meshes.
//
// Exactly one translation unit in the whole engine includes <RocketSim.h>:
// sim/RocketSimBackend.cpp. That buys three concrete things:
//
//   1. TESTABILITY. You can unit-test a reward function by filling in a
//      GameState by hand -- no Arena, no collision meshes, no RocketSim::Init,
//      no 200 ms of startup. The trickiest logic in the environment (action
//      delay timing, terminal-condition combination, episode reset) is tested
//      against a scripted FakeSimBackend in microseconds.
//   2. UPGRADE SAFETY. Swapping or upgrading RocketSim touches one .cpp.
//   3. LAYOUT CONTROL. The struct is packed for OUR access pattern, not
//      RocketSim's. See the Vec3 comment for the concrete 33% we save.
//
// GameState is a trivially-copyable, fixed-size aggregate. No pointers, no
// std::vector, no std::string. Two live inside each environment for the whole
// run (current and previous) and are overwritten in place every step, so a
// training step never touches the heap.
// ============================================================================

#include <array>
#include <type_traits>

#include "../util/Common.h"

namespace rls {

// ---------------------------------------------------------------------------
// Fixed capacities
//
// Compile-time bounds are what let GameState be a flat struct with no heap.
// Raise them and recompile; nothing else has to change.
// ---------------------------------------------------------------------------

/// 4v4 is the largest Rocket League playlist. Competitive tops out at 3v3, so
/// this leaves headroom at a cost of 8 unused CarSnapshots in a 1v1 run.
inline constexpr int kMaxTeamSize = 4;
inline constexpr int kMaxCars     = kMaxTeamSize * 2;  // 8

/// Soccar: 6 big pads then 28 small pads.
/// VERIFIED against Arena.cpp:512-532 -- the arena builds `_boostPads` as
/// indices [0,6) from RLConst::BoostPads::LOCS_BIG_SOCCAR followed by [6,34)
/// from LOCS_SMALL_SOCCAR, in table order. Pad index is therefore a stable
/// identity: safe to use as an array index, safe to send over the wire, and
/// the same pad in every arena and every run.
inline constexpr int kMaxBoostPads = 34;
inline constexpr int kBigBoostPads = 6;

/// Discrete bump/demo/goal events recorded per step. A step is 8 physics ticks;
/// more than 16 distinct events in 66 ms is not physically reachable with 8
/// cars, and the snapshot builder clamps rather than overflowing.
inline constexpr int kMaxStepEvents = 16;

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

/// Which half a car defends. BLUE defends -Y and attacks +Y.
/// Values match RocketSim::Team (BLUE=0, ORANGE=1, verified Car.h:130-133) so
/// the adapter is a static_cast -- but this is deliberately a separate type so
/// nothing above the snapshot layer needs a RocketSim header.
enum class Team : u8 { Blue = 0, Orange = 1 };

[[nodiscard]] constexpr Team OppositeTeam(Team team) noexcept {
    return team == Team::Blue ? Team::Orange : Team::Blue;
}

/// Mirrors RocketSim::GameMode ordering exactly.
enum class GameMode : u8 {
    Soccar = 0, Hoops = 1, Heatseeker = 2, SnowDay = 3, Dropshot = 4, TheVoid = 5
};

/// Dense slot index into GameState::cars. This is NOT RocketSim's Car::id.
///
/// Slot index is assigned in AddCar order and is stable for the entire run.
/// Everything downstream is laid out by it: the rollout buffer's rows, the
/// per-agent reward array, the viewer's car list. See the correctness note on
/// GameState::cars for why this must never come from Arena::GetCars().
using CarIndex = u8;

inline constexpr CarIndex kNoCar = 0xFF;

// ---------------------------------------------------------------------------
// Field geometry and normalisation constants
//
// Transcribed from RocketSim::RLConst with the source line noted. They are
// duplicated (not included) so this header stays RocketSim-free.
//
// sim/ConstantsBridge.cpp -- the one translation unit that sees both this
// header and <RocketSim.h> -- static_asserts every constant in this namespace
// against its real RLConst counterpart. A RocketSim upgrade that changes a
// physics constant is therefore a COMPILE ERROR, not a silently rescaled
// observation feeding a network that was trained on the old scale.
//
// Two constants below are deliberately NOT bridged and say so. Do not "fix"
// that by inventing an RLConst name for them; there isn't one.
// ---------------------------------------------------------------------------

namespace field {

// --- bridged: every one of these is static_asserted in ConstantsBridge.cpp ---

inline constexpr f32 kExtentX        = 4096.0f;    // RLConst.h:14  ARENA_EXTENT_X
inline constexpr f32 kExtentY        = 5120.0f;    // RLConst.h:15  ARENA_EXTENT_Y (excl. inner goal)
inline constexpr f32 kArenaHeight    = 2048.0f;    // RLConst.h:16  ARENA_HEIGHT
inline constexpr f32 kGoalThresholdY = 5124.25f;   // RLConst.h:121 SOCCAR_GOAL_SCORE_BASE_THRESHOLD_Y
inline constexpr f32 kBallRestZ      = 93.15f;     // RLConst.h:43  BALL_REST_Z
inline constexpr f32 kBallRadius     = 91.25f;     // RLConst.h:117 BALL_COLLISION_RADIUS_SOCCAR
inline constexpr f32 kCarSpawnRestZ  = 17.0f;      // RLConst.h:141 CAR_SPAWN_REST_Z
inline constexpr f32 kCarMaxSpeed    = 2300.0f;    // RLConst.h:53  CAR_MAX_SPEED
inline constexpr f32 kBallMaxSpeed   = 6000.0f;    // RLConst.h:54  BALL_MAX_SPEED
inline constexpr f32 kSupersonicSpeed= 2200.0f;    // RLConst.h:69  SUPERSONIC_START_SPEED
inline constexpr f32 kCarMaxAngSpeed = 5.5f;       // RLConst.h:66  CAR_MAX_ANG_SPEED
inline constexpr f32 kBallMaxAngSpeed= 6.0f;       // RLConst.h:44  BALL_MAX_ANG_SPEED
inline constexpr f32 kBoostMax       = 100.0f;     // RLConst.h:56  BOOST_MAX
inline constexpr f32 kBoostPerSecond = 100.0f / 3.0f;  // RLConst.h:57 BOOST_USED_PER_SECOND
inline constexpr f32 kBoostSpawn     = 100.0f / 3.0f;  // RLConst.h:61 BOOST_SPAWN_AMOUNT
inline constexpr f32 kBigPadBoost    = 100.0f;     // RLConst.h:266 BoostPads::BOOST_AMOUNT_BIG
inline constexpr f32 kSmallPadBoost  = 12.0f;      // RLConst.h:267 BoostPads::BOOST_AMOUNT_SMALL
inline constexpr f32 kDemoRespawnTime= 3.0f;       // RLConst.h:146 DEMO_RESPAWN_TIME

// --- NOT bridged: ours, chosen by us, no RLConst counterpart exists ---

/// Height of the point we treat as "the middle of the goal" when a reward talks
/// about aiming. RLConst has no soccar goal-centre constant. (RLConst.h:174
/// defines Heatseeker::TARGET_Z = 320, but that is a heatseeker targeting
/// constant for a different game mode and binding to it would be a lie.)
///
/// 321.4 is roughly the vertical centre of the real goal mouth. It is a PROJECT
/// CONSTANT, deliberately not asserted against anything. If you change it, you
/// change what "aim at the net" means and every checkpoint trained on the old
/// value is subtly mis-aimed.
inline constexpr f32 kGoalCentreZ = 321.4f;

/// Playable ceiling used for clamping a position into [0,1]. We normalise by
/// kArenaHeight (2048, bridged) rather than by the "2044" figure that floats
/// around community code -- 2044 appears nowhere in RLConst and cannot be
/// asserted. If you want the exact playable ceiling for a reward, use this and
/// know that it is a definition, not a measurement.
inline constexpr f32 kCeilingZ = kArenaHeight;

/// Physics tick rate. Fixed by RocketSim; the config records it rather than
/// letting you change it, because RocketSim only reproduces Rocket League's
/// physics at 120 Hz.
inline constexpr f32 kTickRate = 120.0f;
inline constexpr f32 kTickTime = 1.0f / kTickRate;

/// The centre of the goal a team is ATTACKING, in world coordinates.
/// Reward functions that mean "toward the net" should call this instead of
/// writing a literal, so the sign is right for both teams by construction.
[[nodiscard]] constexpr f32 AttackingGoalY(Team team) noexcept {
    return team == Team::Blue ? kGoalThresholdY : -kGoalThresholdY;
}

[[nodiscard]] constexpr f32 DefendingGoalY(Team team) noexcept {
    return AttackingGoalY(OppositeTeam(team));
}

// --- reciprocals, precomputed ---
//
// The observation writer divides ~200 values per agent per step. Multiplying by
// a precomputed reciprocal instead is exact enough (these are powers-of-two-ish
// constants folded at compile time) and removes a divide from the inner loop.

inline constexpr f32 kInvExtentX      = 1.0f / kExtentX;
inline constexpr f32 kInvExtentY      = 1.0f / kExtentY;
inline constexpr f32 kInvArenaHeight  = 1.0f / kArenaHeight;
inline constexpr f32 kInvCarMaxSpeed  = 1.0f / kCarMaxSpeed;
inline constexpr f32 kInvBallMaxSpeed = 1.0f / kBallMaxSpeed;
inline constexpr f32 kInvCarMaxAngSpeed  = 1.0f / kCarMaxAngSpeed;
inline constexpr f32 kInvBallMaxAngSpeed = 1.0f / kBallMaxAngSpeed;
inline constexpr f32 kInvBoostMax     = 1.0f / kBoostMax;

}  // namespace field

// ---------------------------------------------------------------------------
// Vec3
// ---------------------------------------------------------------------------

/// A packed 3-float vector. Exactly 12 bytes.
///
/// Deliberately NOT RocketSim::Vec, and this is a correctness decision as much
/// as a size one. VERIFIED at MathTypes.h:7-20 and 62-152:
///   * RocketSim::Vec is RS_ALIGN_16 with a hidden fourth float `_w`, so
///     sizeof(Vec) == 16 and RotMat is 48 bytes.
///   * `_w` PARTICIPATES in Dot() (line 62-64), LengthSq() (line 36-38),
///     operator==() (line 146-152) and operator-() (line 142-144).
///
/// That second point is the real hazard: `a.Dot(b)` on RocketSim types is not a
/// three-component dot product. It happens to agree while `_w` stays zero, and
/// it will produce a discrepancy nobody can find on the day something sets it.
/// Our snapshot layer never round-trips through RocketSim::Vec, and every dot
/// product a reward function writes is three components by construction.
///
/// The size matters too: a GameState built from RocketSim::Vec would be about
/// 33% larger for no arithmetic benefit, because we READ these far more often
/// than we compute with them.
struct Vec3 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(f32 x, f32 y, f32 z) noexcept : x(x), y(y), z(z) {}

    [[nodiscard]] constexpr Vec3 operator+(const Vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    [[nodiscard]] constexpr Vec3 operator-(const Vec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    [[nodiscard]] constexpr Vec3 operator*(f32 s)         const noexcept { return {x * s, y * s, z * s}; }
    [[nodiscard]] constexpr Vec3 operator-()              const noexcept { return {-x, -y, -z}; }

    constexpr Vec3& operator+=(const Vec3& o) noexcept { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr Vec3& operator-=(const Vec3& o) noexcept { x -= o.x; y -= o.y; z -= o.z; return *this; }

    [[nodiscard]] constexpr bool operator==(const Vec3&) const noexcept = default;

    /// Three-component dot product. Unlike RocketSim::Vec::Dot, which is four.
    [[nodiscard]] constexpr f32 Dot(const Vec3& o) const noexcept { return x * o.x + y * o.y + z * o.z; }
    [[nodiscard]] constexpr f32 LengthSq()          const noexcept { return Dot(*this); }
    [[nodiscard]] f32 Length()                      const noexcept { return std::sqrt(LengthSq()); }
    [[nodiscard]] f32 Dist(const Vec3& o)           const noexcept { return (*this - o).Length(); }
    [[nodiscard]] constexpr f32 DistSq(const Vec3& o) const noexcept { return (*this - o).LengthSq(); }

    [[nodiscard]] constexpr Vec3 Cross(const Vec3& o) const noexcept {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }

    /// Unit vector, or the zero vector when this is (near) zero.
    ///
    /// Safe by construction, and that is not politeness. A NaN produced here
    /// lands in the observation tensor, propagates through the forward pass,
    /// produces a NaN gradient and destroys every weight in the network on the
    /// next optimiser step -- with no error message. A reward function must
    /// never have to remember to guard.
    [[nodiscard]] Vec3 Normalized() const noexcept {
        const f32 lenSq = LengthSq();
        if (lenSq <= 1.0e-12f) return Vec3{};
        return *this * (1.0f / std::sqrt(lenSq));
    }

    /// Horizontal length, ignoring height. Useful for "distance across the
    /// field" rewards that should not care whether the ball is in the air.
    [[nodiscard]] f32 Length2D() const noexcept { return std::sqrt(x * x + y * y); }
};

static_assert(sizeof(Vec3) == 12, "Vec3 must stay packed; GameState density depends on it");

/// Orientation as three orthonormal basis vectors, column-major, matching
/// RocketSim::RotMat's member names exactly (MathTypes.h:169-171).
///
/// A basis, not Euler angles, and that is a machine-learning decision. Euler
/// angles wrap at +/-pi, and that discontinuity is something a smooth function
/// approximator fundamentally cannot represent -- a network fed yaw will always
/// be confused at the seam. Basis vectors are continuous everywhere and let a
/// reward hand the network dot products directly.
struct Mat3 {
    Vec3 forward{1.0f, 0.0f, 0.0f};
    Vec3 right  {0.0f, 1.0f, 0.0f};
    Vec3 up     {0.0f, 0.0f, 1.0f};

    /// Express a world-space vector in this body's local frame.
    [[nodiscard]] constexpr Vec3 ToLocal(const Vec3& worldVec) const noexcept {
        return {worldVec.Dot(forward), worldVec.Dot(right), worldVec.Dot(up)};
    }
};

static_assert(sizeof(Mat3) == 36, "Mat3 is three packed Vec3, not RocketSim's 48-byte RotMat");

// ---------------------------------------------------------------------------
// Perspective -- the ORANGE mirror
// ---------------------------------------------------------------------------

/// Rotates the world 180 degrees about the vertical axis for ORANGE agents.
///
/// WHY THIS IS THE MOST IMPORTANT IDEA IN THIS FILE.
/// Rocket League is symmetric. A BLUE car attacking +Y and an ORANGE car
/// attacking -Y face the same problem. If both teams are shown raw world
/// coordinates, one network has to learn "score at +Y" and "score at -Y" as two
/// unrelated skills, which halves the value of every sample. Mirror the world
/// for ORANGE and every agent believes it is attacking +Y: one policy, one set
/// of weights, twice the data, and self-play becomes a policy playing literally
/// itself.
///
/// The transform is (x, y, z) -> (-x, -y, z), which is exactly what
/// RocketSim::PhysState::GetInvertedY does (PhysState.h:19-21). It applies
/// identically to positions, velocities, angular velocities AND all three basis
/// vectors of the rotation matrix. Angular velocity needs no special handling
/// even though it is a pseudovector: a 180-degree rotation about Z is a PROPER
/// rotation (determinant +1) and pseudovectors transform like vectors under
/// proper rotations.
///
/// Stored as a signed multiplier rather than materialised as a mirrored copy of
/// GameState. A mirrored copy would be ~1.5 KB per ORANGE agent per step --
/// about 1.2 GB/s across 16 threads -- to save a multiply the compiler folds
/// into the store anyway.
struct Perspective {
    f32 sign = 1.0f;  ///< +1 for BLUE, -1 for ORANGE

    [[nodiscard]] static constexpr Perspective For(Team team) noexcept {
        return Perspective{team == Team::Blue ? 1.0f : -1.0f};
    }

    [[nodiscard]] constexpr Vec3 Apply(const Vec3& v) const noexcept {
        return {v.x * sign, v.y * sign, v.z};
    }

    [[nodiscard]] constexpr Mat3 Apply(const Mat3& m) const noexcept {
        return {Apply(m.forward), Apply(m.right), Apply(m.up)};
    }
};

/// Maps each boost pad index to the pad at its mirrored (-x, -y, z) position.
///
/// THIS TABLE FIXES A REAL AND OTHERWISE INVISIBLE BUG. The Perspective flip
/// above mirrors vectors, but a boost pad is identified by an INDEX, and an
/// index has no sign to flip. Pad i is a fixed world position. So an ORANGE
/// agent reading `pads.IsActive(i)` in raw index order sees the mirror image of
/// what a BLUE agent sees -- 34 of the observation's inputs contradict the
/// symmetry that every other input was carefully built to preserve. The shared
/// policy is fed inconsistent geometry for a large block of its input and the
/// effective sample value of that block is halved. Nothing errors.
///
/// VERIFIED constructible from the real tables:
///   * Big pads, RLConst.h:305-312. Under (x,y) -> (-x,-y):
///       0 (-3584,0)     <-> 1 (3584,0)
///       2 (-3072,4096)  <-> 5 (3072,-4096)
///       3 (3072,4096)   <-> 4 (-3072,-4096)
///     i.e. [1, 0, 5, 4, 3, 2].
///   * Small pads, RLConst.h:274-303, are listed in an order that is exactly
///     antisymmetric: small entry j maps to small entry 27 - j. Since small
///     pads occupy global indices [6, 34), global index i maps to 39 - i.
///
/// Generated as constexpr here rather than typed out, and unit-tested for being
/// a valid involution (kMirroredPadIndex[kMirroredPadIndex[i]] == i for all i).
inline constexpr std::array<u8, kMaxBoostPads> kMirroredPadIndex = [] {
    std::array<u8, kMaxBoostPads> table{};
    // Big pads: an explicit permutation, derived above from LOCS_BIG_SOCCAR.
    constexpr u8 bigMirror[kBigBoostPads] = {1, 0, 5, 4, 3, 2};
    for (int i = 0; i < kBigBoostPads; ++i) {
        table[static_cast<usize>(i)] = bigMirror[i];
    }
    // Small pads: the table is antisymmetric, so j <-> 27 - j within the small
    // block, which is 39 - i in global index space.
    for (int i = kBigBoostPads; i < kMaxBoostPads; ++i) {
        table[static_cast<usize>(i)] = static_cast<u8>(39 - i);
    }
    return table;
}();

/// Index of the pad an agent on `team` should read when it wants "the pad that
/// is at position p from my point of view". BLUE reads pads directly; ORANGE
/// reads through the mirror.
[[nodiscard]] constexpr int MirroredPad(Team team, int padIndex) noexcept {
    return team == Team::Blue ? padIndex
                              : static_cast<int>(kMirroredPadIndex[static_cast<usize>(padIndex)]);
}

// ---------------------------------------------------------------------------
// Car flags
// ---------------------------------------------------------------------------

/// Every boolean fact about a car, packed into one 32-bit word.
///
/// A bitfield rather than nineteen separate bools, for two reasons. First, the
/// whole set is a single 4-byte load: a reward that wants three of these facts
/// pays for one cache line instead of three. Second, and more importantly,
/// `HasFlipOrJump` and `HasFlipReset` are NOT stored booleans in RocketSim --
/// VERIFIED at Car.cpp:307-315, both are computed predicates over four other
/// fields. Evaluating them exactly once during snapshot fill means ten reward
/// functions cannot each re-derive them slightly differently.
///
/// Bits 0-15 are STATE: things that are true right now.
/// Bits 16-31 are EVENTS: things that happened during the step just simulated,
/// true for exactly one step. The state/event distinction is the single most
/// important one in reward design -- "distance to ball" is a property of a
/// state, "you touched the ball" is a property of a transition, and conflating
/// them produces rewards that fire on the wrong step.
namespace CarFlag {

inline constexpr u32 kNone            = 0u;

// --- state ---
inline constexpr u32 kOnGround        = 1u << 0;   ///< CarState::isOnGround (3+ wheels)
inline constexpr u32 kOnWall          = 1u << 1;   ///< world contact with a near-horizontal normal
inline constexpr u32 kHasJumped       = 1u << 2;   ///< CarState::hasJumped
inline constexpr u32 kHasDoubleJumped = 1u << 3;   ///< CarState::hasDoubleJumped (flips do NOT count)
inline constexpr u32 kHasFlipped      = 1u << 4;   ///< CarState::hasFlipped
inline constexpr u32 kIsFlipping      = 1u << 5;   ///< CarState::isFlipping
inline constexpr u32 kIsJumping       = 1u << 6;   ///< CarState::isJumping
inline constexpr u32 kHasFlipOrJump   = 1u << 7;   ///< CarState::HasFlipOrJump(), evaluated once
inline constexpr u32 kHasFlipReset    = 1u << 8;   ///< CarState::HasFlipReset(), evaluated once
inline constexpr u32 kIsDemoed        = 1u << 9;   ///< CarState::isDemoed
inline constexpr u32 kIsSupersonic    = 1u << 10;  ///< CarState::isSupersonic (can demo on contact)
inline constexpr u32 kIsBoosting      = 1u << 11;  ///< CarState::isBoosting
inline constexpr u32 kHasWorldContact = 1u << 12;  ///< CarState::worldContact.hasContact

// --- events, true for one step only ---
inline constexpr u32 kTouchedBall     = 1u << 16;
inline constexpr u32 kPickedUpPad     = 1u << 17;
inline constexpr u32 kPickedUpBigPad  = 1u << 18;
inline constexpr u32 kBumpedOpponent  = 1u << 19;
inline constexpr u32 kDemoedOpponent  = 1u << 20;
inline constexpr u32 kWasBumped       = 1u << 21;
inline constexpr u32 kWasDemoed       = 1u << 22;
inline constexpr u32 kJustRespawned   = 1u << 23;
inline constexpr u32 kGotFlipReset    = 1u << 24;  ///< gained a flip reset during this step

/// Mask of every event bit. The snapshot builder clears these before filling.
inline constexpr u32 kAllEvents =
    kTouchedBall | kPickedUpPad | kPickedUpBigPad | kBumpedOpponent |
    kDemoedOpponent | kWasBumped | kWasDemoed | kJustRespawned | kGotFlipReset;

}  // namespace CarFlag

// ---------------------------------------------------------------------------
// CarSnapshot
// ---------------------------------------------------------------------------

/// One car as of the end of a decision step. Exactly 128 bytes -- two cache
/// lines -- so eight of them is 1 KB and an arena's whole car state lands in L1
/// on the first touch.
///
/// Array-of-structs, not struct-of-arrays, and that is considered. SoA pays off
/// when you sweep one field over thousands of elements. Here the inner loop is
/// "for each of at most 8 cars, read most fields", so the entire array is 1 KB
/// read sequentially. Splitting it into twenty parallel arrays would turn one
/// sequential 1 KB read into twenty strided reads. The SoA layout DOES appear,
/// one level up, in the flat batch tensors -- which is where it belongs.
///
/// Field order is by access frequency, not by logical grouping: the six Vec3s
/// (72 bytes, read by essentially every consumer) fill the first cache line and
/// the first 8 bytes of the second.
struct CarSnapshot {
    Vec3 pos;        // offset  0   world position, uu
    Vec3 vel;        //        12   uu/s
    Vec3 angVel;     //        24   rad/s
    Vec3 forward;    //        36   rotation basis, matching RocketSim RotMat
    Vec3 right;      //        48
    Vec3 up;         //        60

    f32 boost = field::kBoostSpawn;  //  72  0..100
    f32 demoRespawnTimer = 0.0f;     //  76  seconds until respawn, 0 when alive
    f32 airTime = 0.0f;              //  80  seconds since leaving the ground
    f32 flipTime = 0.0f;             //  84  seconds into the current flip, 0 if not flipping

    u32 flags = 0;                   //  88  CarFlag bits
    u32 carId = 0;                   //  92  RocketSim Car::id; for the backend and the viewer

    /// Arena tick of this car's most recent ball touch, or ~0ULL if never.
    /// Absolute, monotonic, never resets (Arena::tickCount, Arena.h:63-64).
    u64 lastBallTouchTick = ~0ull;   //  96

    /// Ball speed change caused by this car's touch this step, uu/s. Zero when
    /// this car did not touch. Lets a StrongTouch reward distinguish a nudge
    /// from a shot with no bookkeeping of its own.
    f32 touchSpeedDelta = 0.0f;      // 104

    /// Height of the ball centre at the moment of contact, uu. Zero when no
    /// touch. An AerialTouch reward is one comparison against this.
    f32 touchHeight = 0.0f;          // 108

    /// Boost units gained this step, attributed from pad transitions (0, 12 or
    /// 100 in a normal run). See BoostPadSnapshot for why this is not a boost
    /// delta.
    f32 boostPickedUp = 0.0f;        // 112

    u32 bumpVictimCarId = 0;         // 116  car we bumped this step, 0 if none
    u32 bumpedByCarId = 0;           // 120  car that bumped us this step, 0 if none

    CarIndex index = 0;              // 124  slot in GameState::cars; the observation ordering
    Team team = Team::Blue;          // 125
    u8 lastActionIndex = 0;          // 126  index into the 90-entry action table

    /// Whether this car's transitions belong in the learner's PPO batch.
    ///
    /// THIS FIELD PREVENTS A SILENT, TOTAL BATCH CORRUPTION. With a past-self
    /// opponent pool enabled, some cars are driven by a FROZEN checkpoint, not
    /// by the policy being trained. Their transitions are off-policy data from a
    /// different policy; feeding them to PPO's importance-sampling ratio is
    /// mathematically wrong and produces a quietly poisoned gradient with no
    /// error anywhere. The rollout writer MUST skip every slot where this is 0.
    /// See LearnerConfig::CheckpointConfig::opponentPoolSize.
    u8 isLearning = 1;               // 127
};

static_assert(sizeof(CarSnapshot) == 128,
    "CarSnapshot must stay at exactly two cache lines: 8 of them is 1 KB, which is "
    "what keeps a whole arena's car state in L1 for the obs and reward passes.");

// ---------------------------------------------------------------------------
// BallSnapshot
// ---------------------------------------------------------------------------

/// The ball. One per arena, so its size barely matters and we keep the full
/// orientation basis even though ArenaConfig::noBallRot defaults to true
/// (ArenaConfig.h:33) -- the 3D viewer wants spin and paying 36 bytes once per
/// arena to have it is free.
struct BallSnapshot {
    Vec3 pos;
    Vec3 vel;
    Vec3 angVel;
    Vec3 forward;
    Vec3 right;
    Vec3 up;

    /// Arena tick of the most recent touch by anyone, ~0ULL if untouched.
    u64 lastTouchTick = ~0ull;
    u32 lastTouchCarId = 0;             ///< 0 when nobody has touched it yet
    CarIndex lastTouchCarIndex = kNoCar;
    Team lastTouchTeam = Team::Blue;    ///< only meaningful when lastTouchCarId != 0
    u8 pad0 = 0;
    u8 pad1 = 0;
};

static_assert(sizeof(BallSnapshot) == 88,
    "6 Vec3 (72) + u64 at 72 + u32 at 80 + 4 bytes of small fields = 88, align 8.");

// ---------------------------------------------------------------------------
// Boost pads
// ---------------------------------------------------------------------------

/// Live boost pad state for a whole arena.
///
/// Pad POSITIONS are not here. They are constant for the entire run, live once
/// in BoostPadLayout, and copying 34 Vec3s (408 bytes) into every snapshot
/// 12,500 times a second would be pure waste for data that never changes.
///
/// The bitmask earns its place twice: the observation writer turns pad i into a
/// float with a shift and a mask, and "which pads were taken this step" is
/// `prevMask & ~curMask` -- one instruction instead of a loop.
struct BoostPadSnapshot {
    u64 activeMask = 0;                                ///< bit i set => pad i is available
    std::array<f32, kMaxBoostPads> cooldown{};         ///< seconds until respawn, 0 if available

    [[nodiscard]] constexpr bool IsActive(int pad) const noexcept {
        return ((activeMask >> pad) & 1ull) != 0ull;
    }
};

static_assert(sizeof(BoostPadSnapshot) == 144, "u64 + 34 floats, align 8");

/// Static pad geometry, built once per arena at startup from RocketSim's
/// tables. Handed to observation builders and rewards by const reference
/// alongside the GameState.
struct BoostPadLayout {
    std::array<Vec3, kMaxBoostPads> positions{};
    std::array<bool, kMaxBoostPads> isBig{};
    int padCount = 0;   ///< 34 in soccar, 20 in hoops, 0 in dropshot and the void
};

// ---------------------------------------------------------------------------
// Step events
// ---------------------------------------------------------------------------

enum class StepEventType : u8 { None = 0, Bump = 1, Demo = 2, Goal = 3 };

/// A discrete thing that happened during the step, recorded precisely rather
/// than flattened into a flag. Bumps and demos carry an actor and a victim that
/// a flag cannot express.
struct StepEvent {
    StepEventType type = StepEventType::None;
    Team team = Team::Blue;
    CarIndex actorIndex = kNoCar;
    CarIndex victimIndex = kNoCar;
    u8 pad0 = 0;
    u8 pad1 = 0;
    u8 pad2 = 0;
    u8 pad3 = 0;
    u64 tick = 0;
};

static_assert(sizeof(StepEvent) == 16, "");

// ---------------------------------------------------------------------------
// GameState
// ---------------------------------------------------------------------------

/// Everything the learning side of the system is allowed to see, at one
/// decision point.
///
/// A reward function or observation builder that needs something not in here is
/// telling you the snapshot layer is missing a field. Add the field. Do not
/// reach through to the Arena -- doing so silently makes that component
/// untestable, non-deterministic, or both.
struct GameState {
    // ---- clocks ----

    /// Arena::tickCount AFTER this step. VERIFIED public member at Arena.h:64,
    /// comment says "never resets".
    ///
    /// BECAUSE IT NEVER RESETS, DO NOT USE IT FOR TIMEOUTS. A terminal
    /// condition that compares `tick` against an episode-length limit will fire
    /// on the first step of every episode once the arena has been alive long
    /// enough, collapsing training in a way whose symptom (episodes of length 1)
    /// looks like a reward bug. Use `episodeStep` or `episodeTime`.
    u64 tick = 0;

    /// Decision steps since the last episode reset. This is the one that resets.
    u32 episodeStep = 0;

    /// Simulated seconds since the last reset. episodeStep * tickSkip / 120.
    f32 episodeTime = 0.0f;

    /// Simulated seconds this step represents (tickSkip / 120). Rewards that
    /// mean "per second" multiply by this, so their scale does not silently
    /// change when a student edits tickSkip.
    f32 deltaTime = 8.0f / 120.0f;

    // ---- match layout, constant for the episode ----

    GameMode gameMode = GameMode::Soccar;
    u8 carCount = 0;
    u8 teamSize = 0;      ///< per team; carCount == 2 * teamSize in a normal run
    u8 padCount = 0;
    u8 eventCount = 0;
    u32 arenaIndex = 0;   ///< which arena in the pool produced this snapshot

    // ---- contents ----

    BallSnapshot ball;
    BoostPadSnapshot pads;

    /// Cars in AddCar order. Slot index is stable for the whole run.
    ///
    /// CORRECTNESS NOTE, NOT AN OPTIMISATION NOTE -- this one is worth the
    /// paragraph. VERIFIED at Arena.h:66: `const std::unordered_set<Car*>&
    /// GetCars()`. It is an unordered_set of POINTERS, so iteration order is
    /// pointer-hash order: not insertion order, not stable between two arenas
    /// built identically, not reproducible across runs. Filling this array by
    /// iterating it would make slot 0 refer to a different car in different
    /// environments, permuting observation features between arenas and making
    /// the run both unlearnable and irreproducible -- with no error of any kind.
    ///
    /// The backend captures Car* into its own array in AddCar order at
    /// construction and never iterates GetCars() again. It also asserts, in
    /// debug builds only, that its cached array size still matches
    /// arena->GetCars().size() every step, so a future feature that removes a
    /// car mid-episode fails an assertion instead of a use-after-free.
    ///
    /// (GetBoostPads() returns a std::vector -- Arena.h:67 -- and IS safely
    /// index-ordered. See kMaxBoostPads.)
    std::array<CarSnapshot, kMaxCars> cars{};

    std::array<StepEvent, kMaxStepEvents> events{};

    // ---- match-level events from the step just simulated ----

    /// -1 when no goal, otherwise the team that SCORED.
    ///
    /// Note on the source: RocketSim's goal callback is LEVEL-triggered, not
    /// edge-triggered. VERIFIED at Arena.cpp:748-753 -- the callback fires on
    /// every tick for which IsBallScored() is true, with no edge detection, so
    /// one goal fires it up to 8 times inside a single Step(8). The backend
    /// latches into this scalar, which makes the repeated writes idempotent.
    /// If you ever route goals through the StepEvent array instead, you MUST
    /// add edge detection or you will emit 8 goal events for one goal and
    /// overflow the fixed-size buffer.
    ///
    /// The callback already reports the SCORING team: it computes
    /// RS_TEAM_FROM_Y(-ball.y), i.e. it negates the y before deriving the team,
    /// so a ball in the ORANGE net reports BLUE. Read it directly; do not
    /// invert it again.
    i8 goalScoredTeam = -1;

    u8 pad0 = 0;
    u8 pad1 = 0;
    u8 pad2 = 0;

    /// Running score this episode, indexed by Team.
    std::array<i32, 2> score{0, 0};

    // ---- accessors ----

    [[nodiscard]] const CarSnapshot& Car(CarIndex index) const noexcept {
        RLS_ASSERT(index < carCount, "car index out of range");
        return cars[index];
    }

    [[nodiscard]] bool GoalScored() const noexcept { return goalScoredTeam >= 0; }

    [[nodiscard]] Team GoalScoredBy() const noexcept {
        RLS_ASSERT(GoalScored(), "no goal was scored this step");
        return static_cast<Team>(goalScoredTeam);
    }

    /// Seconds since anyone last touched the ball, or episodeTime when nobody
    /// has yet. Terminal conditions and touch-timeout rewards use this rather
    /// than doing tick arithmetic themselves (and rather than using `tick`,
    /// which never resets).
    [[nodiscard]] f32 TimeSinceTouch() const noexcept {
        if (ball.lastTouchCarId == 0) return episodeTime;
        return static_cast<f32>(tick - ball.lastTouchTick) * field::kTickTime;
    }

    [[nodiscard]] int TeamSize(Team team) const noexcept {
        int count = 0;
        for (int i = 0; i < carCount; ++i) {
            if (cars[static_cast<usize>(i)].team == team) ++count;
        }
        return count;
    }
};

static_assert(std::is_trivially_copyable_v<GameState>,
    "GameState is memcpy'd into the previous-state buffer and into the viewer's "
    "single-slot mailbox every step. A std::vector or std::string here would break "
    "both, so the assertion is a guard rail, not a formality.");

// The exact byte size is NOT asserted, deliberately. Pinning it would turn
// every future field addition into an arithmetic puzzle, and a wrong number in
// a static_assert is a compile error while a wrong number in a design document
// is a memory budget nobody rechecks. The blocks are what matter:
//
//   header ~32 B, ball 88 B, pads 144 B, cars 8 x 128 = 1024 B,
//   events 16 x 16 = 256 B, tail ~16 B   ->  roughly 1.55 KB.
//
// Two per arena (current + previous) is about 3.1 KB. At 512 arenas that is
// ~1.6 MB of snapshot state, which fits in L3 comfortably -- and is under 1% of
// the ~196 MB that 512 RocketSim arenas occupy at ArenaMemWeightMode::LIGHT
// (383 KB each). The honest framing: the PHYSICS working set does not fit in
// cache, and our snapshot layer is a rounding error on the memory traffic.
// Making it smaller is not where throughput comes from.
//
// If you want the real number on your build, print sizeof(GameState) from the
// startup validator -- which already prints the arena working set against the
// measured L3 for exactly this reason.

// ---------------------------------------------------------------------------
// Touch detection helper
// ---------------------------------------------------------------------------

/// Did a car whose ballHitInfo says `hitTick` touch the ball during a step that
/// began at `stepStartTick`?
///
/// THE COMPARISON IS `>=`, NOT `>`, AND THE DIFFERENCE IS ~1/8 OF ALL TOUCHES.
///
/// VERIFIED: Arena.cpp:754 places `tickCount++` at the END of each iteration of
/// the Step loop, and Ball.cpp:256 stamps `ballHitInfo.tickCountWhenHit =
/// tickCount` during the collision -- i.e. BEFORE that increment. So a touch on
/// the FIRST simulated tick of a step records exactly `stepStartTick`. A strict
/// `>` silently discards it. With tickSkip = 8 that is roughly one touch in
/// eight, thrown away with no error anywhere, starving the sparsest and
/// highest-weight shaping term in the preset (StrongTouch is weighted 60) and
/// losing the goal attribution that rides on lastTouchCarIndex.
///
/// The `isValid` check is not optional: BallHitInfo::tickCountWhenHit defaults
/// to ~0ULL (BallHitInfo.h:18), which compares greater than any real tick, so
/// without it every car "touches" on the first step of the run.
[[nodiscard]] constexpr bool TouchedThisStep(bool isValid, u64 hitTick,
                                             u64 stepStartTick) noexcept {
    return isValid && hitTick >= stepStartTick;
}

}  // namespace rls
