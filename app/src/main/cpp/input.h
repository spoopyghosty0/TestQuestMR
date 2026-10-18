// Controllers: aim and grip poses, trigger (select), grip (squeeze, places an anchor), thumbstick (scrolls lists).
// Bindings for Touch controllers and the Khronos simple controller.
#pragma once

#include <openxr/openxr.h>

#include "xmath.h"

class Input {
public:
    XrResult create(XrInstance instance);
    XrResult attach(XrSession session);
    void sync(XrSession session, XrSpace base, XrTime time);
    void destroy();
    void vibrate(XrSession session, int hand, float amplitude, float seconds);

    bool aim_valid[2] = {false, false}, grip_valid[2] = {false, false};
    Pose aim[2], grip[2];
    float trigger[2] = {0, 0}, squeeze[2] = {0, 0};
    float stick_y[2] = {0, 0};
    bool trigger_pressed(int h) const { return trigger[h] > 0.6f && !trigger_was_[h]; }
    bool squeeze_pressed(int h) const { return squeeze[h] > 0.7f && !squeeze_was_[h]; }

private:
    XrInstance instance_ = XR_NULL_HANDLE;
    XrActionSet set_ = XR_NULL_HANDLE;
    XrAction aim_ = XR_NULL_HANDLE, grip_ = XR_NULL_HANDLE, trigger_ = XR_NULL_HANDLE, squeeze_ = XR_NULL_HANDLE,
             stick_ = XR_NULL_HANDLE, haptic_ = XR_NULL_HANDLE;
    XrPath hands_[2] = {XR_NULL_PATH, XR_NULL_PATH};
    XrSpace aim_space_[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE}, grip_space_[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    bool trigger_was_[2] = {false, false}, squeeze_was_[2] = {false, false};
};
