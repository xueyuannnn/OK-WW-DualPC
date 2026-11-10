#pragma once
#include "character.hpp"
namespace okww::runtime {
class HeldCombatInput {
    CombatHost &host_;
    QString key_;
    bool mouse_, held_ = true;

  public:
    HeldCombatInput(CombatHost &host, QString key = "left", bool mouse = true)
        : host_(host), key_(std::move(key)), mouse_(mouse) {
        if (mouse_)
            host_.mouseDown(key_);
        else
            host_.keyDown(key_);
    }
    HeldCombatInput(const HeldCombatInput &) = delete;
    HeldCombatInput &operator=(const HeldCombatInput &) = delete;
    void release() {
        if (!held_)
            return;
        held_ = false;
        if (mouse_)
            host_.mouseUp(key_);
        else
            host_.keyUp(key_);
    }
    ~HeldCombatInput() {
        if (held_)
            try {
                release();
            } catch (...) {
            }
    }
};
} // namespace okww::runtime
