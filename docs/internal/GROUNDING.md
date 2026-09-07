# GROUNDING — verified facts for RL Studio implementation

Everything here was read directly from vendored source or verified over the network on 2026-09-07.
DO NOT contradict this file. If you believe something here is wrong, say so in your output; do not silently deviate.

## Target machine
- Windows 11 Pro, AMD Ryzen 9 9950X3D (16C/32T), NVIDIA RTX 4080 16GB, driver 616.56, 61GB RAM.
- VS 2026 Community at `C:\Program Files\Microsoft Visual Studio\18\Community` (MSVC v14.5x, C++20 OK).
- CMake 4.4.3, Ninja, Git, Node 24, Python 3.13 + 3.14, `uv`. **Rust/rustup is NOT installed yet.**
- Rocket League (Epic) at `C:\Program Files\Epic Games\rocketleague`.
- Project root: `C:\Users\barke\Desktop\RlBot` (git repo, branch `master`).

## Pinned dependency versions

| Dep | Pin | Notes |
|---|---|---|
| libtorch | `2.14.0+cu130`, Windows, **Release** ("shared-with-deps") | URL: `https://download.pytorch.org/libtorch/cu130/libtorch-win-shared-with-deps-2.14.0%2Bcu130.zip` (HTTP 200 verified). `cu128` does NOT exist for 2.14.0. Windows Debug and Release libtorch are ABI-incompatible: build engine as Release/RelWithDebInfo only. |
| RocketSim | vendored at `engine/third_party/RocketSim` (MIT) | C++20, CMake, builds a `RocketSim` library target. |
| rlviser | schemas vendored at `engine/schemas/*.fbs` (MIT) | binary downloaded by the setup script |
| Catch2 | v3.x via CMake FetchContent | tests |
| flatbuffers | via CMake FetchContent (provides `flatc` for codegen) | RLViser wire format |

## RocketSim API — VERIFIED (namespace `RocketSim`, umbrella header `RocketSim.h`)

Global init, called ONCE before any Arena is created:

```cpp
RocketSim::Init(std::filesystem::path collisionMeshesFolder, bool silent = false);
// or RocketSim::InitFromMem(const std::map<GameMode, std::vector<FileData>>&, bool silent = false);
RocketSim::RocketSimStage RocketSim::GetStage();  // UNINITIALIZED / INITIALIZING / INITIALIZED
```

Collision meshes MUST be dumped by the user from their own Rocket League install using
ZealanL/RLArenaCollisionDumper (MIT, standalone .exe, requires Rocket League running).
The dumper writes `collision-meshes/` (hyphen). We store them at `engine/assets/collision_meshes/`
and pass that path to `Init`. They are NOT redistributable — never commit them.

`Arena` (class, `Sim/Arena/Arena.h`) — key members. Verify exact accessor spellings in the header
before use; the ones marked "confirm" are from a grep and may differ slightly:

```cpp
static Arena* Create(GameMode gameMode, const ArenaConfig& config = {}, float tickRate = 120);
Arena* Clone(bool copyCallbacks);
void Step(int ticksToSimulate = 1);
void ResetToRandomKickoff(int seed = -1);
Car* AddCar(Team team, const CarConfig& config = CAR_CONFIG_OCTANE);
bool RemoveCar(uint32_t id);  bool RemoveCar(Car* car);  Car* GetCar(uint32_t id);
Ball* ball;                                      // PUBLIC member, not a getter
void SetGoalScoreCallback(GoalScoreEventFn fn, void* userInfo = NULL);
void SetCarBumpCallback(CarBumpEventFn fn, void* userInfo = NULL);
bool IsBallProbablyGoingIn(float maxTime = 2.f, float extraMargin = 0, Team* goalTeamOut = NULL) const;
bool IsBallScored() const;
void Serialize(DataStreamOut& out) const;  static Arena* DeserializeNew(DataStreamIn& in);
float tickTime;  float GetTickRate() const;      // confirm: tickCount member name
const MutatorConfig& GetMutatorConfig();  void SetMutatorConfig(const MutatorConfig&);
// confirm: GetCars() / GetBoostPads() accessor spelling and return type
// Arena is NON-COPYABLE and NON-MOVABLE. Created with Create(), destroyed with `delete`.
```

Callback typedefs:

```cpp
typedef std::function<void(Arena* arena, Team scoringTeam, void* userInfo)> GoalScoreEventFn;
typedef std::function<void(Arena* arena, Car* bumper, Car* victim, bool isDemo, void* userInfo)> CarBumpEventFn;
```

`CarControls` (`Sim/CarControls.h`) — EXACT field order:

```cpp
struct CarControls { float throttle, steer; float pitch, yaw, roll; bool jump, boost, handbrake; void ClampFix(); };
```

`PhysState` (`Sim/PhysState/PhysState.h`):

```cpp
struct PhysState { Vec pos; RotMat rotMat; Vec vel; Vec angVel; PhysState GetInvertedY() const; };
```

`RotMat` is column-major with members `.forward`, `.right`, `.up` (each a `Vec`).
`PhysState::GetInvertedY()` multiplies vectors by (-1,-1,1). That is exactly what you want for
building the ORANGE team's mirrored observation, so both teams share one policy.

`CarState : PhysState` — fields you will actually use:
`isOnGround`, `wheelsWithContact[4]`, `hasJumped`, `hasDoubleJumped`, `hasFlipped`, `flipRelTorque`,
`jumpTime`, `flipTime`, `isFlipping`, `isJumping`, `airTime`, `airTimeSinceJump`, `boost` (0..100),
`timeSinceBoosted`, `isBoosting`, `boostingTime`, `isSupersonic`, `supersonicTime`, `handbrakeVal`,
`isAutoFlipping`, `autoFlipTimer`, `autoFlipTorqueScale`, `worldContact{hasContact, contactNormal}`,
`carContact{otherCarID, cooldownTimer}`, `isDemoed`, `demoRespawnTimer`, `ballHitInfo`, `lastControls`,
`tickCountSinceUpdate`. Helpers: `HasFlipOrJump()`, `HasFlipReset()`, `GotFlipReset()`.

`Car` (class): public `CarConfig config; Team team; uint32_t id; CarControls controls;` and
`CarState GetState(); void SetState(const CarState&); void Demolish(float); void Respawn(GameMode, int seed, float boost);`
Set inputs by assigning `car->controls = ...` BEFORE calling `arena->Step()`.

`BallState : PhysState` has `hsInfo` (heatseeker) and `dsInfo` (dropshot) sub-structs.
`Ball` has `BallState GetState(); void SetState(const BallState&);`

`BoostPad`: `BoostPadConfig config {Vec pos; bool isBig;}`,
`BoostPadState {bool isActive; float cooldown; Car* curLockedCar; uint32_t prevLockedCarID;}`,
`BoostPadState GetState() const; void SetState(const BoostPadState&);`

`BallHitInfo`: `{bool isValid; Vec relativePosOnBall, ballPos, extraHitVel; uint64_t tickCountWhenHit, tickCountWhenExtraImpulseApplied;}`
Detect a touch by comparing `ballHitInfo.tickCountWhenHit` against the arena tick recorded at the start of the step.

`GameMode` enum: `SOCCAR, HOOPS, HEATSEEKER, SNOWDAY, DROPSHOT, THE_VOID`. `Team` enum: `BLUE=0, ORANGE=1`.
Macros: `RS_OPPOSITE_TEAM(team)`, `RS_TEAM_FROM_Y(y)`.

`ArenaConfig`: `memWeightMode` (`HEAVY` about 1263KB per arena with 4 cars, `LIGHT` about 383KB).
**Use LIGHT** for large arena pools. Also `minPos/maxPos/maxAABBLen/noBallRot/useCustomBroadphase/maxObjects/useCustomBoostPads/customBoostPads`.

### RLConst values (namespace `RocketSim::RLConst`) — VERIFIED

```
ARENA_EXTENT_X = 4096, ARENA_EXTENT_Y = 5120 (excludes inner goal)
BALL_REST_Z = 93.15, CAR_SPAWN_REST_Z = 17
CAR_MAX_SPEED = 2300, BALL_MAX_SPEED = 6000, SUPERSONIC_START_SPEED = 2200
BOOST_MAX = 100, BOOST_USED_PER_SECOND = 100/3, BOOST_SPAWN_AMOUNT = 100/3
```

Normalize observations with these: positions by arena extents, car velocity by CAR_MAX_SPEED,
ball velocity by BALL_MAX_SPEED, angular velocity by 5.5 rad/s, boost by 100.
Use named constants, never magic numbers.

## RLViser wire protocol — VERIFIED (schemas vendored in `engine/schemas/`)

- Transport is **UDP**. Our engine binds `0.0.0.0:34254`; RLViser listens on `127.0.0.1:45243`.
- Framing: **8-byte big-endian length prefix**, then a FlatBuffers-serialized `rocketsim.Packet`.
- `Packet { message: Message (required) }` with
  `union Message { Connection, Quit, Speed, Paused, GameState, AddRender, RemoveRender }`.
  The union tag is the 1-based index in that exact order.
- Handshake: send `Connection{}` (empty table) after binding. RLViser replies with `Connection`.
  Then send the current `Paused` and `Speed`.
- RLViser sends `Paused`/`Speed` back when the user presses P or plus/minus, and sends a `GameState`
  back when the user drags a car or the ball, or presses R. Honor these inbound messages.
- `GameState { tick_rate: float, tick_count: ulong, game_mode: GameMode, cars: [CarInfo], ball: BallState (required), pads: [BoostPadInfo], tiles: DropshotTilesByTeam }`.
- There is no seek or scrub support. Pause and speed only.
- The rlviser README's "raw bytes / rocketsim-rs to_bytes" description is OUT OF DATE. Use the .fbs schemas.

## Reference bots — READ-ONLY inspiration, DO NOT COPY CODE

`ZealanL/GigaLearnCPP-Leak`, `ZealanL/RLGymPPO_CPP` and `ZealanL/RLGymSim_CPP` all have **NO LICENSE**
(all rights reserved), and the GigaLearn one was published after a cheating leak. We write our own code.
Their *architecture* is proven and worth mirroring conceptually:

- `AdvancedObs`-style observation, discrete **90-action lookup table**, `tick_skip = 8`
  (120Hz physics gives 15Hz decisions), `action_delay` about 7 ticks,
  shared-trunk MLP 256 to 1024 wide with ReLU and LayerNorm.
- 256 or more parallel arenas, 50k to 100k steps per iteration, learning rate about 1.5e-4,
  entropy coefficient about 0.03, zero-sum rewards.
- Known-good reward weights from a working scoring bot: AirTime 0.25, FaceBall 0.25,
  VelocityPlayerToBall 4.0, StrongTouch 60, ZeroSum(VelocityBallToGoal) 2.0, BoostPickup 10,
  SaveBoost 0.2, Bump 20 (zero-sum), Demo 80 (zero-sum), Goal 150.

## Project conventions — BINDING

- C++20. Namespace `rls` (RL Studio) for all our code. `#pragma once`. 4-space indent, no tabs.
- Files `PascalCase.h` / `PascalCase.cpp`. Types `PascalCase`, functions `PascalCase`,
  variables `camelCase`, compile-time constants `kPascalCase`, members plain `camelCase` (no `m_` prefix).
- Never `using namespace` in a header.
- Every public type gets a doc comment explaining what it is and why it exists, in plain language.
  This project's purpose is teaching, so comments that explain the reinforcement-learning concept earn their place.
- Single source of truth for configuration is the JSON Schema in `configs/schema/`.
  C++ structs and TypeScript types must match it exactly.
- Target zero warnings: `/W4` on MSVC for our own sources, not for `third_party`.
- Never commit: `engine/libtorch/`, `engine/assets/collision_meshes/`, `runs/`, `node_modules/`, build dirs.

## Bootstrapping without Rocket League assets — VERIFIED, IMPORTANT

Read directly from `RocketSim.cpp` (`Init` / `InitFromMem`) and `CollisionMeshFile.cpp`:

1. `Init()` **skips a game mode silently** if its mesh subfolder is absent (`if (!exists) continue;`).
   `InitFromMem({})` logs "No meshes, skipping" and still reaches `stage = INITIALIZED`.
   So **RocketSim initializes successfully with zero collision meshes.** Arenas then have no
   world hull: cars and the ball fall through the floor. Physics, controls and stepping still work.

2. `GameMode::THE_VOID` is documented in `GameMode.h` as "Soccar but without goals, boost pads, or
   the arena hull". It requires **no meshes at all** and is a fully valid mode. Use it for
   throughput benchmarks and for smoke-testing the whole training loop on a machine with no assets.

3. A mesh whose hash is not in the known-good set produces only `RS_WARN(...)`
   ("does not match any known soccar collision mesh"). The mesh is **still loaded and used**.
   Loading is not gated on the hash.

4. The `.cmf` format is trivial little-endian:
   `int32 numTris; int32 numVertices; Triangle[numTris] {int32 v0,v1,v2}; Vertex[numVertices] {float x,y,z};`
   Validation only requires `numTris > 0`, `numVertices > 0`, both `<= 1,000,000`, and every
   vertex index within range. Path layout is `<meshFolder>/<gamemode>/<anything>.cmf`
   where `<gamemode>` is one of the `GAMEMODE_STRS` values ("soccar", "hoops", "dropshot").

### Consequence for the product — REQUIRED FEATURE

RL Studio must ship a **Practice Arena**: a synthetic `.cmf` box generated by our own code from
published field dimensions (floor, ceiling, four walls, and simple goal recesses using
`ARENA_EXTENT_X = 4096`, `ARENA_EXTENT_Y = 5120`, ceiling about 2044). It contains **no Rocket League
geometry** and is therefore fine to ship. It exists so a new user can install RL Studio, press Train,
and immediately watch a bot learn — before going through the collision-mesh dump.

The onboarding wizard must present this as two clearly-labelled tiers:
  - **Practice Arena** (works instantly, approximate geometry, good for learning RL)
  - **Accurate Arena** (requires the user to dump meshes from their own Rocket League install;
    needed for a bot whose behavior transfers to the real game)

Generate it with an engine subcommand, `rl-engine make-practice-arena`, writing to
`engine/assets/collision_meshes/soccar/practice_box.cmf`. Never present it as real Rocket League
geometry, and make the accuracy trade-off explicit in the UI and the docs.

## Build blocker discovered and resolved: CUDA Toolkit is required at CONFIGURE time

Configuring against the `cu130` libtorch fails with:

```
CUDA_TOOLKIT_ROOT_DIR not found or specified
CMake Error at libtorch/share/cmake/Caffe2/Caffe2Config.cmake:88 (message):
  Your installed Caffe2 version uses CUDA but I cannot find the CUDA libraries.
```

`libtorch/share/cmake/Torch/TorchConfig.cmake` does `find_dependency(Caffe2)`, which does
`find_package(CUDA)`. So even though libtorch **ships every CUDA runtime DLL it needs at run time**
(`cudart64_13.dll`, `cublas64_13.dll`, `cublasLt64_13.dll`, `cudnn*64_9.dll`, `nvrtc64_130_0.dll`,
`torch_cuda.dll`), CMake still needs a real CUDA Toolkit installed to *configure*.

Resolution: install the CUDA Toolkit whose major version matches the libtorch build.
`winget install --id Nvidia.CUDA --version 13.0` (13.0 pairs with `cu130`).
It lands in `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0`.

`tools/setup.ps1` and `tools/check-env.ps1` must both detect this and, if the toolkit is absent,
either install it or fall back to the CPU libtorch build with a clear explanation. Never let a user
hit the raw Caffe2Config error.

## Build environment: MSVC must be imported before CMake

The Ninja generator needs `cl.exe` plus INCLUDE/LIB set. Visual Studio only provides these inside its
developer shell. `tools/build.ps1` solves this by running `vcvars64.bat` in a throwaway `cmd` and
copying the resulting environment into the PowerShell session. Always build through `tools/build.ps1`
rather than calling `cmake` directly, or the configure step will not find a compiler.

Verified working: VS 2026 Community, MSVC 19.51.36256.0, `cl.exe` at
`C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\HostX64\x64\cl.exe`.
CMake 4.4.3 accepts RocketSim's `cmake_minimum_required(VERSION 3.8)` without error.

## CUDA + MSVC toolset incompatibility — SOLVED, do not regress this

Three separate failures were hit and fixed while getting `find_package(Torch)` to configure.
`tools/build.ps1` now handles all three automatically. Do not "simplify" it away.

1. **CUDA Toolkit must be installed** even though libtorch ships its own CUDA runtime DLLs,
   because `TorchConfig.cmake` -> `Caffe2Config.cmake` -> `find_package(CUDA)` runs at configure time.
   Installed: CUDA 13.0 at `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0`, nvcc V13.0.88.

2. **Paths must use forward slashes.** Passing
   `-DCUDA_TOOLKIT_ROOT_DIR="C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0"` fails inside
   libtorch's vendored `Modules_CUDA_fix/upstream/FindCUDA.cmake` with
   `Syntax error ... Invalid character escape '\P'`. Use `C:/Program Files/...`.

3. **The newest MSVC is too new for nvcc.** With VS 2026's toolset 14.51, CUDA 13.0's
   `crt/host_config.h` errors: "unsupported Microsoft Visual Studio version! Only the versions
   between 2019 and 2022 (inclusive) are supported". Forcing it with `-allow-unsupported-compiler`
   gets further and then dies harder: `nvcc error : 'cudafe++' died with status 0xC0000005
   (ACCESS_VIOLATION)`. **Do not use -allow-unsupported-compiler.**
   The fix is to select a 14.4x toolset via `vcvars64.bat -vcvars_ver=14.44`.
   This machine has toolset **14.44.35207** under BOTH VS 2026 Community and VS 2022 Build Tools,
   so no extra install was needed.

Verified-good configure command (what build.ps1 now generates):

```
vcvars64.bat -vcvars_ver=14.44
cmake -S engine -B engine/build -G "Ninja Multi-Config" \
      -DCMAKE_PREFIX_PATH=C:/Users/barke/Desktop/RlBot/engine/libtorch \
      -DCUDA_TOOLKIT_ROOT_DIR="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.0" \
      -DCMAKE_CUDA_ARCHITECTURES=89
```

`CMAKE_CUDA_ARCHITECTURES=89` is Ada Lovelace, matching the RTX 4080.
Configure succeeds and pulls Catch2 v3.7.1 and FlatBuffers automatically.
**Always build via `pwsh -File tools/build.ps1`.** Calling `cmake` directly will fail.
