// Test Quest MR: shows what a headset's runtime delivers through the Meta mixed-reality APIs.
//
// The app enables every MR extension the runtime lists, asks for the room the way Meta's SDKs do (XR_FB_spatial_entity
// queries + XR_FB_scene components, and XR_META_spatial_entity_discovery), and draws each answer where the runtime
// puts it, world-locked over passthrough: planes, volumes, the room mesh, STAGE bounds and spatial anchors, each labelled
// with the API that produced it. The panel lists extensions, system properties, every space and every call's result;
// the same log goes to logcat (tag TQMR) and to mr-log.txt in the app's files folder.
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <jni.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "demo_room.h"
#include "gfx.h"
#include "input.h"
#include "log.h"
#include "mr.h"
#include "mrlog.h"
#include "scene.h"

namespace {

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

const char *kScenePermissions[] = {"com.oculus.permission.USE_SCENE", "horizonos.permission.USE_SCENE"};

struct Swap {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    EyeTarget target;
    int width = 0, height = 0;
};

class App {
public:
    explicit App(android_app *a) : android_(a) {}
    void run();

private:
    android_app *android_;
    JNIEnv *env_ = nullptr;
    Gl gl_;
    Scene scene_;
    Mr mr_;
    Input input_;
    ViewOptions view_;

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId system_ = XR_NULL_SYSTEM_ID;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace local_ = XR_NULL_HANDLE, view_space_ = XR_NULL_HANDLE, stage_ = XR_NULL_HANDLE;
    XrSessionState state_ = XR_SESSION_STATE_UNKNOWN;
    bool running_ = false, exit_ = false, started_ = false;
    std::set<std::string> enabled_;
    std::vector<XrViewConfigurationView> view_configs_;
    Swap swaps_[2];
    Swap panel_swap_;  // the panel, shown as a quad layer
    std::string files_;

    // passthrough: XR_FB_passthrough (Quest; what Quest MR games use), else the ALPHA_BLEND blend mode
    std::string passthrough_mode_;  // "" off, "XR_FB_passthrough" or "ALPHA_BLEND"
    std::vector<XrEnvironmentBlendMode> blend_modes_;
    XrPassthroughFB pt_ = XR_NULL_HANDLE;
    XrPassthroughLayerFB pt_layer_ = XR_NULL_HANDLE;

    bool placed_ = false, demo_ = false, demo_done_ = false;
    Pose user_, head_;
    int hover_ = -1, pressed_ = -1;
    int drag_id_ = -1, drag_hand_ = -1;  // slider held with the trigger
    double pressed_at_ = 0, scroll_at_ = 0, message_at_ = 0, permission_check_ = 0, nudge_at_ = 0;
    double save_at_ = -1;  // save settings at this time (after a thumbstick nudge)
    std::string message_;
    bool permission_granted_ = true;

    void load_settings();
    void save_settings();
    void set_passthrough(bool on);
    void stop_passthrough();
    bool open_instance();
    bool open_session();
    void close_session();
    void handle_state(XrSessionState s);
    void poll_events();
    void check_permission(bool request);
    void frame();
    void update_ui(XrTime time);
    void press(int id, int hand);
    int &slider_value(int id) { return id == kSliderPanel ? view_.panel_opacity : view_.overlay_opacity; }
    void set_slider(int id, int percent, int hand);
    void place(XrTime time);
    void clamp_scroll() {
        const MrState &st = mr_.st;
        const RoomSet &set = view_.source == kSourceMeta ? st.meta : view_.source == kSourceExt ? st.ext : st.fb;
        int rows = scene_.panel.rows_visible();
        view_.room_scroll = std::min(view_.room_scroll, std::max(0, (int)set.entities.size() - rows));
        view_.log_scroll = std::min(view_.log_scroll, std::max(0, (int)MrLog::get().lines().size() - rows));
    }
    void say(const std::string &m) {
        message_ = m;
        message_at_ = now_s();
    }
};

// ---------------------------------------------------------------- settings
void App::load_settings() {
    std::ifstream f(files_ + "/settings.txt");
    std::string key;
    int value;
    while (f >> key >> value) {
        if (key == "passthrough") view_.passthrough = value != 0;
        else if (key == "source") view_.source = value % kSourceModeCount;
        else if (key == "mesh") view_.mesh = value % kMeshModeCount;
        else if (key == "tab") view_.tab = value % kTabCount;
        else if (key == "planes") view_.planes = value != 0;
        else if (key == "volumes") view_.volumes = value != 0;
        else if (key == "labels") view_.labels = value != 0;
        else if (key == "axes") view_.gizmos = value != 0;
        else if (key == "boundary") view_.boundary = value != 0;
        else if (key == "overlay_opacity") view_.overlay_opacity = std::clamp(value, 0, 100);
        else if (key == "panel_opacity") view_.panel_opacity = std::clamp(value, 0, 100);
    }
}

void App::save_settings() {
    std::ofstream f(files_ + "/settings.txt");
    f << "passthrough " << view_.passthrough << "\nsource " << view_.source << "\nmesh " << view_.mesh << "\ntab "
      << view_.tab << "\nplanes " << view_.planes << "\nvolumes " << view_.volumes << "\nlabels " << view_.labels
      << "\naxes " << view_.gizmos << "\nboundary " << view_.boundary << "\noverlay_opacity " << view_.overlay_opacity
      << "\npanel_opacity " << view_.panel_opacity << "\n";
}

// ---------------------------------------------------------------- permissions
// com.oculus.permission.USE_SCENE is a runtime permission on Quest: without it the room queries find nothing.
void App::check_permission(bool request) {
    if (!env_) return;
    jobject activity = android_->activity->clazz;
    jclass cls = env_->GetObjectClass(activity);
    jmethodID check = env_->GetMethodID(cls, "checkSelfPermission", "(Ljava/lang/String;)I");
    std::vector<std::string> missing;
    for (const char *p : kScenePermissions) {
        jstring s = env_->NewStringUTF(p);
        jint r = env_->CallIntMethod(activity, check, s);
        env_->DeleteLocalRef(s);
        if (env_->ExceptionCheck()) env_->ExceptionClear(), r = -1;
        if (r != 0) missing.push_back(p);
    }
    // one of the two names is enough (older / newer Horizon OS know one of them)
    bool granted = missing.size() < 2;
    std::string state = missing.empty() ? "granted" : granted ? "granted (" + missing[0] + " not)" : "NOT granted";
    if (state != mr_.st.permission) MLOG("scene permission: %s", state.c_str());
    mr_.st.permission = state;
    if (granted && !permission_granted_ && started_) {
        MLOG("scene permission granted: asking for the room again");
        mr_.refresh();
    }
    permission_granted_ = granted;
    if (!granted && request) {
        jclass string_cls = env_->FindClass("java/lang/String");
        jobjectArray arr = env_->NewObjectArray((jsize)missing.size(), string_cls, nullptr);
        for (size_t i = 0; i < missing.size(); ++i) {
            jstring s = env_->NewStringUTF(missing[i].c_str());
            env_->SetObjectArrayElement(arr, (jsize)i, s);
            env_->DeleteLocalRef(s);
        }
        jmethodID req = env_->GetMethodID(cls, "requestPermissions", "([Ljava/lang/String;I)V");
        env_->CallVoidMethod(activity, req, arr, 1);
        if (env_->ExceptionCheck()) {
            env_->ExceptionClear();
            MERR("requestPermissions failed");
        } else {
            MLOG("requestPermissions(USE_SCENE) sent");
        }
        env_->DeleteLocalRef(arr);
    }
    env_->DeleteLocalRef(cls);
}

// ---------------------------------------------------------------- passthrough
void App::stop_passthrough() {
    if (instance_ && (pt_ || pt_layer_)) {
        PFN_xrDestroyPassthroughLayerFB destroy_layer = nullptr;
        PFN_xrDestroyPassthroughFB destroy = nullptr;
        xrGetInstanceProcAddr(instance_, "xrDestroyPassthroughLayerFB", (PFN_xrVoidFunction *)&destroy_layer);
        xrGetInstanceProcAddr(instance_, "xrDestroyPassthroughFB", (PFN_xrVoidFunction *)&destroy);
        if (pt_layer_ && destroy_layer) destroy_layer(pt_layer_);
        if (pt_ && destroy) destroy(pt_);
    }
    pt_layer_ = XR_NULL_HANDLE;
    pt_ = XR_NULL_HANDLE;
    passthrough_mode_.clear();
}

void App::set_passthrough(bool on) {
    stop_passthrough();
    view_.passthrough = on;
    mr_.st.passthrough = on ? "" : "off";
    if (!on || !session_) return;
    std::string note;
    if (enabled_.count("XR_FB_passthrough")) {
        PFN_xrCreatePassthroughFB create = nullptr;
        PFN_xrCreatePassthroughLayerFB create_layer = nullptr;
        xrGetInstanceProcAddr(instance_, "xrCreatePassthroughFB", (PFN_xrVoidFunction *)&create);
        xrGetInstanceProcAddr(instance_, "xrCreatePassthroughLayerFB", (PFN_xrVoidFunction *)&create_layer);
        XrResult r = XR_ERROR_FUNCTION_UNSUPPORTED;
        if (create && create_layer) {
            XrPassthroughCreateInfoFB info{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
            info.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
            r = create(session_, &info, &pt_);
            if (XR_SUCCEEDED(r)) {
                XrPassthroughLayerCreateInfoFB layer{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
                layer.passthrough = pt_;
                layer.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
                layer.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
                r = create_layer(session_, &layer, &pt_layer_);
            }
        }
        MrLog::get().add(XR_FAILED(r) ? 2 : 0, "passthrough: xrCreatePassthroughFB + layer -> %s", xr_result_str(r));
        if (XR_SUCCEEDED(r)) {
            passthrough_mode_ = "XR_FB_passthrough";
            mr_.st.passthrough = "XR_FB_passthrough layer";
            return;
        }
        stop_passthrough();
        note = std::string("XR_FB_passthrough failed (") + xr_result_str(r) + "), ";
    }
    for (XrEnvironmentBlendMode m : blend_modes_)
        if (m == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) {
            passthrough_mode_ = "ALPHA_BLEND";
            mr_.st.passthrough = note + "blend mode ALPHA_BLEND";
            MLOG("passthrough: blend mode ALPHA_BLEND");
            return;
        }
    mr_.st.passthrough = note + "not offered by this runtime";
    MWARN("passthrough: %s", mr_.st.passthrough.c_str());
}

// ---------------------------------------------------------------- instance / session
bool App::open_instance() {
    PFN_xrInitializeLoaderKHR init = nullptr;
    if (XR_SUCCEEDED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)&init)) &&
        init) {
        XrLoaderInitInfoAndroidKHR info{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        info.applicationVM = android_->activity->vm;
        info.applicationContext = android_->activity->clazz;
        XrResult r = init((const XrLoaderInitInfoBaseHeaderKHR *)&info);
        MLOG("xrInitializeLoaderKHR -> %s", xr_result_str(r));
    }
    uint32_t count = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
    std::vector<XrExtensionProperties> props(count, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, count, &count, props.data());
    std::set<std::string> listed;
    std::map<std::string, uint32_t> versions;
    for (const auto &p : props) {
        listed.insert(p.extensionName);
        versions[p.extensionName] = p.extensionVersion;
    }
    MLOG("runtime lists %u extensions", count);

    XrInstanceCreateInfoAndroidKHR android_info{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android_info.applicationVM = android_->activity->vm;
    android_info.applicationActivity = android_->activity->clazz;
    XrResult r = XR_ERROR_INITIALIZATION_FAILED;
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::vector<const char *> exts = {"XR_KHR_android_create_instance", "XR_KHR_opengl_es_enable"};
        enabled_ = {exts.begin(), exts.end()};
        for (const std::string &e : listed)
            if (Mr::wants(e) && (attempt == 0 || e == "XR_FB_passthrough")) {
                exts.push_back(e.c_str());
                enabled_.insert(e);
            }
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
        info.next = &android_info;
        std::strcpy(info.applicationInfo.applicationName, "Test Quest MR");
        info.applicationInfo.applicationVersion = 1;
        std::strcpy(info.applicationInfo.engineName, "none");
        info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
        info.enabledExtensionCount = (uint32_t)exts.size();
        info.enabledExtensionNames = exts.data();
        r = xrCreateInstance(&info, &instance_);
        MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrCreateInstance (OpenXR 1.0, %zu extensions%s) -> %s", exts.size(),
                         attempt ? ", MR extensions left out" : "", xr_result_str(r));
        if (XR_SUCCEEDED(r)) break;
        instance_ = XR_NULL_HANDLE;
    }
    if (!instance_) return false;
    for (const std::string &e : enabled_) MLOG("  enabled %s", e.c_str());

    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(instance_, &ip);
    char rt[160];
    std::snprintf(rt, sizeof(rt), "%s %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
                  XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));
    mr_.st.runtime = rt;
    mr_.st.instance = "OpenXR 1.0 instance";

    XrSystemGetInfo sys{XR_TYPE_SYSTEM_GET_INFO};
    sys.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = xrGetSystem(instance_, &sys, &system_);
    if (XR_FAILED(r)) {
        MERR("xrGetSystem -> %s", xr_result_str(r));
        return false;
    }
    XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
    xrGetSystemProperties(instance_, system_, &sp);
    mr_.st.system = sp.systemName;
    MLOG("runtime: %s, system: %s", rt, sp.systemName);
    mr_.on_instance(instance_, system_, listed, enabled_, versions);

    PFN_xrGetOpenGLESGraphicsRequirementsKHR reqs = nullptr;
    xrGetInstanceProcAddr(instance_, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&reqs);
    if (reqs) {
        XrGraphicsRequirementsOpenGLESKHR g{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
        reqs(instance_, system_, &g);
    }
    count = 0;
    xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr);
    view_configs_.assign(count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count,
                                      view_configs_.data());
    input_.create(instance_);
    return count >= 2;
}

bool App::open_session() {
    XrGraphicsBindingOpenGLESAndroidKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    binding.display = gl_.display;
    binding.config = gl_.config;
    binding.context = gl_.context;
    XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};
    info.next = &binding;
    info.systemId = system_;
    XrResult r = xrCreateSession(instance_, &info, &session_);
    MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrCreateSession -> %s", xr_result_str(r));
    if (XR_FAILED(r)) {
        session_ = XR_NULL_HANDLE;
        return false;
    }
    XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space.poseInReferenceSpace.orientation.w = 1;
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    xrCreateReferenceSpace(session_, &space, &local_);
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    xrCreateReferenceSpace(session_, &space, &view_space_);
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    r = xrCreateReferenceSpace(session_, &space, &stage_);
    MrLog::get().add(XR_FAILED(r) ? 1 : 0, "xrCreateReferenceSpace(STAGE) -> %s", xr_result_str(r));
    if (XR_FAILED(r)) stage_ = XR_NULL_HANDLE;

    uint32_t count = 0;
    xrEnumerateSwapchainFormats(session_, 0, &count, nullptr);
    std::vector<int64_t> formats(count);
    xrEnumerateSwapchainFormats(session_, count, &count, formats.data());
    int64_t color_format = formats.empty() ? GL_SRGB8_ALPHA8 : formats[0];
    for (int64_t f : formats)
        if (f == GL_SRGB8_ALPHA8) color_format = f;
    for (int eye = 0; eye < 2; ++eye) {
        Swap &s = swaps_[eye];
        XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sc.format = color_format;
        sc.sampleCount = 1;
        sc.width = view_configs_[eye].recommendedImageRectWidth;
        sc.height = view_configs_[eye].recommendedImageRectHeight;
        sc.faceCount = 1;
        sc.arraySize = 1;
        sc.mipCount = 1;
        r = xrCreateSwapchain(session_, &sc, &s.handle);
        if (XR_FAILED(r)) {
            MERR("xrCreateSwapchain -> %s", xr_result_str(r));
            return false;
        }
        s.width = (int)sc.width;
        s.height = (int)sc.height;
        uint32_t n = 0;
        xrEnumerateSwapchainImages(s.handle, 0, &n, nullptr);
        s.images.assign(n, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        xrEnumerateSwapchainImages(s.handle, n, &n, (XrSwapchainImageBaseHeader *)s.images.data());
        std::vector<GLuint> tex;
        for (const auto &img : s.images) tex.push_back(img.image);
        s.target.create(tex, s.width, s.height);
    }
    {
        // the panel: its own swapchain at kPanelTexelsPerUnit (1800 x 1364, a little more than one texel per display
        // pixel at its distance), composited as a quad layer: sampled once by the compositor instead of drawn into
        // the eye buffers and resampled again
        Swap &s = panel_swap_;
        XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sc.format = color_format;
        sc.sampleCount = 1;
        sc.width = (uint32_t)scene_.panel_texture_width();
        sc.height = (uint32_t)scene_.panel_texture_height();
        sc.faceCount = 1;
        sc.arraySize = 1;
        sc.mipCount = 1;
        r = xrCreateSwapchain(session_, &sc, &s.handle);
        MrLog::get().add(XR_FAILED(r) ? 2 : 0, "panel: xrCreateSwapchain(%ux%u) -> %s", sc.width, sc.height,
                         xr_result_str(r));
        if (XR_SUCCEEDED(r)) {
            s.width = (int)sc.width;
            s.height = (int)sc.height;
            uint32_t n = 0;
            xrEnumerateSwapchainImages(s.handle, 0, &n, nullptr);
            s.images.assign(n, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
            xrEnumerateSwapchainImages(s.handle, n, &n, (XrSwapchainImageBaseHeader *)s.images.data());
            std::vector<GLuint> tex;
            for (const auto &img : s.images) tex.push_back(img.image);
            s.target.create(tex, s.width, s.height, false);
        } else {
            s.handle = XR_NULL_HANDLE;
        }
    }
    r = input_.attach(session_);
    MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrAttachSessionActionSets -> %s", xr_result_str(r));
    uint32_t n = 0;
    xrEnumerateEnvironmentBlendModes(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &n, nullptr);
    blend_modes_.assign(n, XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
    xrEnumerateEnvironmentBlendModes(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, n, &n,
                                     blend_modes_.data());
    mr_.on_session(session_, local_, stage_);
    set_passthrough(view_.passthrough);
    return true;
}

void App::close_session() {
    stop_passthrough();
    mr_.on_session_end();
    input_.destroy();
    for (Swap *sp : {&swaps_[0], &swaps_[1], &panel_swap_}) {
        Swap &s = *sp;
        s.target.release();
        if (s.handle) xrDestroySwapchain(s.handle);
        s.handle = XR_NULL_HANDLE;
        s.images.clear();
    }
    for (XrSpace *s : {&local_, &view_space_, &stage_})
        if (*s) xrDestroySpace(*s), *s = XR_NULL_HANDLE;
    if (session_) xrDestroySession(session_);
    session_ = XR_NULL_HANDLE;
    running_ = false;
}

// ---------------------------------------------------------------- events
void App::handle_state(XrSessionState s) {
    state_ = s;
    MLOG("session state %s", session_state_name(s));
    switch (s) {
        case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
            begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            XrResult r = xrBeginSession(session_, &begin);
            running_ = XR_SUCCEEDED(r);
            MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrBeginSession -> %s", xr_result_str(r));
            if (running_ && !started_) {
                started_ = true;
                if (demo_) MLOG("demo mode: the synthetic room is shown instead of the runtime's");
                else mr_.start();
            }
            break;
        }
        case XR_SESSION_STATE_STOPPING:
            xrEndSession(session_);
            running_ = false;
            break;
        case XR_SESSION_STATE_EXITING:
        case XR_SESSION_STATE_LOSS_PENDING:
            exit_ = true;
            break;
        default:
            break;
    }
}

void App::poll_events() {
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (instance_ && xrPollEvent(instance_, &ev) == XR_SUCCESS) {
        if (!mr_.handle_event(ev)) {
            switch (ev.type) {
                case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                    auto *e = (XrEventDataSessionStateChanged *)&ev;
                    if (e->session == session_) handle_state(e->state);
                    break;
                }
                case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                    exit_ = true;
                    break;
                case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                    MLOG("event INTERACTION_PROFILE_CHANGED");
                    break;
                default:
                    MLOG("event type %d", (int)ev.type);
                    break;
            }
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

// ---------------------------------------------------------------- per frame
void App::place(XrTime time) {
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(view_space_, local_, time, &loc)) ||
        !(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
        return;
    Pose head(loc.pose);
    Vec3 f = head.q.rotate({0, 0, -1});
    user_ = Pose(Quat::axis_angle({0, 1, 0}, std::atan2(-f.x, -f.z)), head.p);
    placed_ = true;
}

void App::press(int id, int hand) {
    pressed_ = id;
    pressed_at_ = now_s();
    input_.vibrate(session_, hand, 0.4f, 0.02f);
    MrState &st = mr_.st;
    switch (id) {
        case kBtnPlanes: view_.planes = !view_.planes; break;
        case kBtnVolumes: view_.volumes = !view_.volumes; break;
        case kBtnMesh: view_.mesh = (view_.mesh + 1) % kMeshModeCount; break;
        case kBtnLabels: view_.labels = !view_.labels; break;
        case kBtnAxes: view_.gizmos = !view_.gizmos; break;
        case kBtnBoundary: view_.boundary = !view_.boundary; break;
        case kBtnPassthrough: set_passthrough(!view_.passthrough); break;
        case kBtnSource:
            view_.source = (view_.source + 1) % kSourceModeCount;
            if (view_.source == kSourceExt && !mr_.enabled("XR_EXT_plane_detection")) view_.source = kSourceFb;
            view_.room_scroll = 0;
            break;
        case kBtnRefresh:
            if (demo_) say("demo mode: the room is synthetic (delete the demo file to see the runtime's)");
            else mr_.refresh(), say("asked again: FB query, META discovery, EXT planes, STAGE bounds");
            break;
        case kBtnCapture:
            if (demo_) say("demo mode: nothing to capture");
            else mr_.scene_capture(), say("xrRequestSceneCaptureFB: " + st.capture_status);
            break;
        case kBtnBoundaryVis: mr_.request_boundary(!st.boundary_suppress_requested); break;
        case kBtnRecenter: placed_ = false; break;
        case kBtnSaveFb: mr_.save_anchors(false); break;
        case kBtnSaveMeta: mr_.save_anchors(true); break;
        case kBtnEraseFb: mr_.erase_anchors(false); break;
        case kBtnEraseMeta: mr_.erase_anchors(true); break;
        case kBtnLoadFb: mr_.load_anchors(false); break;
        case kBtnLoadMeta: mr_.load_anchors(true); break;
        case kBtnClear: mr_.clear_anchors(); break;
        case kBtnUp:
        case kBtnDown: {
            int step = std::max(1, scene_.panel.rows_visible() / 2) * (id == kBtnUp ? 1 : -1);
            if (view_.tab == kTabLog) view_.log_scroll = std::max(0, view_.log_scroll + step);
            else view_.room_scroll = std::max(0, view_.room_scroll - step);
            clamp_scroll();
            break;
        }
        default:
            if (id >= kBtnTab && id < kBtnTab + kTabCount) view_.tab = id - kBtnTab;
            break;
    }
    save_settings();
}

void App::set_slider(int id, int percent, int hand) {
    int &value = slider_value(id);
    percent = std::clamp(percent, 0, 100);
    if (percent == value) return;
    if (percent / 10 != value / 10 || percent == 0 || percent == 100) input_.vibrate(session_, hand, 0.15f, 0.01f);  // a tick per 10 %
    value = percent;
}

void App::update_ui(XrTime time) {
    double t = now_s();
    scene_.layout(user_);
    int hover = -1;
    for (int h = 0; h < 2; ++h) {
        float u, v;
        Vec3 w;
        // a slider held with this hand's trigger follows the pointer along the panel's plane, past its edges too
        if (drag_hand_ == h) {
            if (!input_.trigger_held(h) || !input_.aim_valid[h]) {
                drag_id_ = drag_hand_ = -1;
                save_settings();
            } else {
                if (scene_.panel.hit(input_.aim[h], u, v, w, false))
                    set_slider(drag_id_, (int)std::lround(scene_.panel.slider_value(drag_id_, u) * 100), h);
                hover = drag_id_;
                continue;
            }
        }
        bool on_panel = input_.aim_valid[h] && scene_.panel.hit(input_.aim[h], u, v, w);
        int item = on_panel ? scene_.panel.item_at(u, v) : -1;
        if (item >= 0) hover = item;
        bool slider = item >= 0 && scene_.panel.is_slider(item);
        if (input_.trigger_pressed(h) && slider && drag_id_ < 0) {
            drag_id_ = item;
            drag_hand_ = h;
            input_.vibrate(session_, h, 0.4f, 0.02f);
            set_slider(item, (int)std::lround(scene_.panel.slider_value(item, u) * 100), h);
            continue;
        }
        if (input_.trigger_pressed(h) && item >= 0 && !slider) press(item, h);
        // thumbstick sideways on a slider: 5 % steps (held: repeats)
        if (slider && std::fabs(input_.stick_x[h]) > 0.6f && t - nudge_at_ > 0.12) {
            nudge_at_ = t;
            int &value = slider_value(item);
            set_slider(item, (value / 5 + (input_.stick_x[h] > 0 ? 1 : (value % 5 ? 0 : -1))) * 5, h);
            save_at_ = t + 1.0;
            continue;  // no scrolling while nudging
        }
        // grip: an anchor at that controller (pointing at the panel or not)
        if (input_.squeeze_pressed(h) && input_.grip_valid[h]) {
            if (demo_) say("demo mode: anchors are synthetic");
            else {
                mr_.create_anchor(input_.grip[h]);
                input_.vibrate(session_, h, 0.8f, 0.05f);
                if (view_.tab != kTabAnchors) say("anchor requested (see the Anchors tab)");
            }
        }
        // thumbstick: scroll the list on the panel
        float y = input_.stick_y[h];
        if (std::fabs(y) > 0.6f && t - scroll_at_ > 0.12) {
            scroll_at_ = t;
            int step = y > 0 ? 1 : -1;
            if (view_.tab == kTabLog) view_.log_scroll = std::max(0, view_.log_scroll + step);
            else view_.room_scroll = std::max(0, view_.room_scroll - step);
            clamp_scroll();
        }
    }
    hover_ = hover;
    if (save_at_ >= 0 && t > save_at_) {
        save_at_ = -1;
        save_settings();
    }
    if (pressed_ >= 0 && t - pressed_at_ > 0.15) pressed_ = -1;
    if (!message_.empty() && t - message_at_ > 6) message_.clear();
    if (!permission_granted_ && t - permission_check_ > 1.0) {
        permission_check_ = t;
        check_permission(false);
    }
    (void)time;
}

void App::frame() {
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState fs{XR_TYPE_FRAME_STATE};
    if (XR_FAILED(xrWaitFrame(session_, &wait, &fs))) return;
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    if (XR_FAILED(xrBeginFrame(session_, &begin))) return;

    mr_.update(fs.predictedDisplayTime);
    std::vector<XrCompositionLayerProjectionView> proj(2, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerPassthroughFB pt_layer{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
    pt_layer.layerHandle = pt_layer_;
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    bool panel_rendered = false;
    const XrCompositionLayerBaseHeader *layers[3];
    uint32_t layer_count = 0;
    bool rendered = false;

    if (fs.shouldRender) {
        XrViewLocateInfo vli{XR_TYPE_VIEW_LOCATE_INFO};
        vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        vli.displayTime = fs.predictedDisplayTime;
        vli.space = local_;
        XrViewState vs{XR_TYPE_VIEW_STATE};
        XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        uint32_t n = 0;
        if (XR_SUCCEEDED(xrLocateViews(session_, &vli, &vs, 2, &n, views)) && n == 2) {
            input_.sync(session_, local_, fs.predictedDisplayTime);
            if (!placed_) place(fs.predictedDisplayTime);
            XrSpaceLocation hl{XR_TYPE_SPACE_LOCATION};
            if (XR_SUCCEEDED(xrLocateSpace(view_space_, local_, fs.predictedDisplayTime, &hl)) &&
                (hl.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
                head_ = Pose(hl.pose);
            if (demo_ && !demo_done_ && placed_) {
                // the synthetic room on the real floor if STAGE is known, else 1.6 m below the start position
                Pose stage = mr_.st.stage_located ? mr_.st.stage_pose : Pose(user_.q, user_.p + Vec3(0, -1.6f, 0));
                demo_room(mr_.st, stage);
                demo_done_ = true;
            }
            update_ui(fs.predictedDisplayTime);
            SceneData d;
            d.mr = &mr_.st;
            d.view = &view_;
            d.head = head_;
            for (int h = 0; h < 2; ++h) {
                d.aim_valid[h] = input_.aim_valid[h];
                d.aim[h] = input_.aim[h];
                d.grip_valid[h] = input_.grip_valid[h];
                d.grip[h] = input_.grip[h];
            }
            d.panel.hover = hover_;
            d.panel.pressed = pressed_;
            d.panel.message = message_;
            d.panel.log_file = MrLog::get().path().empty() ? "" : "mr-log.txt in the app's files folder";
            d.panel.session_state = session_state_name(state_);
            d.panel.dragging = drag_id_;
            if (panel_swap_.handle) {
                Swap &s = panel_swap_;
                uint32_t image = 0;
                XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                wi.timeout = XR_INFINITE_DURATION;
                if (XR_SUCCEEDED(xrAcquireSwapchainImage(s.handle, &ai, &image)) &&
                    XR_SUCCEEDED(xrWaitSwapchainImage(s.handle, &wi))) {
                    glBindFramebuffer(GL_FRAMEBUFFER, s.target.fbos[image]);
                    scene_.render_panel(d, s.width, s.height);
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                    xrReleaseSwapchainImage(s.handle, &ri);
                    // premultiplied alpha (the panel's background opacity), over the projection layer
                    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                    quad.space = local_;
                    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                    quad.subImage.swapchain = s.handle;
                    quad.subImage.imageRect = {{0, 0}, {s.width, s.height}};
                    quad.pose = scene_.panel.pose.xr();
                    quad.size = {scene_.panel.width * scene_.panel.px, scene_.panel.height * scene_.panel.px};
                    panel_rendered = true;
                }
            }
            for (int eye = 0; eye < 2; ++eye) {
                Swap &s = swaps_[eye];
                uint32_t image = 0;
                XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                xrAcquireSwapchainImage(s.handle, &ai, &image);
                XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                wi.timeout = XR_INFINITE_DURATION;
                xrWaitSwapchainImage(s.handle, &wi);
                glBindFramebuffer(GL_FRAMEBUFFER, s.target.fbos[image]);
                glViewport(0, 0, s.width, s.height);
                glEnable(GL_DEPTH_TEST);
                glDisable(GL_CULL_FACE);
                glEnable(GL_BLEND);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                if (passthrough_mode_.empty()) glClearColor(kClearColor[0], kClearColor[1], kClearColor[2], 1);
                else glClearColor(0, 0, 0, 0);  // the real world shows through
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                Mat4 view_m = Mat4::from_pose(Pose(views[eye].pose).inverse());
                scene_.draw(Mat4::projection(views[eye].fov, 0.05f, 50.0f) * view_m, d);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                xrReleaseSwapchainImage(s.handle, &ri);
                proj[eye].pose = views[eye].pose;
                proj[eye].fov = views[eye].fov;
                proj[eye].subImage.swapchain = s.handle;
                proj[eye].subImage.imageRect = {{0, 0}, {s.width, s.height}};
            }
            layer.space = local_;
            if (!passthrough_mode_.empty())
                layer.layerFlags =
                    XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
            layer.viewCount = 2;
            layer.views = proj.data();
            rendered = true;
        }
    }
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = fs.predictedDisplayTime;
    end.environmentBlendMode =
        passthrough_mode_ == "ALPHA_BLEND" ? XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND : XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    if (pt_layer_) layers[layer_count++] = (const XrCompositionLayerBaseHeader *)&pt_layer;  // under the scene
    if (rendered) layers[layer_count++] = (const XrCompositionLayerBaseHeader *)&layer;
    if (rendered && panel_rendered) layers[layer_count++] = (const XrCompositionLayerBaseHeader *)&quad;
    end.layerCount = layer_count;
    end.layers = layer_count ? layers : nullptr;
    XrResult r = xrEndFrame(session_, &end);
    if (XR_FAILED(r)) {
        static int logged = 0;
        if (logged++ < 5) MERR("xrEndFrame -> %s", xr_result_str(r));
    }
}

// ---------------------------------------------------------------- main loop
void App::run() {
    android_->activity->vm->AttachCurrentThread(&env_, nullptr);
    const char *dir = android_->activity->externalDataPath ? android_->activity->externalDataPath
                                                           : android_->activity->internalDataPath;
    files_ = dir ? dir : ".";
    mr_.files_dir = files_;
    MrLog::get().open(files_ + "/mr-log.txt");
    MLOG("Test Quest MR starting; files in %s", files_.c_str());
    load_settings();
    demo_ = std::ifstream(files_ + "/demo").good();
    if (demo_) view_.passthrough = false;  // the synthetic room isn't the real one
    permission_granted_ = true;
    check_permission(true);

    // the system's font (Roboto on Android); stb_easy_font if none can be read
    font::load({"/system/fonts/Roboto-Regular.ttf", "/system/fonts/NotoSans-Regular.ttf", "/system/fonts/DroidSans.ttf"});
    MLOG("font: %s", font::name().c_str());
    if (!gl_.init() || !scene_.init()) {
        MERR("startup failed");
        exit_ = true;
    } else if (!open_instance() || !open_session()) {
        exit_ = true;
    }

    bool finishing = false;
    while (!android_->destroyRequested) {
        int events = 0;
        android_poll_source *source = nullptr;
        while (ALooper_pollOnce(running_ && !exit_ ? 0 : 100, nullptr, &events, (void **)&source) >= 0) {
            if (source) source->process(android_, source);
            if (android_->destroyRequested) break;
        }
        if (android_->destroyRequested) break;
        if (exit_) {
            if (!finishing) {
                MLOG("exiting");
                ANativeActivity_finish(android_->activity);
                finishing = true;
            }
            continue;
        }
        poll_events();
        if (running_) frame();
    }
    close_session();
    if (instance_) xrDestroyInstance(instance_);
    instance_ = XR_NULL_HANDLE;
    gl_.shutdown();
    android_->activity->vm->DetachCurrentThread();
}

}  // namespace

void android_main(android_app *app) {
    App(app).run();
    // Finishing a NativeActivity leaves its process running, and a container (e.g. the Steam Frame's Lepton) only ends
    // the app once the process is gone. OpenXR and GL are shut down already.
    std::exit(0);
}
