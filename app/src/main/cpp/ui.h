// The panel: tabs (Runtime, Room, Anchors, Log), view toggles and actions, drawn as flat geometry.
#pragma once

#include <string>
#include <vector>

#include "gfx.h"
#include "state.h"

enum ButtonId {
    kBtnTab = 0,  // + Tab
    kBtnPlanes = 10, kBtnVolumes, kBtnMesh, kBtnLabels, kBtnBoundary, kBtnAxes, kBtnPassthrough,
    kBtnSource, kBtnRefresh, kBtnCapture, kBtnBoundaryVis, kBtnRecenter,
    kBtnSaveFb, kBtnSaveMeta, kBtnEraseFb, kBtnEraseMeta, kBtnLoadFb, kBtnLoadMeta, kBtnClear,
    kBtnUp, kBtnDown,
};

struct PanelInfo {
    const MrState *mr = nullptr;
    const ViewOptions *view = nullptr;
    int hover = -1;      // ButtonId under a pointer
    int pressed = -1;    // ButtonId just pressed (flashes)
    std::string message; // one line under the toolbar
    std::string log_file;
    std::string session_state;
};

struct PanelItem {
    int id;
    float x0, y0, x1, y1;  // panel pixels
};

class Panel {
public:
    Pose pose;             // centre of the panel; it faces +Z
    float px = 0.00105f;   // metres per panel pixel
    float width = 900, height = 660;

    void build(const PanelInfo &info, std::vector<FlatVertex> &tris);
    // Ray from `aim` (along -Z): panel pixel coordinates of the hit and its world point.
    bool hit(const Pose &aim, float &u, float &v, Vec3 &world) const;
    int item_at(float u, float v) const;  // ButtonId or -1
    int rows_visible() const { return rows_visible_; }

private:
    std::vector<PanelItem> items_;
    int rows_visible_ = 30;
    Vec3 to_world(float u, float v, float z = 0) const;
    void rect(std::vector<FlatVertex> &t, float x0, float y0, float x1, float y1, const float c[4], float z) const;
    float text(std::vector<FlatVertex> &t, float x, float y, float scale, const std::string &s, const float c[4]) const;
    float button(std::vector<FlatVertex> &t, const PanelInfo &info, int id, float x, float y, const std::string &label,
                 bool on = false, float min_w = 0);
    void runtime_tab(std::vector<FlatVertex> &t, const PanelInfo &info, float y);
    void room_tab(std::vector<FlatVertex> &t, const PanelInfo &info, float y);
    void anchors_tab(std::vector<FlatVertex> &t, const PanelInfo &info, float y);
    void log_tab(std::vector<FlatVertex> &t, const PanelInfo &info, float y);
};

// Text in world space on the plane of `pose` (x right, y up, facing +Z), centred on its position, on a dark plate.
void world_label(std::vector<FlatVertex> &tris, const Pose &pose, float metres_per_px, const std::vector<std::string> &lines,
                 const float color[4], const float plate[4]);
// Truncates `s` to fit `width` pixels at `scale` (adds "...").
std::string fit_text(const std::string &s, float width, float scale = 1.0f);
float text_width(const std::string &s);
