#pragma once

// ============================================================================
// Common.h -- the only header every other RL Studio header is allowed to
// include unconditionally.
//
// Rules this file exists to enforce:
//   * No heavy includes. <vector>, <string>, <torch/torch.h>, <RocketSim.h> and
//     friends are deliberately absent. This header is pulled into the hot path
//     by everything, so it stays cheap to parse and cheap to recompile.
//   * No "using namespace" (project convention, and it is a header).
//   * Everything lives in namespace rls.
//
// Naming conventions, restated here because this is the first file a new
// contributor opens:
//   Types and functions   PascalCase
//   Variables and members camelCase, never m_ prefixed
//   Compile-time consts   kPascalCase
//   Files                 PascalCase.h / PascalCase.cpp
// ============================================================================

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace rls {

// ---------------------------------------------------------------------------
// Fixed-width aliases
//
// We spell integer sizes explicitly everywhere in the contract layer because
// several of these types are memcpy'd into a previous-state buffer, published
// to a viewer thread, and serialised onto the wire. "int" is not a wire type.
// ---------------------------------------------------------------------------

using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;

// ---------------------------------------------------------------------------
// Cache line size
//
// x86-64 is 64 bytes and both target CPUs (Zen 5, and any Intel part a student
// might use) agree. We do NOT use std::hardware_destructive_interference_size:
// it is 64 on MSVC anyway, and using it portably produces an ABI warning on GCC
// that we would then have to suppress.
//
// This constant is load-bearing. Several invariants in the config validator are
// proofs that a per-thread block starts on a multiple of it, which is what
// guarantees two worker threads never write the same cache line. False sharing
// is the failure mode that turns a 16x speedup into a 3x one while showing up in
// a profiler as nothing but "cache misses".
// ---------------------------------------------------------------------------

inline constexpr usize kCacheLineBytes = 64;

// ---------------------------------------------------------------------------
// Assertions
//
// RLS_ASSERT is compiled out in release. It is for invariants that a correct
// program can never violate -- an index inside a fixed-size array, an
// observation cursor inside its span. Use it liberally: the whole point of the
// snapshot layer is that these checks are cheap enough to leave on in every
// debug build a student ever runs.
//
// RLS_VERIFY is NEVER compiled out. It is for conditions that depend on user
// input or on the outside world (a config value, a file that must exist). A
// student who mistypes a config should get a message, not undefined behaviour
// four hours into a run.
// ---------------------------------------------------------------------------

#if defined(NDEBUG)
    #define RLS_ASSERT(cond, msg) ((void)0)
#else
    #define RLS_ASSERT(cond, msg) assert(((void)(msg), (cond)))
#endif

#define RLS_VERIFY(cond, msg)                                                 \
    do {                                                                      \
        if (!(cond)) {                                                        \
            ::rls::detail::FailVerify(#cond, (msg), __FILE__, __LINE__);      \
        }                                                                     \
    } while (false)

namespace detail {
/// Prints the failure and aborts. Defined in util/Common.cpp so this header
/// does not need <cstdio> or <exception>.
[[noreturn]] void FailVerify(const char* expression, const char* message,
                             const char* file, int line) noexcept;
}  // namespace detail

// ---------------------------------------------------------------------------
// Compiler hints
//
// Used sparingly and only where a measurement or a proof justifies them. A
// wrong RLS_LIKELY is worse than no hint at all.
// ---------------------------------------------------------------------------

#if defined(_MSC_VER)
    #define RLS_FORCE_INLINE __forceinline
    #define RLS_NEVER_INLINE __declspec(noinline)
    #define RLS_RESTRICT     __restrict
#else
    #define RLS_FORCE_INLINE inline __attribute__((always_inline))
    #define RLS_NEVER_INLINE __attribute__((noinline))
    #define RLS_RESTRICT     __restrict__
#endif

/// [[likely]] / [[unlikely]] are C++20 and MSVC 19.3x+ supports them, so we use
/// the standard spelling rather than a macro of our own.
#define RLS_LIKELY   [[likely]]
#define RLS_UNLIKELY [[unlikely]]

// ---------------------------------------------------------------------------
// Small numeric helpers
//
// These exist so hot-path code never reaches for <algorithm> (which drags in a
// large chunk of the standard library) and never writes a hand-rolled clamp
// with a subtly wrong NaN behaviour.
// ---------------------------------------------------------------------------

template <typename T>
[[nodiscard]] constexpr T Min(T a, T b) noexcept { return a < b ? a : b; }

template <typename T>
[[nodiscard]] constexpr T Max(T a, T b) noexcept { return a > b ? a : b; }

template <typename T>
[[nodiscard]] constexpr T Clamp(T value, T low, T high) noexcept {
    return value < low ? low : (value > high ? high : value);
}

/// Linear interpolation. t is not clamped -- callers that need clamping should
/// say so at the call site, because "extrapolate" is occasionally what you want
/// (a curriculum schedule that overshoots, for instance).
[[nodiscard]] constexpr f32 Lerp(f32 a, f32 b, f32 t) noexcept {
    return a + (b - a) * t;
}

/// Round `value` up to the next multiple of `multiple`. Used by the observation
/// size padding rule; see ObsBuilder.h for the proof that this removes false
/// sharing on the observation tensor.
[[nodiscard]] constexpr int RoundUpToMultiple(int value, int multiple) noexcept {
    return ((value + multiple - 1) / multiple) * multiple;
}

/// Divide, treating a zero denominator as producing zero rather than NaN.
///
/// This matters more than it looks. A single NaN written into the observation
/// tensor propagates through the whole forward pass, produces a NaN gradient,
/// and silently destroys every weight in the network on the next optimiser
/// step. There is no error message and the symptom is "my bot suddenly became
/// random after four hours". Every division in reward and observation code
/// should either use this or be provably safe.
[[nodiscard]] constexpr f32 SafeDivide(f32 numerator, f32 denominator) noexcept {
    return denominator == 0.0f ? 0.0f : numerator / denominator;
}

/// True when `value` is finite (not NaN, not +/-inf). Used by the config
/// validator and by debug-build guards around reward outputs.
[[nodiscard]] inline bool IsFinite(f32 value) noexcept {
    return value == value && value != HUGE_VALF && value != -HUGE_VALF;
}

// ---------------------------------------------------------------------------
// Non-copyable base
//
// Contract objects (reward trees, observation builders, environments) are owned
// by exactly one place and referenced everywhere else. Making the copy
// accidental-proof removes a whole class of "why are there 512 copies of my
// reward function" bugs.
// ---------------------------------------------------------------------------

class NonCopyable {
public:
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;

protected:
    NonCopyable() = default;
    ~NonCopyable() = default;
    NonCopyable(NonCopyable&&) = default;
    NonCopyable& operator=(NonCopyable&&) = default;
};

// ---------------------------------------------------------------------------
// Deterministic RNG
//
// A training run must be reproducible from its seed alone, or "did my reward
// change help?" is unanswerable -- you cannot tell a real improvement from
// luck. That requires every stochastic decision (state setting, action
// sampling on CPU, curriculum choices) to come from a generator whose stream is
// a pure function of (masterSeed, workerIndex, episodeIndex).
//
// std::mt19937 would work but is 2.5 KB of state per worker and is slow to
// seed. SplitMix64 is 8 bytes, seeds in one multiply, and passes BigCrush; for
// picking spawn positions it is more than enough.
// ---------------------------------------------------------------------------

/// Mixes a 64-bit value into a well-distributed 64-bit value. Also used
/// standalone to derive per-worker and per-episode seeds from the master seed.
[[nodiscard]] constexpr u64 SplitMix64(u64 state) noexcept {
    state += 0x9E3779B97F4A7C15ull;
    u64 z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// Per-worker, per-episode random number generator. Never shared between
/// threads: a shared generator would both contend on a cache line every draw
/// and destroy reproducibility, because thread interleaving is not
/// deterministic.
class Rng {
public:
    Rng() = default;
    explicit Rng(u64 seed) noexcept : state(SplitMix64(seed)) {}

    void Reseed(u64 seed) noexcept { state = SplitMix64(seed); }

    /// Uniform in [0, 2^64).
    [[nodiscard]] u64 NextU64() noexcept {
        state += 0x9E3779B97F4A7C15ull;
        u64 z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    /// Uniform in [0, 1). 24 bits of mantissa, which is every bit a float has.
    [[nodiscard]] f32 NextFloat() noexcept {
        return static_cast<f32>(NextU64() >> 40) * (1.0f / 16777216.0f);
    }

    /// Uniform in [low, high).
    [[nodiscard]] f32 NextFloat(f32 low, f32 high) noexcept {
        return low + NextFloat() * (high - low);
    }

    /// Uniform integer in [0, bound). Uses Lemire's multiply-shift; the modulo
    /// bias is negligible for the small bounds we use and the branchless form
    /// is worth more than perfect uniformity here.
    [[nodiscard]] u32 NextIndex(u32 bound) noexcept {
        RLS_ASSERT(bound > 0, "NextIndex(0) has no valid result");
        return static_cast<u32>((NextU64() >> 32) * static_cast<u64>(bound) >> 32);
    }

    [[nodiscard]] bool NextBool() noexcept { return (NextU64() >> 63) != 0; }

    /// Standard normal, via Box-Muller. Used by state setters that jitter a
    /// spawn position; a Gaussian keeps most samples near the intended state
    /// while still occasionally producing the rare situation you actually want
    /// the agent to encounter.
    [[nodiscard]] f32 NextGaussian() noexcept {
        // Guard against log(0), which is -inf and would produce an infinite
        // spawn coordinate.
        const f32 u1 = Max(NextFloat(), 1.0e-7f);
        const f32 u2 = NextFloat();
        return std::sqrt(-2.0f * std::log(u1)) *
               std::cos(6.2831853071795864769f * u2);
    }

private:
    u64 state = 0;
};

/// Derives the generator for one worker thread from the run's master seed.
/// Kept as a named function so every caller derives it the same way and a run
/// stays reproducible when the worker count is unchanged.
[[nodiscard]] inline Rng MakeWorkerRng(u32 masterSeed, int workerIndex) noexcept {
    return Rng(SplitMix64((static_cast<u64>(masterSeed) << 32) ^
                          (static_cast<u64>(workerIndex) + 0x1234567ull)));
}

}  // namespace rls
