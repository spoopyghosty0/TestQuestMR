// What the runtime answered, as plain data: filled by Mr (mr.cpp) on the headset and by demo_room.cpp for the demo
// mode and the PC renderer; drawn by Scene and the panel.
#pragma once

#include <openxr/openxr.h>

#include <string>
#include <vector>

#include "xmath.h"
#include "xrnames.h"

// One MR-relevant extension: listed by the runtime, enabled by the app.
struct ExtRow {
    std::string name;
    std::string note;  // what the app uses it for
    uint32_t version = 0;
    bool listed = false, enabled = false;
};

// One call's answer, for the panel ("xrGetSpaceBoundingBox3DFB  XR_ERROR_..."): kNotCalled until it ran.
struct CallResult {
    std::string call;
    XrResult result = kNotCalled;
};

// A spatial entity (room, floor, wall, furniture, mesh, anchor) or a detected plane, with everything read about it.
struct Entity {
    XrSpace space = XR_NULL_HANDLE;
    XrUuidEXT uuid{};
    bool has_uuid = false;
    std::vector<XrSpaceComponentTypeFB> components;  // supported (xrEnumerateSpaceSupportedComponentsFB)
    std::vector<XrSpaceComponentTypeFB> enabled;     // enabled (xrGetSpaceComponentStatusFB)
    std::string labels;                               // semantic labels as returned (comma separated)

    bool located = false;  // pose valid in the app's base space
    Pose pose;
    XrSpaceLocationFlags flags = 0;
    XrResult locate_result = kNotCalled;

    bool has_b2d = false;
    XrRect2Df b2d{};
    std::vector<XrVector2f> boundary;  // BOUNDED_2D outline (xrGetSpaceBoundary2DFB), local XY
    bool has_b3d = false;
    XrRect3DfFB b3d{};

    bool is_room = false;  // has ROOM_LAYOUT
    XrUuidEXT floor{}, ceiling{};
    std::vector<XrUuidEXT> walls;      // ROOM_LAYOUT order
    std::vector<XrUuidEXT> contained;  // SPACE_CONTAINER

    std::vector<XrVector3f> mesh_vertices;  // TRIANGLE_MESH (XR_META_spatial_entity_mesh), local space
    std::vector<uint32_t> mesh_indices;
    unsigned mesh_generation = 0;  // changes whenever the mesh is read again (the renderer re-uploads)

    std::vector<CallResult> calls;  // every call made for this entity, in order
    int failures = 0;               // calls that failed (other than "component not supported/enabled")

    // App anchors
    bool anchor = false;
    std::string anchor_state;  // "created", "saved (FB)", "loaded (META)", ...

    // EXT plane detection
    int ext_semantic = -1;
    uint64_t ext_plane_id = 0;

    std::string role;  // "room", "floor", "ceiling", "wall 2/5" (filled from the room layout)
};

// The result of one way of asking for the room (FB query, META discovery, EXT plane detection) or the app's anchors.
struct RoomSet {
    std::string name;    // "XR_FB_spatial_entity_query"
    std::string short_name;  // "FB"
    std::string status;  // one line: what happened last
    int status_level = 0;  // 0 neutral, 1 good, 2 problem
    std::vector<Entity> entities;
    double updated = -1;  // log clock of the last result
    int requests = 0;

    int find(const XrUuidEXT &u) const {
        for (size_t i = 0; i < entities.size(); ++i)
            if (entities[i].has_uuid && uuid_eq(entities[i].uuid, u)) return (int)i;
        return -1;
    }
    int find_space(XrSpace s) const {
        for (size_t i = 0; i < entities.size(); ++i)
            if (entities[i].space == s) return (int)i;
        return -1;
    }
    // Fills `role` of every entity from the room layouts found in the set.
    void assign_roles();
};

struct Fact {
    std::string label, value;
    int level = 0;  // 0 neutral, 1 good, 2 problem
};

struct MrState {
    std::string runtime, system, instance;  // "Oculus 1.2.3", "Oculus Quest3", "OpenXR 1.0 instance"
    std::vector<ExtRow> extensions;
    std::vector<Fact> facts;  // system properties, reference spaces, permissions, ...

    // STAGE: bounds rect (xrGetReferenceSpaceBoundsRect) and where the STAGE origin is in the base space
    XrResult stage_bounds_result = kNotCalled;
    XrExtent2Df stage_bounds{};
    bool stage_located = false;
    Pose stage_pose;
    bool stage_space = false;  // a STAGE space could be created

    // XR_META_boundary_visibility
    std::string boundary_visibility = "unknown";  // from the change event
    XrResult boundary_request_result = kNotCalled;
    bool boundary_suppress_requested = false;

    // XR_FB_scene_capture
    std::string capture_status;
    // XR_FB_triangle_mesh: the global mesh handed back to the runtime as an XrTriangleMeshFB
    std::string triangle_mesh_status;
    std::string passthrough;  // how passthrough runs, or why not
    std::string permission;   // scene permission state

    RoomSet fb, meta, ext, anchors;
    bool demo = false;
};

// What the panel's toggles show.
enum MeshMode { kMeshOff, kMeshShaded, kMeshWire, kMeshBoth, kMeshModeCount };
enum SourceMode { kSourceFb, kSourceMeta, kSourceBoth, kSourceExt, kSourceModeCount };
enum Tab { kTabRuntime, kTabRoom, kTabAnchors, kTabLog, kTabCount };

struct ViewOptions {
    bool planes = true, volumes = true, labels = true, boundary = true, gizmos = true, passthrough = true;
    int mesh = kMeshShaded;
    int source = kSourceFb;
    int tab = kTabRoom;
    int log_scroll = 0;   // lines up from the newest
    int room_scroll = 0;  // entities down from the first
    // percent: the room visualisation (fills and mesh fade to nothing, outlines and label plates to a faint minimum)
    // and the panel's background
    int overlay_opacity = 100, panel_opacity = 100;
};

const char *source_mode_name(int mode);
const char *mesh_mode_name(int mode);
