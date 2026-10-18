#include "demo_room.h"

#include <cmath>
#include <cstdio>

namespace {

// Deterministic UUIDs: "de305d54" + index, recognisable as demo data.
XrUuidEXT demo_uuid(int i) {
    XrUuidEXT u{};
    const uint8_t head[6] = {0xde, 0x30, 0x5d, 0x54, 0x75, 0xb4};
    for (int k = 0; k < 6; ++k) u.data[k] = head[k];
    for (int k = 6; k < 16; ++k) u.data[k] = (uint8_t)((i * 131 + k * 29) & 0xff);
    u.data[15] = (uint8_t)i;
    return u;
}

float noise(float x, float y, int seed) {
    // smooth value noise from a few sines: a scan's ripple without randomness
    return 0.5f * std::sin(x * 7.1f + seed * 1.7f) * std::sin(y * 5.3f + seed * 0.9f) +
           0.3f * std::sin(x * 13.7f + y * 11.3f + seed) + 0.2f * std::sin(x * 23.0f - y * 19.0f + seed * 2.3f);
}

struct Builder {
    RoomSet *set;
    Pose stage;  // STAGE in the base space
    int next = 0;

    Entity &add(const std::string &labels, const Pose &in_stage, const std::vector<XrSpaceComponentTypeFB> &comps) {
        set->entities.emplace_back();
        Entity &e = set->entities.back();
        e.uuid = demo_uuid(next++);
        e.has_uuid = true;
        e.labels = labels;
        e.pose = stage * in_stage;
        e.located = true;
        e.locate_result = XR_SUCCESS;
        e.flags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT |
                  XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
        e.components = {XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB, XR_SPACE_COMPONENT_TYPE_STORABLE_FB,
                        XR_SPACE_COMPONENT_TYPE_SHARABLE_FB};
        e.components.insert(e.components.end(), comps.begin(), comps.end());
        e.enabled = e.components;
        e.calls = {{"xrEnumerateSpaceSupportedComponentsFB", XR_SUCCESS}};
        for (XrSpaceComponentTypeFB t : comps) {
            if (t == XR_SPACE_COMPONENT_TYPE_SEMANTIC_LABELS_FB) e.calls.push_back({"xrGetSpaceSemanticLabelsFB", XR_SUCCESS});
            if (t == XR_SPACE_COMPONENT_TYPE_BOUNDED_2D_FB) {
                e.calls.push_back({"xrGetSpaceBoundingBox2DFB", XR_SUCCESS});
                e.calls.push_back({"xrGetSpaceBoundary2DFB", XR_SUCCESS});
            }
            if (t == XR_SPACE_COMPONENT_TYPE_BOUNDED_3D_FB) e.calls.push_back({"xrGetSpaceBoundingBox3DFB", XR_SUCCESS});
            if (t == XR_SPACE_COMPONENT_TYPE_ROOM_LAYOUT_FB) e.calls.push_back({"xrGetSpaceRoomLayoutFB", XR_SUCCESS});
            if (t == XR_SPACE_COMPONENT_TYPE_SPACE_CONTAINER_FB) e.calls.push_back({"xrGetSpaceContainerFB", XR_SUCCESS});
            if (t == XR_SPACE_COMPONENT_TYPE_TRIANGLE_MESH_META)
                e.calls.push_back({"xrGetSpaceTriangleMeshMETA", XR_SUCCESS});
        }
        return e;
    }
};

const float kHeight = 2.6f;

// The room's outline (x, z in STAGE): an L-shaped living room, positive shoelace order.
std::vector<Vec3> outline() {
    std::vector<Vec3> pts = {{-2.0f, 0, -2.4f}, {2.2f, 0, -2.4f}, {2.2f, 0, 1.2f},
                             {0.9f, 0, 1.2f},   {0.9f, 0, 2.0f},  {-2.0f, 0, 2.0f}};
    float area = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        const Vec3 &a = pts[i], &b = pts[(i + 1) % pts.size()];
        area += a.x * b.z - b.x * a.z;
    }
    if (area < 0) std::vector<Vec3>(pts.rbegin(), pts.rend()).swap(pts);
    return pts;
}

Vec3 centroid(const std::vector<Vec3> &pts) {
    float a = 0, cx = 0, cz = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        const Vec3 &p = pts[i], &q = pts[(i + 1) % pts.size()];
        float c = p.x * q.z - q.x * p.z;
        a += c;
        cx += (p.x + q.x) * c;
        cz += (p.z + q.z) * c;
    }
    a *= 0.5f;
    return {cx / (6 * a), 0, cz / (6 * a)};
}

bool inside(const std::vector<Vec3> &pts, float x, float z) {
    bool in = false;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++)
        if ((pts[i].z > z) != (pts[j].z > z) &&
            x < (pts[j].x - pts[i].x) * (z - pts[i].z) / (pts[j].z - pts[i].z) + pts[i].x)
            in = !in;
    return in;
}

// Wall frame (Meta convention): +X along the wall, +Y up, +Z into the room.
Pose wall_pose(const Vec3 &a, const Vec3 &b, const Vec3 &centre, float y) {
    Vec3 x = normalize(b - a), up(0, 1, 0), z = cross(x, up);
    Vec3 mid = (a + b) * 0.5f;
    if (dot(z, centre - mid) < 0) z = -z, x = -x;  // keep +Z pointing into the room
    return Pose(Quat::from_basis(x, up, z), Vec3(mid.x, y, mid.z));
}

struct Box {
    const char *label;
    float x, z, top, yaw, w, d, h;  // top-face centre, yaw (degrees), width/depth/height
};
const Box kBoxes[] = {
    {"TABLE", -0.85f, -1.35f, 0.74f, 0, 1.40f, 0.80f, 0.74f},
    {"COUCH", 1.55f, -0.55f, 0.82f, 90, 2.00f, 0.90f, 0.82f},
    {"STORAGE", -1.78f, 0.75f, 1.85f, 0, 0.40f, 1.20f, 1.85f},
    {"LAMP", 1.95f, -2.15f, 1.55f, 0, 0.32f, 0.32f, 1.55f},
    {"PLANT", -1.72f, -2.12f, 1.05f, 20, 0.45f, 0.45f, 1.05f},
    {"BED", -0.95f, 1.30f, 0.50f, 0, 1.40f, 1.10f, 0.50f},
};

Pose box_pose(const Box &b) {
    Quat yaw = Quat::axis_angle({0, 1, 0}, deg(b.yaw));
    Quat up = Quat::axis_angle({1, 0, 0}, deg(-90));  // local +Z -> +Y
    return Pose(yaw * up, Vec3(b.x, b.top, b.z));
}

// A noisy grid over a rectangle in a frame: what a depth scan of a flat surface looks like.
void grid(std::vector<XrVector3f> &v, std::vector<uint32_t> &idx, const Pose &frame, float x0, float y0, float x1,
          float y1, float step, int seed, const std::vector<Vec3> *clip = nullptr) {
    int nx = std::max(1, (int)std::round((x1 - x0) / step)), ny = std::max(1, (int)std::round((y1 - y0) / step));
    uint32_t base = (uint32_t)v.size();
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i) {
            float x = x0 + (x1 - x0) * i / nx, y = y0 + (y1 - y0) * j / ny;
            float n = 0.004f * noise(x * 0.6f, y * 0.6f, seed);  // a few millimetres of scan noise
            Vec3 p = frame.apply(Vec3(x, y, n));
            v.push_back({p.x, p.y, p.z});
        }
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            uint32_t a = base + j * (nx + 1) + i, b = a + 1, c = a + nx + 1, d = c + 1;
            if (clip) {
                const XrVector3f &pa = v[a], &pd = v[d];
                if (!inside(*clip, (pa.x + pd.x) * 0.5f, (pa.z + pd.z) * 0.5f)) continue;
            }
            idx.insert(idx.end(), {a, b, d, a, d, c});  // counter-clockwise seen from the frame's +Z
        }
}

}  // namespace

void demo_room(MrState &st, const Pose &stage_in_base) {
    std::vector<Vec3> pts = outline();
    Vec3 c = centroid(pts);
    for (RoomSet *set : {&st.fb, &st.meta}) {
        set->entities.clear();
        Builder b{set, stage_in_base};
        Entity &room = b.add("", Pose(), {XR_SPACE_COMPONENT_TYPE_ROOM_LAYOUT_FB, XR_SPACE_COMPONENT_TYPE_SPACE_CONTAINER_FB});
        room.is_room = true;
        size_t room_index = set->entities.size() - 1;

        // floor and ceiling: outline in local XY (floor: +Z up, local +Y = -Z; ceiling: +Z down, local +Y = +Z)
        Entity &floor = b.add("FLOOR", Pose(Quat::axis_angle({1, 0, 0}, deg(-90)), c),
                              {XR_SPACE_COMPONENT_TYPE_BOUNDED_2D_FB, XR_SPACE_COMPONENT_TYPE_SEMANTIC_LABELS_FB});
        float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
        for (const Vec3 &p : pts) {
            floor.boundary.push_back({p.x - c.x, -(p.z - c.z)});
            minx = std::min(minx, p.x), maxx = std::max(maxx, p.x), minz = std::min(minz, p.z), maxz = std::max(maxz, p.z);
        }
        floor.has_b2d = true;
        floor.b2d = {{minx - c.x, -(maxz - c.z)}, {maxx - minx, maxz - minz}};
        Entity &ceiling = b.add("CEILING", Pose(Quat::axis_angle({1, 0, 0}, deg(90)), Vec3(c.x, kHeight, c.z)),
                                {XR_SPACE_COMPONENT_TYPE_BOUNDED_2D_FB, XR_SPACE_COMPONENT_TYPE_SEMANTIC_LABELS_FB});
        for (const Vec3 &p : pts) ceiling.boundary.push_back({p.x - c.x, p.z - c.z});
        ceiling.has_b2d = true;
        ceiling.b2d = {{minx - c.x, minz - c.z}, {maxx - minx, maxz - minz}};

        std::vector<XrUuidEXT> walls;
        for (size_t i = 0; i < pts.size(); ++i) {
            const Vec3 &p = pts[i], &q = pts[(i + 1) % pts.size()];
            float len = length(q - p);
            // the short edge of the L is open: the scan closed it without seeing a wall
            const char *label = i == 3 ? "INVISIBLE_WALL_FACE" : "WALL_FACE";
            Entity &w = b.add(label, wall_pose(p, q, c, kHeight / 2),
                              {XR_SPACE_COMPONENT_TYPE_BOUNDED_2D_FB, XR_SPACE_COMPONENT_TYPE_SEMANTIC_LABELS_FB});
            w.has_b2d = true;
            w.b2d = {{-len / 2, -kHeight / 2}, {len, kHeight}};
            w.boundary = {{-len / 2, -kHeight / 2}, {len / 2, -kHeight / 2}, {len / 2, kHeight / 2}, {-len / 2, kHeight / 2}};
            walls.push_back(w.uuid);
        }

        // planes on walls: door, window, wall art, screen (pose at the centre, +Z into the room)
        struct WallPlane {
            const char *label;
            int wall;
            float along, y, w, h;
        };
        const WallPlane planes[] = {{"DOOR_FRAME", 5, 0.9f, 1.03f, 0.92f, 2.06f},
                                    {"WINDOW_FRAME", 1, -0.4f, 1.45f, 1.40f, 1.10f},
                                    {"WALL_ART", 4, -0.2f, 1.55f, 0.80f, 0.60f},
                                    {"SCREEN", 0, -0.85f, 1.30f, 1.20f, 0.70f}};
        for (const WallPlane &wp : planes) {
            const Vec3 &p = pts[wp.wall], &q = pts[(wp.wall + 1) % pts.size()];
            Pose frame = wall_pose(p, q, c, wp.y);
            Entity &e = b.add(wp.label, frame * Pose(Quat(), Vec3(wp.along, 0, 0.01f)),
                              {XR_SPACE_COMPONENT_TYPE_BOUNDED_2D_FB, XR_SPACE_COMPONENT_TYPE_SEMANTIC_LABELS_FB});
            e.has_b2d = true;
            e.b2d = {{-wp.w / 2, -wp.h / 2}, {wp.w, wp.h}};
        }

        for (const Box &bx : kBoxes) {
            Entity &e = b.add(bx.label, box_pose(bx),
                              {XR_SPACE_COMPONENT_TYPE_BOUNDED_2D_FB, XR_SPACE_COMPONENT_TYPE_BOUNDED_3D_FB,
                               XR_SPACE_COMPONENT_TYPE_SEMANTIC_LABELS_FB});
            e.has_b2d = true;
            e.b2d = {{-bx.w / 2, -bx.d / 2}, {bx.w, bx.d}};
            e.has_b3d = true;
            e.b3d = {{-bx.w / 2, -bx.d / 2, -bx.h}, {bx.w, bx.d, bx.h}};
        }

        // the global mesh: walls, floor, ceiling and furniture as a scan would see them
        Entity &mesh = b.add("GLOBAL_MESH", Pose(), {XR_SPACE_COMPONENT_TYPE_TRIANGLE_MESH_META,
                                                     XR_SPACE_COMPONENT_TYPE_SEMANTIC_LABELS_FB});
        std::vector<XrVector3f> &v = mesh.mesh_vertices;
        std::vector<uint32_t> &idx = mesh.mesh_indices;
        for (size_t i = 0; i < pts.size(); ++i) {
            if (i == 3) continue;  // nothing to scan at the open side
            const Vec3 &p = pts[i], &q = pts[(i + 1) % pts.size()];
            float len = length(q - p);
            grid(v, idx, wall_pose(p, q, c, 0), -len / 2, 0, len / 2, kHeight, 0.1f, (int)i);
        }
        Pose floor_frame(Quat::axis_angle({1, 0, 0}, deg(-90)), Vec3());
        grid(v, idx, floor_frame, minx, -maxz, maxx, -minz, 0.1f, 11, &pts);
        Pose ceil_frame(Quat::axis_angle({1, 0, 0}, deg(90)), Vec3(0, kHeight, 0));
        grid(v, idx, ceil_frame, minx, minz, maxx, maxz, 0.12f, 12, &pts);
        int seed = 20;
        for (const Box &bx : kBoxes) {
            Pose top = box_pose(bx);  // +Z up
            grid(v, idx, top, -bx.w / 2, -bx.d / 2, bx.w / 2, bx.d / 2, 0.06f, seed++);
            // four sides: each face's frame has +Z outwards
            for (int s = 0; s < 4; ++s) {
                float half = (s % 2 == 0) ? bx.d / 2 : bx.w / 2, span = (s % 2 == 0) ? bx.w / 2 : bx.d / 2;
                Quat turn = Quat::axis_angle({0, 0, 1}, deg(90.0f * s));
                Pose side = top * Pose(turn, Vec3()) * Pose(Quat::axis_angle({1, 0, 0}, deg(90)), Vec3(0, -half, 0));
                grid(v, idx, side, -span, -bx.h, span, 0, 0.08f, seed++);  // from the floor up to the top
            }
        }
        mesh.mesh_generation = 1;
        (void)room_index;

        Entity &r = set->entities[room_index];
        r.floor = set->entities[1].uuid;
        r.ceiling = set->entities[2].uuid;
        r.walls = walls;
        for (size_t i = 1; i < set->entities.size(); ++i) r.contained.push_back(set->entities[i].uuid);
        set->assign_roles();
        set->updated = 0.5;
    }
    st.fb.name = "XR_FB_spatial_entity_query + XR_FB_scene";
    st.fb.short_name = "FB";
    st.meta.name = "XR_META_spatial_entity_discovery";
    st.meta.short_name = "META";
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "demo: %zu spaces: 1 room, 1 floor, 1 ceiling, %zu walls, 4 other planes, 6 volumes, 1 mesh (%zu tris)",
                  st.fb.entities.size(), pts.size(), st.fb.entities.back().mesh_indices.size() / 3);
    st.fb.status = st.meta.status = buf;
    st.fb.status_level = st.meta.status_level = 1;

    // one saved anchor on the table
    st.anchors.name = "Spatial anchors (XR_FB_spatial_entity)";
    st.anchors.short_name = "Anchors";
    st.anchors.entities.clear();
    Builder a{&st.anchors, stage_in_base, 40};
    Entity &anchor = a.add("", Pose(Quat::axis_angle({0, 1, 0}, deg(25)), Vec3(-0.6f, 0.745f, -1.25f)), {});
    anchor.anchor = true;
    anchor.anchor_state = "saved (FB)";
    anchor.role = "anchor";
    st.anchors.status = "demo: 1 anchor";
    st.demo = true;
}

void demo_runtime(MrState &st) {
    st.runtime = "Demo runtime 1.0.0";
    st.system = "Demo headset";
    st.instance = "OpenXR 1.0 instance";
    const char *listed[] = {"XR_FB_spatial_entity", "XR_FB_spatial_entity_query", "XR_FB_spatial_entity_storage",
                            "XR_FB_spatial_entity_container", "XR_FB_scene", "XR_FB_scene_capture",
                            "XR_META_spatial_entity_mesh", "XR_META_spatial_entity_discovery",
                            "XR_META_spatial_entity_persistence", "XR_META_boundary_visibility",
                            "XR_FB_triangle_mesh", "XR_FB_passthrough", "XR_META_headset_id",
                            "XR_FB_spatial_entity_storage_batch", "XR_FB_spatial_entity_sharing",
                            "XR_FB_spatial_entity_user", "XR_META_environment_depth"};
    for (ExtRow &row : st.extensions) {
        for (const char *l : listed)
            if (row.name == l) row.listed = true, row.version = 1;
        row.enabled = row.listed && row.note.find("listed only") == std::string::npos;
    }
    st.facts = {{"MR functions", "24 of 24 resolved", 1},
                {"supportsSpatialEntity (FB)", "yes", 1},
                {"supportsSpaceDiscovery (META)", "yes", 1},
                {"supportsSpacePersistence (META)", "yes", 1},
                {"supportsBoundaryVisibility", "yes", 1},
                {"Headset id (META)", "00000000000000000000000000000000", 0},
                {"Passthrough (FB)", "capabilities 0x3", 0},
                {"Reference spaces", "VIEW LOCAL STAGE LOCAL_FLOOR", 0},
                {"STAGE space", "created", 0},
                {"STAGE bounds rect", "4.20 x 4.40 m", 1}};
    st.stage_bounds_result = XR_SUCCESS;
    st.stage_bounds = {4.2f, 4.4f};
    st.stage_space = true;
    st.boundary_visibility = "suppressed";
    st.boundary_request_result = XR_SUCCESS;
    st.boundary_suppress_requested = true;
    st.capture_status = "-";
    st.triangle_mesh_status = "xrCreateTriangleMeshFB -> XR_SUCCESS";
    st.passthrough = "XR_FB_passthrough";
    st.permission = "granted";
    st.ext = RoomSet{"XR_EXT_plane_detection", "EXT"};
    st.ext.status = "XR_EXT_plane_detection not offered";
}
