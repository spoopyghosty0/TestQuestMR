#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>

#include "mrlog.h"
#include "stb_easy_font.h"

namespace {
const float kBg[4] = {0.012f, 0.014f, 0.02f, 1.0f};
const float kRow[4] = {0.035f, 0.04f, 0.058f, 1};
const float kRowAlt[4] = {0.022f, 0.025f, 0.036f, 1};
const float kHover[4] = {0.07f, 0.11f, 0.2f, 1};
const float kOn[4] = {0.40f, 0.12f, 0.01f, 1};
const float kPressed[4] = {0.75f, 0.3f, 0.05f, 1};
const float kText[4] = {0.86f, 0.86f, 0.89f, 1};
const float kDim[4] = {0.38f, 0.39f, 0.44f, 1};
const float kHead[4] = {1.0f, 0.45f, 0.08f, 1};
const float kBlue[4] = {0.35f, 0.62f, 1.0f, 1};
const float kGood[4] = {0.35f, 0.88f, 0.45f, 1};
const float kBad[4] = {1.0f, 0.35f, 0.3f, 1};
const float kWarn[4] = {1.0f, 0.78f, 0.25f, 1};
const float kRule[4] = {0.12f, 0.13f, 0.17f, 1};

const float *level_color(int level) { return level == 1 ? kGood : level == 2 ? kBad : kText; }

// stb_easy_font emits quads of 4 vertices: x, y, z, packed colour (16 bytes each)
struct StbVertex {
    float x, y, z;
    unsigned char c[4];
};

void emit_quads(std::vector<FlatVertex> &t, const std::string &s, const float color[4],
                const std::function<Vec3(float, float)> &map) {
    static StbVertex buf[12000];
    int quads = stb_easy_font_print(0, 0, const_cast<char *>(s.c_str()), nullptr, buf, sizeof(buf));
    for (int q = 0; q < quads; ++q) {
        const StbVertex *v = &buf[q * 4];
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k : order) {
            Vec3 w = map(v[k].x, v[k].y);
            t.push_back({w.x, w.y, w.z, color[0], color[1], color[2], color[3]});
        }
    }
}

std::string fmt(const char *f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char *f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

const char *component_short(XrSpaceComponentTypeFB t) {
    switch (t) {
        case XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB: return "loc";
        case XR_SPACE_COMPONENT_TYPE_STORABLE_FB: return "sto";
        case XR_SPACE_COMPONENT_TYPE_SHARABLE_FB: return "sha";
        case XR_SPACE_COMPONENT_TYPE_BOUNDED_2D_FB: return "2D";
        case XR_SPACE_COMPONENT_TYPE_BOUNDED_3D_FB: return "3D";
        case XR_SPACE_COMPONENT_TYPE_SEMANTIC_LABELS_FB: return "lbl";
        case XR_SPACE_COMPONENT_TYPE_ROOM_LAYOUT_FB: return "room";
        case XR_SPACE_COMPONENT_TYPE_SPACE_CONTAINER_FB: return "cont";
        case XR_SPACE_COMPONENT_TYPE_TRIANGLE_MESH_META: return "mesh";
        default: return "?";
    }
}
}  // namespace

float text_width(const std::string &s) { return (float)stb_easy_font_width(const_cast<char *>(s.c_str())); }

std::string fit_text(const std::string &s, float width, float scale) {
    if (text_width(s) * scale <= width) return s;
    std::string out = s;
    while (!out.empty() && text_width(out + "...") * scale > width) out.pop_back();
    return out + "...";
}

Vec3 Panel::to_world(float u, float v, float z) const {
    return pose.apply(Vec3((u - width / 2) * px, (height / 2 - v) * px, z));
}

void Panel::rect(std::vector<FlatVertex> &t, float x0, float y0, float x1, float y1, const float c[4], float z) const {
    Vec3 a = to_world(x0, y0, z), b = to_world(x1, y0, z), cc = to_world(x1, y1, z), d = to_world(x0, y1, z);
    for (const Vec3 &p : {a, b, cc, a, cc, d}) t.push_back({p.x, p.y, p.z, c[0], c[1], c[2], c[3]});
}

float Panel::text(std::vector<FlatVertex> &t, float x, float y, float scale, const std::string &s,
                  const float c[4]) const {
    emit_quads(t, s, c, [&](float gx, float gy) { return to_world(x + gx * scale, y + gy * scale, 0.002f); });
    return x + text_width(s) * scale;
}

float Panel::button(std::vector<FlatVertex> &t, const PanelInfo &info, int id, float x, float y,
                    const std::string &label, bool on, float min_w) {
    float w = std::max(min_w, text_width(label) + 16);
    PanelItem it{id, x, y, x + w, y + 16};
    items_.push_back(it);
    const float *bg = info.pressed == id ? kPressed : info.hover == id ? kHover : on ? kOn : kRow;
    rect(t, it.x0, it.y0, it.x1, it.y1, bg, 0.001f);
    text(t, x + (w - text_width(label)) / 2, y + 4, 1, label, kText);
    return x + w + 6;
}

void Panel::build(const PanelInfo &info, std::vector<FlatVertex> &t) {
    items_.clear();
    const MrState &mr = *info.mr;
    const ViewOptions &view = *info.view;
    rect(t, 0, 0, width, height, kBg, 0);

    float x = text(t, 16, 12, 2.0f, "Test Quest MR", kHead);
    if (mr.demo) text(t, x + 12, 18, 1.2f, "DEMO DATA (synthetic room)", kWarn);
    std::string right = info.session_state.empty() ? "" : "session " + info.session_state;
    text(t, width - 16 - text_width(right), 18, 1, right, kDim);
    text(t, 16, 38, 1,
         fit_text("Runtime: " + mr.runtime + "    System: " + mr.system + "    " + mr.instance, width - 32), kText);

    // tabs
    float y = 54;
    const char *tabs[] = {"Runtime & extensions", "Room", "Anchors", "Log"};
    x = 12;
    for (int i = 0; i < kTabCount; ++i) {
        std::string label = tabs[i];
        if (i == kTabLog && MrLog::get().errors()) label += fmt("  (%d error%s)", MrLog::get().errors(), MrLog::get().errors() == 1 ? "" : "s");
        x = button(t, info, kBtnTab + i, x, y, label, view.tab == i, 120);
    }
    y += 22;
    rect(t, 12, y, width - 12, y + 1, kRule, 0.001f);
    y += 6;

    // view toggles
    x = text(t, 16, y + 4, 1, "Show", kDim) + 10;
    x = button(t, info, kBtnPlanes, x, y, "Planes", view.planes);
    x = button(t, info, kBtnVolumes, x, y, "Volumes", view.volumes);
    x = button(t, info, kBtnMesh, x, y, std::string("Mesh: ") + mesh_mode_name(view.mesh), view.mesh != kMeshOff);
    x = button(t, info, kBtnLabels, x, y, "Labels", view.labels);
    x = button(t, info, kBtnAxes, x, y, "Axes", view.gizmos);
    x = button(t, info, kBtnBoundary, x, y, "STAGE bounds", view.boundary);
    button(t, info, kBtnPassthrough, x, y, "Passthrough", view.passthrough);
    y += 22;
    x = text(t, 16, y + 4, 1, "From", kDim) + 10;
    x = button(t, info, kBtnSource, x, y, std::string("Source: ") + source_mode_name(view.source), true, 150);
    x = button(t, info, kBtnRefresh, x, y, "Ask again");
    x = button(t, info, kBtnCapture, x, y, "Scene capture");
    x = button(t, info, kBtnBoundaryVis, x, y,
               mr.boundary_suppress_requested ? "Boundary: show" : "Boundary: hide");
    button(t, info, kBtnRecenter, x, y, "Recenter panel");
    y += 22;
    if (!info.message.empty()) text(t, 16, y + 2, 1, fit_text(info.message, width - 32), kWarn);
    y += 14;
    rect(t, 12, y, width - 12, y + 1, kRule, 0.001f);
    y += 8;

    switch (view.tab) {
        case kTabRuntime: runtime_tab(t, info, y); break;
        case kTabRoom: room_tab(t, info, y); break;
        case kTabAnchors: anchors_tab(t, info, y); break;
        default: log_tab(t, info, y); break;
    }
}

void Panel::runtime_tab(std::vector<FlatVertex> &t, const PanelInfo &info, float y) {
    const MrState &mr = *info.mr;
    float y0 = y;
    text(t, 16, y, 1.3f, "Extensions", kHead);
    text(t, 290, y + 3, 1, "runtime", kDim);
    text(t, 350, y + 3, 1, "app", kDim);
    y += 18;
    for (size_t i = 0; i < mr.extensions.size(); ++i) {
        const ExtRow &e = mr.extensions[i];
        if (i % 2) rect(t, 12, y - 2, 430, y + 10, kRowAlt, 0.001f);
        text(t, 16, y, 1, e.name, e.listed ? kText : kDim);
        text(t, 290, y, 1, e.listed ? fmt("v%u", e.version) : "-", e.listed ? kGood : kDim);
        bool listed_only = e.note.find("listed only") != std::string::npos;
        text(t, 350, y, 1, e.enabled ? "enabled" : listed_only ? "" : e.listed ? "OFF" : "-",
             e.enabled ? kGood : e.listed && !listed_only ? kBad : kDim);
        y += 12;
    }
    y += 6;
    text(t, 16, y, 1, "Grey: the runtime doesn't list it.", kDim);

    // right column: what the system and session said
    float x = 450;
    y = y0;
    text(t, x, y, 1.3f, "System and spaces", kHead);
    y += 18;
    auto row = [&](const std::string &label, const std::string &value, const float *color) {
        text(t, x, y, 1, label, kDim);
        text(t, x + 200, y, 1, fit_text(value, width - x - 216), color);
        y += 12;
    };
    for (const Fact &f : mr.facts) row(f.label, f.value, level_color(f.level));
    y += 6;
    text(t, x, y, 1.3f, "Boundary", kHead);
    y += 18;
    row("xrGetReferenceSpaceBoundsRect", mr.stage_bounds_result == XR_SUCCESS
                                              ? fmt("%.2f x %.2f m", mr.stage_bounds.width, mr.stage_bounds.height)
                                              : xr_result_str(mr.stage_bounds_result),
        mr.stage_bounds_result == XR_SUCCESS ? kGood : kBad);
    row("STAGE origin", mr.stage_located ? fmt("located (%.2f, %.2f, %.2f)", mr.stage_pose.p.x, mr.stage_pose.p.y,
                                               mr.stage_pose.p.z)
                                         : (mr.stage_space ? "not located" : "no STAGE space"),
        mr.stage_located ? kText : kBad);
    row("Visibility (event)", mr.boundary_visibility, kText);
    row("Last request", mr.boundary_request_result == kNotCalled
                            ? "-"
                            : std::string(mr.boundary_suppress_requested ? "hide -> " : "show -> ") +
                                  xr_result_str(mr.boundary_request_result),
        XR_FAILED(mr.boundary_request_result) && mr.boundary_request_result != kNotCalled ? kBad : kText);
    y += 6;
    text(t, x, y, 1.3f, "Other", kHead);
    y += 18;
    row("Scene permission", mr.permission.empty() ? "-" : mr.permission,
        mr.permission.rfind("granted", 0) == 0 ? kGood : mr.permission.empty() ? kText : kBad);
    row("Passthrough", mr.passthrough.empty() ? "-" : mr.passthrough, kText);
    row("Scene capture", mr.capture_status.empty() ? "-" : mr.capture_status, kText);
    row("XR_FB_triangle_mesh", mr.triangle_mesh_status.empty() ? "-" : mr.triangle_mesh_status,
        mr.triangle_mesh_status.find("SUCCESS") != std::string::npos ? kGood : kText);
}

void Panel::room_tab(std::vector<FlatVertex> &t, const PanelInfo &info, float y) {
    const MrState &mr = *info.mr;
    const ViewOptions &view = *info.view;
    auto status = [&](const RoomSet &s, const char *label) {
        float x = text(t, 16, y, 1, label, kDim);
        x = std::max(x + 8, 90.0f);
        std::string age = s.updated >= 0 ? fmt("  (%.0f s ago)", MrLog::get().clock() - s.updated) : "";
        if (mr.demo) age.clear();
        text(t, x, y, 1, fit_text(s.status + age, width - x - 16), level_color(s.status_level));
        y += 12;
    };
    status(mr.fb, "FB query");
    status(mr.meta, "META disc.");
    status(mr.ext, "EXT planes");

    // FB vs META: the same spaces?
    {
        int same = 0, only_fb = 0, only_meta = 0;
        float worst = 0;
        for (const Entity &e : mr.fb.entities) {
            int j = e.has_uuid ? mr.meta.find(e.uuid) : -1;
            if (j < 0) {
                ++only_fb;
                continue;
            }
            ++same;
            const Entity &m = mr.meta.entities[j];
            if (e.located && m.located) worst = std::max(worst, length(e.pose.p - m.pose.p));
        }
        for (const Entity &m : mr.meta.entities)
            if (!m.has_uuid || mr.fb.find(m.uuid) < 0) ++only_meta;
        std::string cmp = fmt("FB vs META: %d same UUIDs, %d only FB, %d only META, largest position difference %.3f m",
                              same, only_fb, only_meta, worst);
        bool good = only_fb == 0 && only_meta == 0 && worst < 0.01f && same > 0;
        text(t, 16, y, 1, cmp, good ? kGood : (same || only_fb || only_meta) ? kWarn : kDim);
        y += 16;
    }

    const RoomSet &set = view.source == kSourceMeta ? mr.meta : view.source == kSourceExt ? mr.ext : mr.fb;
    float x = text(t, 16, y, 1.3f, "Spaces from " + set.name, kHead);
    (void)x;
    float bx = width - 16 - 2 * 56;
    button(t, info, kBtnUp, bx, y - 3, "Up", false, 50);
    button(t, info, kBtnDown, bx + 56, y - 3, "Down", false, 50);
    y += 18;
    const float cols[] = {16, 92, 232, 400, 494, 600, 670, 750};
    const char *heads[] = {"role", "labels", "components", "2D (m)", "3D (m)", "mesh tris", "located", "uuid / failures"};
    for (int c = 0; c < 8; ++c) text(t, cols[c], y, 1, heads[c], kDim);
    y += 13;
    float bottom = height - 14;
    rows_visible_ = std::max(1, (int)((bottom - y) / 12));
    int first = std::min(view.room_scroll, std::max(0, (int)set.entities.size() - rows_visible_));
    for (int i = first; i < (int)set.entities.size() && y < bottom - 12; ++i) {
        const Entity &e = set.entities[i];
        if ((i - first) % 2) rect(t, 12, y - 2, width - 12, y + 10, kRowAlt, 0.001f);
        const float *c = e.failures ? kBad : kText;
        text(t, cols[0], y, 1, fit_text(e.role.empty() ? "-" : e.role, cols[1] - cols[0] - 6), c);
        text(t, cols[1], y, 1, fit_text(e.labels.empty() ? "-" : e.labels, cols[2] - cols[1] - 6), c);
        std::string comps;
        for (XrSpaceComponentTypeFB ct : e.components) comps += std::string(comps.empty() ? "" : " ") + component_short(ct);
        text(t, cols[2], y, 1, fit_text(comps.empty() ? "-" : comps, cols[3] - cols[2] - 6), kText);
        text(t, cols[3], y, 1, e.has_b2d ? fmt("%.2f x %.2f", e.b2d.extent.width, e.b2d.extent.height) : "-", kText);
        text(t, cols[4], y, 1,
             e.has_b3d ? fmt("%.2fx%.2fx%.2f", e.b3d.extent.width, e.b3d.extent.height, e.b3d.extent.depth) : "-", kText);
        text(t, cols[5], y, 1, e.mesh_indices.empty() ? "-" : fmt("%zu", e.mesh_indices.size() / 3), kText);
        text(t, cols[6], y, 1, e.located ? "yes" : xr_result_str(e.locate_result), e.located ? kGood : kBad);
        std::string last = e.has_uuid ? uuid_hex(e.uuid).substr(0, 8) : "-";
        for (const CallResult &cr : e.calls)
            if (XR_FAILED(cr.result)) {
                last += " " + cr.call.substr(2) + ": " + xr_result_str(cr.result);
                break;
            }
        text(t, cols[7], y, 1, fit_text(last, width - cols[7] - 16), e.failures ? kBad : kDim);
        y += 12;
    }
    if (set.entities.empty()) text(t, 16, y, 1, "(no spaces)", kDim);
    std::string foot = fmt("%zu spaces", set.entities.size());
    if ((int)set.entities.size() > rows_visible_) foot += fmt(", showing %d-%d (thumbstick or Up/Down scrolls)", first + 1,
                                                             std::min((int)set.entities.size(), first + rows_visible_));
    text(t, 16, height - 14, 1, foot, kDim);
}

void Panel::anchors_tab(std::vector<FlatVertex> &t, const PanelInfo &info, float y) {
    const MrState &mr = *info.mr;
    text(t, 16, y, 1.3f, "Spatial anchors", kHead);
    y += 18;
    text(t, 16, y, 1, "Squeeze a grip button: xrCreateSpatialAnchorFB at that controller.", kText);
    y += 18;
    float x = text(t, 16, y + 4, 1, "Save", kDim) + 10;
    x = button(t, info, kBtnSaveFb, x, y, "xrSaveSpaceFB");
    x = button(t, info, kBtnSaveMeta, x, y, "xrSaveSpacesMETA");
    x = text(t, x + 14, y + 4, 1, "Erase", kDim) + 10;
    x = button(t, info, kBtnEraseFb, x, y, "xrEraseSpaceFB");
    button(t, info, kBtnEraseMeta, x, y, "xrEraseSpacesMETA");
    y += 22;
    x = text(t, 16, y + 4, 1, "Load", kDim) + 10;
    x = button(t, info, kBtnLoadFb, x, y, "xrQuerySpacesFB (saved UUIDs)");
    x = button(t, info, kBtnLoadMeta, x, y, "xrDiscoverSpacesMETA (saved UUIDs)");
    button(t, info, kBtnClear, x + 14, y, "Drop from view");
    y += 24;
    text(t, 16, y, 1, fit_text(mr.anchors.status, width - 32), level_color(mr.anchors.status_level));
    y += 18;
    const float cols[] = {16, 260, 420, 520, 700};
    const char *heads[] = {"uuid", "state", "located", "position (LOCAL, m)", "components"};
    for (int c = 0; c < 5; ++c) text(t, cols[c], y, 1, heads[c], kDim);
    y += 13;
    for (size_t i = 0; i < mr.anchors.entities.size() && y < height - 24; ++i) {
        const Entity &e = mr.anchors.entities[i];
        if (i % 2) rect(t, 12, y - 2, width - 12, y + 10, kRowAlt, 0.001f);
        text(t, cols[0], y, 1, e.has_uuid ? uuid_hex(e.uuid) : "-", kText);
        text(t, cols[1], y, 1, fit_text(e.anchor_state, cols[2] - cols[1] - 6),
             e.anchor_state.find("fail") != std::string::npos ? kBad : kText);
        text(t, cols[2], y, 1, e.located ? "yes" : xr_result_str(e.locate_result), e.located ? kGood : kBad);
        text(t, cols[3], y, 1, e.located ? fmt("%+.2f %+.2f %+.2f", e.pose.p.x, e.pose.p.y, e.pose.p.z) : "-", kText);
        std::string comps;
        for (XrSpaceComponentTypeFB ct : e.components) comps += std::string(comps.empty() ? "" : " ") + component_short(ct);
        text(t, cols[4], y, 1, fit_text(comps, width - cols[4] - 16), kDim);
        y += 12;
    }
    text(t, 16, height - 14, 1, "Saved UUIDs are kept in anchors.txt in the app's files folder, for loading after a restart.",
         kDim);
}

void Panel::log_tab(std::vector<FlatVertex> &t, const PanelInfo &info, float y) {
    const ViewOptions &view = *info.view;
    const auto &lines = MrLog::get().lines();
    text(t, 16, y, 1.3f, "Every MR call, its result and every event", kHead);
    float bx = width - 16 - 2 * 56;
    button(t, info, kBtnUp, bx, y - 3, "Older", false, 50);
    button(t, info, kBtnDown, bx + 56, y - 3, "Newer", false, 50);
    y += 20;
    float bottom = height - 16;
    rows_visible_ = std::max(1, (int)((bottom - y) / 11));
    int last = (int)lines.size() - 1 - std::max(0, view.log_scroll);
    int first = std::max(0, last - rows_visible_ + 1);
    for (int i = first; i <= last; ++i) {
        const LogLine &l = lines[i];
        const float *c = l.level == 2 ? kBad : l.level == 1 ? kWarn : (l.text.rfind("event", 0) == 0 ? kBlue : kText);
        text(t, 16, y, 1, fmt("%7.2f", l.t), kDim);
        text(t, 66, y, 1, fit_text(l.text, width - 82), c);
        y += 11;
    }
    std::string foot = fmt("%d lines", MrLog::get().total());
    if (!info.log_file.empty()) foot += "   file: " + info.log_file;
    text(t, 16, height - 14, 1, fit_text(foot, width - 32), kDim);
}

bool Panel::hit(const Pose &aim, float &u, float &v, Vec3 &world) const {
    Vec3 origin = aim.p, dir = aim.q.rotate({0, 0, -1});
    Vec3 n = pose.q.rotate({0, 0, 1});
    float denom = dot(dir, n);
    if (std::fabs(denom) < 1e-4f) return false;
    float d = dot(pose.p - origin, n) / denom;
    if (d < 0 || d > 5) return false;
    world = origin + dir * d;
    Vec3 local = pose.inverse().apply(world);
    u = local.x / px + width / 2;
    v = height / 2 - local.y / px;
    return u >= 0 && u <= width && v >= 0 && v <= height;
}

int Panel::item_at(float u, float v) const {
    for (const PanelItem &it : items_)
        if (u >= it.x0 && u <= it.x1 && v >= it.y0 && v <= it.y1) return it.id;
    return -1;
}

void world_label(std::vector<FlatVertex> &tris, const Pose &pose, float m, const std::vector<std::string> &lines,
                 const float color[4], const float plate[4]) {
    float w = 0;
    for (const std::string &l : lines) w = std::max(w, text_width(l));
    const float line_h = 11;
    float h = line_h * lines.size();
    // plate behind the text, slightly larger
    float x0 = -w / 2 - 4, x1 = w / 2 + 4, y0 = -3, y1 = h + 1;
    auto at = [&](float gx, float gy, float z) { return pose.apply(Vec3(gx * m, (h / 2 - gy) * m, z)); };
    Vec3 a = at(x0, y0, -0.001f), b = at(x1, y0, -0.001f), c = at(x1, y1, -0.001f), d = at(x0, y1, -0.001f);
    for (const Vec3 &p : {a, b, c, a, c, d}) tris.push_back({p.x, p.y, p.z, plate[0], plate[1], plate[2], plate[3]});
    for (size_t i = 0; i < lines.size(); ++i) {
        float lw = text_width(lines[i]);
        const float *col = color;
        static const float kSub[4] = {0.75f, 0.77f, 0.82f, 1};
        if (i > 0) col = kSub;
        emit_quads(tris, lines[i], col, [&](float gx, float gy) { return at(gx - lw / 2, gy + i * line_h, 0); });
    }
}
