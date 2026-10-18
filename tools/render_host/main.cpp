// Renders the app's scene on a PC (Mesa, surfaceless EGL) with the synthetic demo room, to check the layout and the
// visualisation without a headset. Uses the app's own scene, panel and demo code.
//
//   render_host <out.ppm> <shot> [width height]      (shot icon: RGBA PAM)
//
// Shots: room (inside view with the panel), overview (the whole room, no panel), both (FB + META overlay),
// wire (mesh wireframe), runtime / roomtab / anchors / log (the panel's tabs, close up), icon (for gen_icon.py).
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "demo_room.h"
#include "mr.h"
#include "mrlog.h"
#include "scene.h"

static Pose look_at(const Vec3 &eye, const Vec3 &target) {
    Vec3 fwd = normalize(target - eye), right = normalize(cross(fwd, {0, 1, 0})), up = cross(right, fwd);
    return Pose(Quat::from_basis(right, up, -fwd), eye);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <out.ppm> <shot> [width height]\n", argv[0]);
        return 2;
    }
    std::string out = argv[1], shot = argv[2];
    const int w = argc > 4 ? std::atoi(argv[3]) : 1600, h = argc > 4 ? std::atoi(argv[4]) : 1000;

    auto get_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay display = get_display ? get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                                     : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(display, nullptr, nullptr)) {
        std::fprintf(stderr, "eglInitialize failed\n");
        return 1;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint cfg_attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_NONE};
    EGLConfig config;
    EGLint n = 0;
    eglChooseConfig(display, cfg_attribs, &config, 1, &n);
    const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(display, n ? config : EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        std::fprintf(stderr, "no GLES 3 context (0x%x)\n", eglGetError());
        return 1;
    }

    // 4x supersampled, like the headset's resolution per degree, then box-filtered down
    const int ss = 2, W = w * ss, H = h * ss;
    GLuint tex, depth, fbo;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_SRGB8_ALPHA8, W, H);  // like the headset's swapchain
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);

    Scene scene;
    scene.init();

    // what a Quest-like runtime answers, and the synthetic room in STAGE = base space
    MrState st;
    Mr::extension_list(st.extensions);
    demo_runtime(st);
    st.stage_located = true;
    st.stage_pose = Pose();
    demo_room(st, Pose());
    st.anchors.entities[0].anchor_state = "saved (FB)";
    MLOG("xrCreateInstance (OpenXR 1.0, 16 extensions) -> XR_SUCCESS");
    MLOG("xrQuerySpacesFB(LOAD, component ROOM_LAYOUT, LOCAL) -> XR_SUCCESS request 1");
    MLOG("event SPACE_QUERY_RESULTS_AVAILABLE_FB request 1");
    MLOG("xrRetrieveSpaceQueryResultsFB(request 1) -> XR_SUCCESS, 1 spaces");
    MLOG("xrQuerySpacesFB(LOAD, %zu room UUIDs) -> XR_SUCCESS request 2", st.fb.entities.size() - 1);
    MLOG("event SPACE_QUERY_COMPLETE_FB request 1 -> XR_SUCCESS");
    MLOG("xrDiscoverSpacesMETA(no filter) -> XR_SUCCESS request 3");
    MLOG("event SPACE_DISCOVERY_RESULTS_AVAILABLE_META request 3");
    MLOG("  mesh 13de305d: %zu vertices, %zu triangles", st.fb.entities.back().mesh_vertices.size(),
         st.fb.entities.back().mesh_indices.size() / 3);
    MLOG("xrCreateTriangleMeshFB(%zu triangles of the room mesh) -> XR_SUCCESS", st.fb.entities.back().mesh_indices.size() / 3);
    MWARN("xrLocateSpace(META 03de305d) -> XR_SUCCESS, flags rot+T pos+T");
    MERR("  xrGetSpaceBoundingBox3DFB(09de305d) -> XR_ERROR_SPACE_COMPONENT_NOT_ENABLED_FB");
    MLOG("event BOUNDARY_VISIBILITY_CHANGED_META -> suppressed");
    MLOG("xrCreateSpatialAnchorFB(at -0.60 0.75 -1.25) -> XR_SUCCESS request 9");
    MLOG("event SPATIAL_ANCHOR_CREATE_COMPLETE_FB request 9 -> XR_SUCCESS, anchor 0dde305d");
    MLOG("xrSaveSpaceFB(0dde305d, LOCAL) -> XR_SUCCESS request 10");
    MLOG("event SPACE_SAVE_COMPLETE_FB request 10 0dde305d -> XR_SUCCESS");

    ViewOptions view;
    SceneData d;
    d.mr = &st;
    d.view = &view;
    d.panel.session_state = "FOCUSED";
    d.panel.log_file = "mr-log.txt in the app's files folder";

    // the user stands in the room, facing the table wall
    Vec3 eye(0.35f, 1.62f, 1.35f);
    Pose user(Quat::axis_angle({0, 1, 0}, deg(18)), eye);
    scene.layout(user);
    // a step behind the user, so the panel and the room are both in view
    Vec3 cam_at = user.apply({0.3f, 0.08f, 0.4f});
    Pose cam = look_at(cam_at, user.apply({0.25f, -0.6f, -1.6f}));
    float hfov = 44, vfov = 36;  // half angles
    bool panel_close = shot == "runtime" || shot == "roomtab" || shot == "anchors" || shot == "log";
    if (shot == "room" || shot == "both" || shot == "wire") {
        d.show_panel = shot == "room";
        if (shot == "both") view.source = kSourceBoth, view.mesh = kMeshOff;
        if (shot == "wire") view.mesh = kMeshWire, view.labels = false;
        // right controller pointing at the panel's "Ask again" button
        d.aim_valid[1] = d.grip_valid[1] = true;
        d.grip[1] = Pose(Quat::axis_angle({1, 0, 0}, deg(-40)), user.apply({0.18f, -0.35f, -0.35f}));
        Vec3 from = d.grip[1].p + Vec3(0, 0.02f, -0.04f), to = scene.panel.pose.apply({-0.12f, 0.2f, 0});
        d.aim[1] = look_at(from, to);
        d.panel.hover = kBtnRefresh;
        if (shot != "room") cam = look_at(eye, eye + Vec3(-0.6f, -0.35f, -1.0f)), d.aim_valid[1] = d.grip_valid[1] = false;
        else eye = cam_at;
    } else if (shot == "overview") {
        d.show_panel = false;
        eye = Vec3(2.0f, 2.35f, 1.05f);
        cam = look_at(eye, Vec3(-0.6f, 0.2f, -1.2f));
        hfov = 48, vfov = 40;
    } else if (shot == "icon") {
        // the room's outline and furniture seen from above a corner, lines only, on a transparent background
        d.show_panel = false;
        view.labels = view.gizmos = view.boundary = false;
        view.mesh = kMeshOff;
        st.anchors.entities.clear();
        for (RoomSet *set : {&st.fb, &st.meta}) {
            std::vector<Entity> keep;
            for (const Entity &e : set->entities)
                if (e.labels != "CEILING" && e.labels.find("WALL") == std::string::npos && e.labels != "SCREEN" &&
                    e.labels != "WALL_ART" && e.labels != "WINDOW_FRAME" && e.labels != "DOOR_FRAME")
                    keep.push_back(e);
            set->entities = keep;
        }
        d.line_scale = 4.0f;
        eye = Vec3(3.4f, 4.6f, 3.6f);
        cam = look_at(eye, Vec3(0.1f, 0.3f, -0.2f));
        hfov = 27;
    } else if (panel_close) {
        view.tab = shot == "runtime" ? kTabRuntime : shot == "roomtab" ? kTabRoom : shot == "anchors" ? kTabAnchors : kTabLog;
        Vec3 c = scene.panel.pose.p, nrm = scene.panel.pose.q.rotate({0, 0, 1});
        eye = c + nrm * 0.62f;
        cam = look_at(eye, c);
        d.panel.hover = shot == "anchors" ? kBtnSaveMeta : -1;
    }
    d.head = Pose(cam.q, eye);

    glViewport(0, 0, W, H);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (shot == "icon") glClearColor(0, 0, 0, 0);
    else glClearColor(kClearColor[0], kClearColor[1], kClearColor[2], 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    float ax = std::tan(deg(hfov)), ay = ax * H / W;
    (void)vfov;
    XrFovf fov{-std::atan(ax), std::atan(ax), std::atan(ay), -std::atan(ay)};
    Mat4 view_m = Mat4::from_pose(cam.inverse());
    scene.draw(Mat4::projection(fov, 0.05f, 50.0f) * view_m, d);

    std::vector<unsigned char> px(W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    std::ofstream f(out, std::ios::binary);
    const int channels = shot == "icon" ? 4 : 3;  // the icon keeps its alpha (PAM)
    if (channels == 4)
        f << "P7\nWIDTH " << w << "\nHEIGHT " << h << "\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n";
    else
        f << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y)
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < channels; ++c) {
                int sum = 0;
                for (int sy = 0; sy < ss; ++sy)
                    for (int sx = 0; sx < ss; ++sx) sum += px[((y * ss + sy) * W + x * ss + sx) * 4 + c];
                f.put((char)(sum / (ss * ss)));
            }
        }
    std::printf("wrote %s\n", out.c_str());
    return 0;
}
