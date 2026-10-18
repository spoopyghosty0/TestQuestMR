// What the app draws: the room as the runtime describes it (planes, volumes, mesh, anchors, STAGE bounds), labels,
// the panel and the pointer rays. Shared by the app and the PC renderer (tools/render_host).
#pragma once

#include <map>
#include <string>

#include "gfx.h"
#include "state.h"
#include "ui.h"

// Background without passthrough (linear RGB)
constexpr float kClearColor[3] = {0.03f, 0.034f, 0.045f};

struct SceneData {
    const MrState *mr = nullptr;
    const ViewOptions *view = nullptr;
    Pose head;  // centre eye in the base space: labels turn to it, lines face it
    bool aim_valid[2] = {false, false}, grip_valid[2] = {false, false};
    Pose aim[2], grip[2];
    bool show_panel = true;
    float line_scale = 1.0f;  // thicker lines (the app icon)
    PanelInfo panel;
};

class Scene {
public:
    Renderer renderer;
    Panel panel;

    bool init() { return renderer.init(); }
    // Places the panel in front of `user` (a pose with yaw only); call before hit tests and draw().
    void layout(const Pose &user);
    // Draws everything for one eye into the bound framebuffer (cleared by the caller).
    void draw(const Mat4 &view_proj, SceneData &d);

private:
    struct MeshCache {
        unsigned generation = 0;
        Mesh shaded;
        FlatBuffer wire;
        size_t triangles = 0;
        Vec3 label_at;  // local: above the middle of the mesh, under its top
        bool used = false;
    };
    std::map<std::string, MeshCache> meshes_;
    MeshCache &mesh_for(const std::string &key, const Entity &e);
};

// Colour of a semantic label (first of a comma-separated list), linear RGB.
Vec3 label_color(const std::string &labels);
