// The panel: tabs (Runtime, Room, Anchors, Log), view toggles, opacity sliders and actions. Built in panel units
// (x right, y down) and drawn into the panel's own texture (a quad layer on the headset).
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
    kSliderOverlay = 100, kSliderPanel,  // sliders: point, hold the trigger and drag (or push the thumbstick sideways)
};

struct PanelInfo {
    const MrState *mr = nullptr;
    const ViewOptions *view = nullptr;
    int hover = -1;      // ButtonId under a pointer
    int pressed = -1;    // ButtonId just pressed (flashes)
    int dragging = -1;   // slider being dragged
    std::string message; // one line under the toolbar
    std::string log_file;
    std::string session_state;
};

struct PanelItem {
    int id;
    float x0, y0, x1, y1;  // panel units
    bool slider = false;
    float track0 = 0, track1 = 0;  // a slider's track (0 % .. 100 %)
};

class Panel {
public:
    Pose pose;             // centre of the panel; it faces +Z
    float px = 0.00105f;   // metres per panel unit
    float width = 900, height = 682;

    // Rectangles and glyphs in panel units (z = 0); the glyphs use panel_atlas().
    void build(const PanelInfo &info, std::vector<FlatVertex> &rects, std::vector<TexVertex> &glyphs);
    // Ray from `aim` (along -Z): panel coordinates of the hit and its world point. bounded = false: anywhere on the
    // panel's plane (a slider follows the pointer past the panel's edge).
    bool hit(const Pose &aim, float &u, float &v, Vec3 &world, bool bounded = true) const;
    int item_at(float u, float v) const;  // ButtonId or -1
    const PanelItem *item(int id) const;  // from the last build
    bool is_slider(int id) const { return item(id) && item(id)->slider; }
    float slider_value(int id, float u) const;  // 0..1 for a pointer at u
    int rows_visible() const { return rows_visible_; }

private:
    std::vector<PanelItem> items_;
    std::vector<TexVertex> *glyphs_ = nullptr;
    float alpha_ = 1;  // background opacity while building
    int rows_visible_ = 30;
    void rect(std::vector<FlatVertex> &t, float x0, float y0, float x1, float y1, const float c[4], float z = 0);
    float text(std::vector<FlatVertex> &t, float x, float y, float scale, const std::string &s, const float c[4]);
    float button(std::vector<FlatVertex> &t, const PanelInfo &info, int id, float x, float y, const std::string &label,
                 bool on = false, float min_w = 0);
    float slider(std::vector<FlatVertex> &t, const PanelInfo &info, int id, float x, float y, const std::string &label,
                 int percent, float track_w);
    void runtime_tab(std::vector<FlatVertex> &t, const PanelInfo &info, float y);
    void room_tab(std::vector<FlatVertex> &t, const PanelInfo &info, float y);
    void anchors_tab(std::vector<FlatVertex> &t, const PanelInfo &info, float y);
    void log_tab(std::vector<FlatVertex> &t, const PanelInfo &info, float y);
};

// Text in world space on the plane of `pose` (x right, y up, facing +Z), centred on its position, on a dark plate;
// the glyphs use label_atlas().
void world_label(std::vector<FlatVertex> &plates, std::vector<TexVertex> &glyphs, const Pose &pose,
                 float metres_per_unit, const std::vector<std::string> &lines, const float color[4], const float plate[4]);
// Truncates `s` to fit `width` units at `scale` (adds "...").
std::string fit_text(const std::string &s, float width, float scale = 1.0f);
float text_width(const std::string &s);
