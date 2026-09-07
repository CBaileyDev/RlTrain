#pragma once
#include <array>
#include "util/Common.h"

namespace rls {
/// Eight controller inputs in the simulator's order, independent of RocketSim.
using Controls = std::array<f32, 8>;
/// A fixed, versioned action vocabulary shared by training and playback.
const std::array<Controls, 90>& ActionTable();
}
