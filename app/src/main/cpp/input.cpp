#include "input.h"

#include <cstring>
#include <string>
#include <vector>

#include "mrlog.h"
#include "xrnames.h"

static XrPath path(XrInstance instance, const char *s) {
    XrPath p = XR_NULL_PATH;
    xrStringToPath(instance, s, &p);
    return p;
}

XrResult Input::create(XrInstance instance) {
    instance_ = instance;
    hands_[0] = path(instance, "/user/hand/left");
    hands_[1] = path(instance, "/user/hand/right");
    XrActionSetCreateInfo set_info{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(set_info.actionSetName, "mr_test");
    std::strcpy(set_info.localizedActionSetName, "MR test");
    XrResult r = xrCreateActionSet(instance, &set_info, &set_);
    if (XR_FAILED(r)) return r;
    auto action = [&](const char *name, XrActionType type, XrAction &out) {
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        std::strcpy(info.actionName, name);
        std::strcpy(info.localizedActionName, name);
        info.actionType = type;
        info.countSubactionPaths = 2;
        info.subactionPaths = hands_;
        xrCreateAction(set_, &info, &out);
    };
    action("aim", XR_ACTION_TYPE_POSE_INPUT, aim_);
    action("grip", XR_ACTION_TYPE_POSE_INPUT, grip_);
    action("select", XR_ACTION_TYPE_FLOAT_INPUT, trigger_);
    action("place_anchor", XR_ACTION_TYPE_FLOAT_INPUT, squeeze_);
    action("scroll", XR_ACTION_TYPE_VECTOR2F_INPUT, stick_);
    action("haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT, haptic_);

    struct Profile {
        const char *path;
        const char *trigger, *squeeze, *stick;
    };
    const Profile profiles[] = {
        {"/interaction_profiles/oculus/touch_controller", "trigger/value", "squeeze/value", "thumbstick"},
        {"/interaction_profiles/khr/simple_controller", "select/click", nullptr, nullptr},
    };
    for (const Profile &p : profiles) {
        std::vector<XrActionSuggestedBinding> b;
        for (int h = 0; h < 2; ++h) {
            std::string hand = h ? "/user/hand/right/" : "/user/hand/left/";
            b.push_back({aim_, path(instance, (hand + "input/aim/pose").c_str())});
            b.push_back({grip_, path(instance, (hand + "input/grip/pose").c_str())});
            b.push_back({trigger_, path(instance, (hand + "input/" + p.trigger).c_str())});
            if (p.squeeze) b.push_back({squeeze_, path(instance, (hand + "input/" + p.squeeze).c_str())});
            if (p.stick) b.push_back({stick_, path(instance, (hand + "input/" + p.stick).c_str())});
            b.push_back({haptic_, path(instance, (hand + "output/haptic").c_str())});
        }
        XrInteractionProfileSuggestedBinding s{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        s.interactionProfile = path(instance, p.path);
        s.countSuggestedBindings = (uint32_t)b.size();
        s.suggestedBindings = b.data();
        XrResult sr = xrSuggestInteractionProfileBindings(instance, &s);
        MrLog::get().add(XR_FAILED(sr) ? 2 : 0, "bindings for %s -> %s", p.path, xr_result_str(sr));
    }
    return XR_SUCCESS;
}

XrResult Input::attach(XrSession session) {
    XrSessionActionSetsAttachInfo info{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    info.countActionSets = 1;
    info.actionSets = &set_;
    XrResult r = xrAttachSessionActionSets(session, &info);
    for (int h = 0; h < 2; ++h) {
        XrActionSpaceCreateInfo si{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        si.poseInActionSpace.orientation.w = 1;
        si.subactionPath = hands_[h];
        si.action = aim_;
        xrCreateActionSpace(session, &si, &aim_space_[h]);
        si.action = grip_;
        xrCreateActionSpace(session, &si, &grip_space_[h]);
    }
    return r;
}

void Input::sync(XrSession session, XrSpace base, XrTime time) {
    for (int h = 0; h < 2; ++h) {
        trigger_was_[h] = trigger[h] > 0.6f;
        squeeze_was_[h] = squeeze[h] > 0.7f;
    }
    XrActiveActionSet active{set_, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (XR_FAILED(xrSyncActions(session, &sync))) return;
    for (int h = 0; h < 2; ++h) {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.subactionPath = hands_[h];
        XrActionStateFloat f{XR_TYPE_ACTION_STATE_FLOAT};
        get.action = trigger_;
        trigger[h] = XR_SUCCEEDED(xrGetActionStateFloat(session, &get, &f)) && f.isActive ? f.currentState : 0;
        f = {XR_TYPE_ACTION_STATE_FLOAT};
        get.action = squeeze_;
        squeeze[h] = XR_SUCCEEDED(xrGetActionStateFloat(session, &get, &f)) && f.isActive ? f.currentState : 0;
        XrActionStateVector2f v{XR_TYPE_ACTION_STATE_VECTOR2F};
        get.action = stick_;
        stick_y[h] = XR_SUCCEEDED(xrGetActionStateVector2f(session, &get, &v)) && v.isActive ? v.currentState.y : 0;
        for (int k = 0; k < 2; ++k) {
            XrSpace space = k ? grip_space_[h] : aim_space_[h];
            XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
            bool ok = space && XR_SUCCEEDED(xrLocateSpace(space, base, time, &loc)) &&
                      (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                      (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
            (k ? grip_valid : aim_valid)[h] = ok;
            if (ok) (k ? grip : aim)[h] = Pose(loc.pose);
        }
    }
}

void Input::vibrate(XrSession session, int hand, float amplitude, float seconds) {
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = haptic_;
    info.subactionPath = hands_[hand];
    XrHapticVibration v{XR_TYPE_HAPTIC_VIBRATION};
    v.amplitude = amplitude;
    v.duration = (XrDuration)(seconds * 1e9);
    v.frequency = XR_FREQUENCY_UNSPECIFIED;
    xrApplyHapticFeedback(session, &info, (const XrHapticBaseHeader *)&v);
}

void Input::destroy() {
    for (int h = 0; h < 2; ++h) {
        if (aim_space_[h]) xrDestroySpace(aim_space_[h]);
        if (grip_space_[h]) xrDestroySpace(grip_space_[h]);
        aim_space_[h] = grip_space_[h] = XR_NULL_HANDLE;
    }
    if (set_) xrDestroyActionSet(set_);
    set_ = XR_NULL_HANDLE;
}
