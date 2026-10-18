#include "scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>

namespace {

struct LabelColor {
    const char *label;
    Vec3 color;
};
const LabelColor kLabelColors[] = {
    {"FLOOR", {0.08f, 0.62f, 0.38f}},       {"CEILING", {0.42f, 0.38f, 0.75f}},
    {"WALL_FACE", {0.12f, 0.36f, 1.0f}},    {"INVISIBLE_WALL_FACE", {0.5f, 0.52f, 0.58f}},
    {"TABLE", {1.0f, 0.42f, 0.04f}},        {"DESK", {1.0f, 0.42f, 0.04f}},
    {"COUCH", {0.62f, 0.2f, 0.95f}},        {"BED", {0.95f, 0.22f, 0.45f}},
    {"STORAGE", {0.72f, 0.4f, 0.12f}},      {"SCREEN", {0.04f, 0.82f, 0.92f}},
    {"LAMP", {1.0f, 0.86f, 0.15f}},         {"PLANT", {0.3f, 0.88f, 0.12f}},
    {"DOOR_FRAME", {1.0f, 0.16f, 0.12f}},   {"WINDOW_FRAME", {0.45f, 0.78f, 1.0f}},
    {"WALL_ART", {1.0f, 0.42f, 0.78f}},     {"GLOBAL_MESH", {0.8f, 0.82f, 0.86f}},
    {"WALL", {0.12f, 0.36f, 1.0f}},         {"PLATFORM", {1.0f, 0.42f, 0.04f}},  // EXT plane detection
};
const Vec3 kAnchorColor(1.0f, 0.82f, 0.2f), kStageColor(0.1f, 0.9f, 0.85f), kMetaOverlay(1.0f, 0.3f, 0.95f);

struct Batches {
    std::vector<FlatVertex> solid;  // opaque lines (ribbons), depth-written
    std::vector<FlatVertex> fill;   // translucent faces, not depth-written
    std::vector<FlatVertex> text;   // labels, always on top
    Vec3 eye;
    float scale = 1.0f;
};

FlatVertex fv(const Vec3 &p, const Vec3 &c, float a) { return {p.x, p.y, p.z, c.x, c.y, c.z, a}; }

// A line as a flat ribbon turned towards the eye: visible width in metres instead of one aliased pixel.
void ribbon(Batches &b, const Vec3 &p, const Vec3 &q, float width, const Vec3 &c, float a) {
    Vec3 d = q - p;
    if (length(d) < 1e-5f) return;
    Vec3 side = cross(d, (p + q) * 0.5f - b.eye);
    if (length(side) < 1e-6f) side = cross(d, Vec3(0, 1, 0));
    side = normalize(side) * (width * 0.5f * b.scale);
    Vec3 v[4] = {p - side, p + side, q + side, q - side};
    for (int k : {0, 1, 2, 0, 2, 3}) b.solid.push_back(fv(v[k], c, a));
}

void dashed(Batches &b, const Vec3 &p, const Vec3 &q, float width, const Vec3 &c, float a, float dash = 0.08f) {
    float len = length(q - p);
    int n = std::max(1, (int)(len / dash));
    for (int i = 0; i < n; i += 2) ribbon(b, lerp(p, q, (float)i / n), lerp(p, q, std::min(1.0f, (float)(i + 1) / n)), width, c, a);
}

void gizmo(Batches &b, const Pose &pose, float size, float width) {
    ribbon(b, pose.p, pose.apply({size, 0, 0}), width, {1.0f, 0.12f, 0.1f}, 1);
    ribbon(b, pose.p, pose.apply({0, size, 0}), width, {0.15f, 0.95f, 0.2f}, 1);
    ribbon(b, pose.p, pose.apply({0, 0, size}), width, {0.15f, 0.35f, 1.0f}, 1);
}

// Ear clipping of a simple polygon (any winding) into triangles; returns index triples.
std::vector<int> triangulate(const std::vector<XrVector2f> &poly) {
    std::vector<int> out, idx;
    int n = (int)poly.size();
    if (n < 3) return out;
    float area = 0;
    for (int i = 0; i < n; ++i) {
        const XrVector2f &a = poly[i], &c = poly[(i + 1) % n];
        area += a.x * c.y - c.x * a.y;
    }
    for (int i = 0; i < n; ++i) idx.push_back(area > 0 ? i : n - 1 - i);  // counter-clockwise
    auto cross2 = [&](int a, int b2, int c) {
        const XrVector2f &A = poly[a], &B = poly[b2], &C = poly[c];
        return (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
    };
    auto in_tri = [&](int p, int a, int b2, int c) {
        return cross2(a, b2, p) >= 0 && cross2(b2, c, p) >= 0 && cross2(c, a, p) >= 0;
    };
    int guard = 0;
    while (idx.size() > 3 && guard++ < 10000) {
        bool clipped = false;
        int m = (int)idx.size();
        for (int i = 0; i < m; ++i) {
            int a = idx[(i + m - 1) % m], b2 = idx[i], c = idx[(i + 1) % m];
            if (cross2(a, b2, c) <= 0) continue;
            bool ear = true;
            for (int k : idx)
                if (k != a && k != b2 && k != c && in_tri(k, a, b2, c)) {
                    ear = false;
                    break;
                }
            if (!ear) continue;
            out.insert(out.end(), {a, b2, c});
            idx.erase(idx.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) break;  // degenerate: give up on the rest
    }
    if (idx.size() == 3) out.insert(out.end(), {idx[0], idx[1], idx[2]});
    return out;
}

Pose facing(const Vec3 &at, const Vec3 &eye) {
    Vec3 z = eye - at;
    z.y = 0;  // upright labels
    if (length(z) < 1e-4f) z = {0, 0, 1};
    z = normalize(z);
    Vec3 x = normalize(cross({0, 1, 0}, z));
    return Pose(Quat::from_basis(x, {0, 1, 0}, z), at);
}

std::string fmt2(const char *f, float a, float b2) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), f, a, b2);
    return buf;
}

void label(Batches &b, const Vec3 &at, const Vec3 &color, const std::vector<std::string> &lines) {
    float dist = std::max(0.5f, length(at - b.eye));
    float m = 0.0016f * std::min(2.2f, dist);  // further away: a bit bigger, still readable at 4 m
    const float c[4] = {color.x, color.y, color.z, 1}, plate[4] = {0.01f, 0.012f, 0.018f, 0.93f};
    world_label(b.text, facing(at, b.eye), m, lines, c, plate);
}

}  // namespace

Vec3 label_color(const std::string &labels) {
    std::string first = labels.substr(0, labels.find(','));
    for (const LabelColor &l : kLabelColors)
        if (first == l.label) return l.color;
    return {0.7f, 0.7f, 0.72f};
}

void Scene::layout(const Pose &user) {
    panel.px = 0.00105f;  // 900 x 660 px = 0.95 x 0.69 m
    // a little to the left, turned towards the user, slightly below eye height
    const Vec3 at(-0.25f, -0.12f, -0.95f);
    panel.pose = user * Pose(Quat::axis_angle({0, 1, 0}, std::atan2(-at.x, -at.z)), at);
}

Scene::MeshCache &Scene::mesh_for(const std::string &key, const Entity &e) {
    MeshCache &c = meshes_[key];
    c.used = true;
    if (c.generation == e.mesh_generation && !c.shaded.empty()) return c;
    c.generation = e.mesh_generation;
    // smooth normals (averaged over the triangles at each vertex): a scan's small ripples don't turn into patches
    const auto &mv = e.mesh_vertices;
    const auto &mi = e.mesh_indices;
    size_t tris = mi.size() / 3;
    std::vector<Vec3> normals(mv.size());
    for (size_t t = 0; t < tris; ++t) {
        uint32_t a = mi[t * 3], b2 = mi[t * 3 + 1], c3 = mi[t * 3 + 2];
        if (a >= mv.size() || b2 >= mv.size() || c3 >= mv.size()) continue;
        Vec3 n = cross(Vec3(mv[b2]) - Vec3(mv[a]), Vec3(mv[c3]) - Vec3(mv[a]));  // area-weighted
        normals[a] += n, normals[b2] += n, normals[c3] += n;
    }
    Vec3 lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
    for (const XrVector3f &p : mv) {
        lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
        hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    }
    c.label_at = mv.empty() ? Vec3() : Vec3((lo.x + hi.x) / 2, std::max(lo.y, hi.y - 0.45f), (lo.z + hi.z) / 2);
    std::vector<float> v;
    v.reserve(mv.size() * 6);
    for (size_t i = 0; i < mv.size(); ++i) {
        Vec3 n = normalize(normals[i]);
        v.insert(v.end(), {mv[i].x, mv[i].y, mv[i].z, n.x, n.y, n.z});
    }
    std::vector<uint32_t> idx;
    idx.reserve(tris * 3);
    std::vector<FlatVertex> wire;
    wire.reserve(tris * 6);
    const Vec3 wc(0.8f, 0.86f, 1.0f);
    for (size_t t = 0; t < tris; ++t) {
        uint32_t k3[3] = {mi[t * 3], mi[t * 3 + 1], mi[t * 3 + 2]};
        if (k3[0] >= mv.size() || k3[1] >= mv.size() || k3[2] >= mv.size()) continue;
        idx.insert(idx.end(), {k3[0], k3[1], k3[2]});
        for (int k = 0; k < 3; ++k) {
            wire.push_back(fv(Vec3(mv[k3[k]]), wc, 0.5f));
            wire.push_back(fv(Vec3(mv[k3[(k + 1) % 3]]), wc, 0.5f));
        }
    }
    c.shaded.upload(v, idx);
    c.wire.upload(wire);
    c.triangles = tris;
    return c;
}

void Scene::draw(const Mat4 &view_proj, SceneData &d) {
    renderer.begin_eye(view_proj);
    const MrState &mr = *d.mr;
    const ViewOptions &view = *d.view;
    Batches b;
    b.eye = d.head.p;
    b.scale = d.line_scale;
    for (auto &m : meshes_) m.second.used = false;

    struct Shown {
        const RoomSet *set;
        bool overlay;  // META over FB: outlines only, in one colour
    };
    std::vector<Shown> shown;
    if (view.source == kSourceFb || view.source == kSourceBoth) shown.push_back({&mr.fb, false});
    if (view.source == kSourceMeta) shown.push_back({&mr.meta, false});
    if (view.source == kSourceBoth) shown.push_back({&mr.meta, true});
    if (view.source == kSourceExt) shown.push_back({&mr.ext, false});
    shown.push_back({&mr.anchors, false});

    std::vector<std::pair<MeshCache *, Mat4>> mesh_draws;
    for (const Shown &s : shown) {
        for (const Entity &e : s.set->entities) {
            if (!e.located) continue;
            const Pose &P = e.pose;
            Vec3 color = e.anchor ? kAnchorColor : label_color(e.labels);
            if (s.overlay) color = kMetaOverlay;
            const float lw = s.overlay ? 0.004f : 0.008f;
            bool invisible = e.labels.find("INVISIBLE_WALL_FACE") != std::string::npos;
            std::vector<std::string> text;
            std::string title = e.labels.empty() ? (e.role.empty() ? "(no label)" : e.role) : e.labels;
            std::string upper_role = e.role;
            for (char &ch : upper_role) ch = (char)std::toupper((unsigned char)ch);
            if (!e.role.empty() && !e.labels.empty() && e.role != "room" && upper_role != e.labels) title += "  " + e.role;
            Vec3 label_at = P.p;

            if (e.anchor) {
                renderer.draw_mesh(renderer.sphere, Mat4::from_pose(P, 0.025f), kAnchorColor, 1.0f);
                gizmo(b, P, 0.15f, 0.007f);
                label_at = P.apply({0, 0.12f, 0});
                text = {"anchor", e.anchor_state, e.has_uuid ? uuid_hex(e.uuid).substr(0, 8) : "-"};
                if (view.labels) label(b, label_at, kAnchorColor, text);
                continue;
            }

            // volumes: wire box + faint faces (BOUNDED_3D, Meta: hangs below the anchor)
            if (e.has_b3d && view.volumes) {
                const XrRect3DfFB &r = e.b3d;
                Vec3 lo(r.offset.x, r.offset.y, r.offset.z),
                    hi(r.offset.x + r.extent.width, r.offset.y + r.extent.height, r.offset.z + r.extent.depth);
                Vec3 c[8];
                for (int k = 0; k < 8; ++k)
                    c[k] = P.apply({k & 1 ? hi.x : lo.x, k & 2 ? hi.y : lo.y, k & 4 ? hi.z : lo.z});
                const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                          {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                for (auto &ed : edges) ribbon(b, c[ed[0]], c[ed[1]], lw, color, 0.95f);
                if (!s.overlay) {
                    const int faces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4},
                                             {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
                    for (auto &f : faces)
                        for (int k : {0, 1, 2, 0, 2, 3}) b.fill.push_back(fv(c[f[k]], color, 0.16f));
                }
                Vec3 top = P.apply({(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, hi.z});
                label_at = top + Vec3(0, 0.14f, 0);
                char size[80];
                std::snprintf(size, sizeof(size), "%.2f x %.2f x %.2f m", r.extent.width, r.extent.height,
                              r.extent.depth);
                text = {title, size};
            } else if (e.has_b2d && view.planes) {
                // planes: outline (BOUNDARY_2D polygon if given, else the BOUNDED_2D rect) + translucent fill
                const XrRect2Df &r = e.b2d;
                std::vector<XrVector2f> rect = {{r.offset.x, r.offset.y},
                                                {r.offset.x + r.extent.width, r.offset.y},
                                                {r.offset.x + r.extent.width, r.offset.y + r.extent.height},
                                                {r.offset.x, r.offset.y + r.extent.height}};
                const std::vector<XrVector2f> &poly = e.boundary.size() >= 3 ? e.boundary : rect;
                auto world = [&](const XrVector2f &p) { return P.apply({p.x, p.y, 0}); };
                for (size_t k = 0; k < poly.size(); ++k) {
                    Vec3 p = world(poly[k]), q = world(poly[(k + 1) % poly.size()]);
                    if (invisible) dashed(b, p, q, lw, color, 0.9f);
                    else ribbon(b, p, q, lw, color, 0.95f);
                }
                if (&poly != &rect && !s.overlay)  // the bounding rect, thin, when the outline isn't it
                    for (size_t k = 0; k < 4; ++k) dashed(b, world(rect[k]), world(rect[(k + 1) % 4]), 0.003f, color, 0.5f, 0.05f);
                if (!s.overlay && !invisible) {
                    bool wall = e.labels.find("WALL") != std::string::npos;
                    float alpha = wall ? 0.09f : 0.2f;
                    for (int i : triangulate(poly)) b.fill.push_back(fv(world(poly[i]), color, alpha));
                }
                bool horizontal = std::fabs(P.q.rotate({0, 0, 1}).y) > 0.7f;
                label_at = horizontal ? P.p + Vec3(0, 0.05f, 0) : P.apply({0, 0, 0.04f});
                text = {title, fmt2("%.2f x %.2f m", r.extent.width, r.extent.height)};
            }

            // room mesh (XR_META_spatial_entity_mesh)
            if (!e.mesh_indices.empty() && view.mesh != kMeshOff && !s.overlay) {
                MeshCache &mc = mesh_for(s.set->short_name + uuid_hex(e.uuid), e);
                mesh_draws.push_back({&mc, Mat4::from_pose(P)});
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%zu triangles", mc.triangles);
                text = {title, buf};
                label_at = P.apply(mc.label_at);
            }
            if (e.is_room) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%zu walls, %zu spaces inside", e.walls.size(), e.contained.size());
                text = {"ROOM (room layout)", buf};
                label_at = P.p + Vec3(0, 1.1f, 0);
            }
            if (view.gizmos && !s.overlay) gizmo(b, P, e.is_room ? 0.3f : 0.12f, 0.006f);
            // FB + META: one label per space saying whether both APIs agree; META's own label only where they don't
            std::string agree;
            bool differs = false;
            if (view.source == kSourceBoth) {
                const RoomSet &other = s.overlay ? mr.fb : mr.meta;
                int j = e.has_uuid ? other.find(e.uuid) : -1;
                if (j < 0) {
                    agree = s.overlay ? "only META" : "META: missing";
                    differs = true;
                } else {
                    const Entity &o = other.entities[j];
                    float dist = o.located && e.located ? length(o.pose.p - e.pose.p) : -1;
                    if (o.labels != e.labels) agree = "META labels: " + o.labels, differs = true;
                    else if (dist < 0) agree = "META: not located", differs = true;
                    else if (dist > 0.01f) {
                        char buf[48];
                        std::snprintf(buf, sizeof(buf), "META %.2f m away", dist);
                        agree = buf;
                        differs = true;
                    } else {
                        agree = "FB = META";
                    }
                }
                if (s.overlay && !differs) text.clear();
            }
            if (view.labels && !text.empty()) {
                std::string last = (e.has_uuid ? uuid_hex(e.uuid).substr(0, 8) : std::string("-")) + "  " +
                                   s.set->short_name + (e.failures ? "  FAILED CALLS" : "");
                if (!agree.empty()) last += "  " + agree;
                text.push_back(last);
                Vec3 lc = e.failures || differs ? Vec3(1, 0.3f, 0.25f) : color;
                label(b, label_at + (s.overlay ? Vec3(0, -0.1f, 0) : Vec3()), lc, text);
            }
        }
    }

    // STAGE: origin axes and the bounds rect on its floor
    if (view.boundary && mr.stage_located) {
        const Pose &S = mr.stage_pose;
        gizmo(b, S, 0.25f, 0.008f);
        std::vector<std::string> text = {"STAGE"};
        Vec3 stage_label = S.p + Vec3(0, 0.45f, 0);
        if (mr.stage_bounds_result == XR_SUCCESS && mr.stage_bounds.width > 0) {
            float w = mr.stage_bounds.width / 2, h = mr.stage_bounds.height / 2;
            Vec3 c[4] = {S.apply({-w, 0.005f, -h}), S.apply({w, 0.005f, -h}), S.apply({w, 0.005f, h}), S.apply({-w, 0.005f, h})};
            for (int k = 0; k < 4; ++k) {
                dashed(b, c[k], c[(k + 1) % 4], 0.012f, kStageColor, 0.95f, 0.15f);
                ribbon(b, c[k], c[k] + Vec3(0, 0.3f, 0), 0.01f, kStageColor, 0.9f);
            }
            text.push_back(fmt2("bounds rect %.2f x %.2f m", mr.stage_bounds.width, mr.stage_bounds.height));
            stage_label = c[0] + Vec3(0, 0.45f, 0);  // at a corner post, clear of the floor's label
        } else {
            text.push_back(std::string("bounds: ") + xr_result_str(mr.stage_bounds_result));
        }
        text.push_back("xrGetReferenceSpaceBoundsRect");
        if (view.labels) label(b, stage_label, kStageColor, text);
    }

    // controllers: grip marker, aim ray, cursor on the panel
    for (int h = 0; h < 2; ++h) {
        if (d.grip_valid[h]) renderer.draw_mesh(renderer.sphere, Mat4::from_pose(d.grip[h], 0.012f), {0.9f, 0.9f, 0.95f}, 1.0f);
        if (!d.aim_valid[h]) continue;
        float len = 1.5f, u, v;
        Vec3 hit;
        bool on_panel = d.show_panel && panel.hit(d.aim[h], u, v, hit);
        if (on_panel) len = length(hit - d.aim[h].p);
        Vec3 col = h == 0 ? Vec3(0.3f, 0.6f, 1.0f) : Vec3(1.0f, 0.5f, 0.1f);
        ribbon(b, d.aim[h].p, d.aim[h].apply({0, 0, -len}), 0.004f, col, 0.8f);
        if (on_panel) renderer.draw_mesh(renderer.sphere, Mat4::from_pose(Pose(Quat(), hit), 0.007f), col, 1.0f);
    }

    // 1: lines (depth-written)
    renderer.draw_flat(b.solid);
    // 2: the mesh, translucent faces and the wireframe, without depth writes (nothing hides the planes and volumes)
    glDepthMask(GL_FALSE);
    for (auto &m : mesh_draws)
        if (view.mesh == kMeshShaded || view.mesh == kMeshBoth)
            renderer.draw_mesh(m.first->shaded, m.second, {0.8f, 0.8f, 0.8f}, 0.3f, true);
    renderer.draw_flat(b.fill);
    for (auto &m : mesh_draws)
        if (view.mesh == kMeshWire || view.mesh == kMeshBoth)
            renderer.draw_buffer(m.first->wire, GL_LINES, m.second, view.mesh == kMeshWire ? 0.9f : 0.35f);
    glDepthMask(GL_TRUE);
    // 4: labels and the panel on top of everything
    glDisable(GL_DEPTH_TEST);
    renderer.draw_flat(b.text);
    if (d.show_panel) {
        d.panel.mr = d.mr;
        d.panel.view = d.view;
        std::vector<FlatVertex> tris;
        panel.build(d.panel, tris);
        renderer.draw_flat(tris);
        for (int h = 0; h < 2; ++h) {  // the cursor stays visible on the panel
            float u, v;
            Vec3 hit;
            if (d.aim_valid[h] && panel.hit(d.aim[h], u, v, hit))
                renderer.draw_mesh(renderer.sphere, Mat4::from_pose(Pose(Quat(), hit), 0.006f),
                                   h == 0 ? Vec3(0.3f, 0.6f, 1.0f) : Vec3(1.0f, 0.5f, 0.1f), 1.0f);
        }
    }
    glEnable(GL_DEPTH_TEST);

    // meshes nobody showed this frame: free them
    for (auto it = meshes_.begin(); it != meshes_.end();) {
        if (!it->second.used && it->second.generation != 0) {
            it->second.shaded.release();
            it->second.wire.release();
            it = meshes_.erase(it);
        } else {
            ++it;
        }
    }
}
