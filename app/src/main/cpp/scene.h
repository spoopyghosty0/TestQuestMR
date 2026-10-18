// What the app draws: the room as the runtime describes it (planes, volumes, mesh, anchors, STAGE bounds), labels,
// the pointer rays, and the panel into its own texture (a quad layer on the headset). Shared by the app and the PC
// renderer (tools/render_host).
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
    // Draws the room, labels and rays for one eye into the bound framebuffer (cleared by the caller); not the panel.
    void draw(const Mat4 &view_proj, SceneData &d);
    // Panel texture size for kPanelTexelsPerUnit.
    int panel_texture_width() const { return (int)(panel.width * kPanelTexelsPerUnit); }
    int panel_texture_height() const { return (int)(panel.height * kPanelTexelsPerUnit); }
    // Draws the panel and the pointer cursors on it into the bound framebuffer (w x h texels; cleared here to
    // transparent; premultiplied alpha).
    void render_panel(SceneData &d, int w, int h);
    // Draws the panel texture where it hangs, over everything: what the compositor does with the quad layer (the PC
    // renderer).
    void composite_panel(const Mat4 &view_proj, GLuint texture);

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
