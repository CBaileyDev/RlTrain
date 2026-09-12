# Troubleshooting

## The engine is missing

Run `pwsh -File tools/build.ps1 -Test` from the source checkout. In a portable build,
keep engine/build/bin together with rl-studio.exe. Do not copy only the app executable.

## libtorch or CUDA does not configure

Use tools/setup.ps1 to fetch dependencies. CUDA libtorch needs CUDA Toolkit 13.0 at build
time. CUDA 13.0 needs the MSVC 14.4x toolset; do not force an unsupported compiler flag.
Always build through tools/build.ps1 so compiler and toolkit paths are imported correctly.
For a CPU build, use tools/setup.ps1 -Cpu. Build Release or RelWithDebInfo, never Debug
against the release Windows libtorch package.

## CUDA is unavailable at runtime

Auto falls back to CPU. Explicit CUDA produces an error if CUDA cannot initialize.
Check your NVIDIA driver and keep all packaged DLLs together. For tiny batches CPU can
be faster because transfers and kernel launch overhead dominate.

## Charts are empty

Metrics appear only after a full rollout and PPO update. Check the status, then inspect
Settings → Engine log. A completed zero-update stop may have a checkpoint but no curves.
Browser preview cannot run the native engine; open the desktop executable.

## Most evaluation matches are draws

An untrained or weak policy may never score before the episode time limit. Draws are
reported honestly. Train longer, inspect ball touches, and compare many matches. Reward
increases do not establish playing strength or rank.

## A setting is rejected

Read the explanation under the field. actionDelayTicks must be less than tickSkip.
Minibatch size cannot exceed arenas × teamSize × 2 × rolloutSteps. Numbers must be finite
and inside schema limits. Checkpoints require compatible observation, team size, network
width and action timing for training resume.

## Out of memory or low throughput

Stop the run and reduce arenas, rolloutSteps, hiddenSize or minibatchSize. The rollout
is initially stored in host RAM and copied to the selected device for updates. Increase
CPU worker count only while throughput improves. Metrics include both simulation and
optimizer work, whereas the physics benchmark measures the configured arena pool under OpenMP.

## Wrong collisions or missing meshes

Select Practice Arena for generated geometry. Accurate Arena requires a directory with
soccar/*.cmf. Do not mix accurate files with practice_arena.cmf. The viewer is a simplified
illustration and cannot diagnose every mesh defect. Malformed third-party meshes may
cause RocketSim itself to terminate; the app reports the child exit code.

## The assistant fails

Save an API key in Settings, enter a supported Responses API model ID, and check your
OpenAI billing/model access. Network errors do not stop training. An invalid AI proposal
is rejected. Remove a saved key through Settings. Local diagnostic needs no network.

## Development preview becomes stale

Restart npm run dev and reload. The file watcher waits for writes to settle; a crashed
editor or interrupted dependency update can still leave a stale module cache.

## The app closes during training

Closing the window requests Stop & save and waits for the engine. A child that does not
respond within 30 seconds is terminated. Forced OS termination or a crash may lose work
since the latest checkpoint. Resume starts fresh episodes, not exact process recovery.
