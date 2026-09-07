#include "ActionParser.h"
#include <stdexcept>
namespace rls {
const std::array<Controls, 90>& ActionTable() {
    static const auto table = [] {
        std::array<Controls, 90> result{};
        int index = 0;
        // Ground actions: boost implies full throttle; handbrake allows powerslides.
        for (float throttle : {-1.f, 0.f, 1.f})
            for (float steer : {-1.f, 0.f, 1.f})
                for (float boost : {0.f, 1.f})
                    for (float brake : {0.f, 1.f}) {
                        if (boost && throttle != 1.f) continue;
                        result.at(index++) = {throttle, steer, 0, steer, 0, 0, boost, brake};
                    }
        // Air actions include directional dodges and independent roll.
        for (float pitch : {-1.f, 0.f, 1.f})
            for (float yaw : {-1.f, 0.f, 1.f})
                for (float roll : {-1.f, 0.f, 1.f})
                    for (float jump : {0.f, 1.f})
                        for (float boost : {0.f, 1.f}) {
                            if (jump && yaw != 0) continue;
                            if (!jump && pitch == 0 && roll == 0) continue;
                            const float brake = jump && (pitch != 0 || yaw != 0 || roll != 0) ? 1.f : 0.f;
                            result.at(index++) = {boost, yaw, pitch, yaw, roll, jump, boost, brake};
                        }
        if (index != 90) throw std::logic_error("Action vocabulary must have 90 entries");
        return result;
    }();
    return table;
}
}
