// Everything the app asks the runtime about mixed reality: extensions, system properties, the room through
// XR_FB_spatial_entity_query + XR_FB_scene and through XR_META_spatial_entity_discovery, the global mesh
// (XR_META_spatial_entity_mesh, handed back through XR_FB_triangle_mesh), EXT plane detection, STAGE bounds,
// boundary visibility, scene capture and spatial anchors (create, save, erase, load through both FB and META storage).
// Every call and event goes to the MR log; the answers go into MrState for the panel and the 3D view.
#pragma once

#include <openxr/openxr.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "state.h"

#define TQMR_MR_FUNCTIONS(_)                                              \
    _(xrQuerySpacesFB, "XR_FB_spatial_entity_query")                      \
    _(xrRetrieveSpaceQueryResultsFB, "XR_FB_spatial_entity_query")        \
    _(xrGetSpaceUuidFB, "XR_FB_spatial_entity")                           \
    _(xrEnumerateSpaceSupportedComponentsFB, "XR_FB_spatial_entity")      \
    _(xrSetSpaceComponentStatusFB, "XR_FB_spatial_entity")                \
    _(xrGetSpaceComponentStatusFB, "XR_FB_spatial_entity")                \
    _(xrCreateSpatialAnchorFB, "XR_FB_spatial_entity")                    \
    _(xrSaveSpaceFB, "XR_FB_spatial_entity_storage")                      \
    _(xrEraseSpaceFB, "XR_FB_spatial_entity_storage")                     \
    _(xrGetSpaceContainerFB, "XR_FB_spatial_entity_container")            \
    _(xrGetSpaceRoomLayoutFB, "XR_FB_scene")                              \
    _(xrGetSpaceBoundingBox2DFB, "XR_FB_scene")                           \
    _(xrGetSpaceBoundingBox3DFB, "XR_FB_scene")                           \
    _(xrGetSpaceBoundary2DFB, "XR_FB_scene")                              \
    _(xrGetSpaceSemanticLabelsFB, "XR_FB_scene")                          \
    _(xrRequestSceneCaptureFB, "XR_FB_scene_capture")                     \
    _(xrGetSpaceTriangleMeshMETA, "XR_META_spatial_entity_mesh")          \
    _(xrDiscoverSpacesMETA, "XR_META_spatial_entity_discovery")           \
    _(xrRetrieveSpaceDiscoveryResultsMETA, "XR_META_spatial_entity_discovery") \
    _(xrSaveSpacesMETA, "XR_META_spatial_entity_persistence")             \
    _(xrEraseSpacesMETA, "XR_META_spatial_entity_persistence")            \
    _(xrRequestBoundaryVisibilityMETA, "XR_META_boundary_visibility")     \
    _(xrCreateTriangleMeshFB, "XR_FB_triangle_mesh")                      \
    _(xrDestroyTriangleMeshFB, "XR_FB_triangle_mesh")                     \
    _(xrCreatePlaneDetectorEXT, "XR_EXT_plane_detection")                 \
    _(xrDestroyPlaneDetectorEXT, "XR_EXT_plane_detection")                \
    _(xrBeginPlaneDetectionEXT, "XR_EXT_plane_detection")                 \
    _(xrGetPlaneDetectionStateEXT, "XR_EXT_plane_detection")              \
    _(xrGetPlaneDetectionsEXT, "XR_EXT_plane_detection")                  \
    _(xrGetPlanePolygonBufferEXT, "XR_EXT_plane_detection")

class Mr {
public:
    MrState st;
    std::string files_dir;  // anchors.txt and the room dumps go here

    // The MR extensions the app knows (listed in the panel) and which of them it enables if the runtime lists them.
    static void extension_list(std::vector<ExtRow> &rows);
    static bool wants(const std::string &name);
    // After xrCreateInstance/xrGetSystem: looks up the functions, reads the system properties.
    void on_instance(XrInstance instance, XrSystemId system, const std::set<std::string> &listed,
                     const std::set<std::string> &enabled, const std::map<std::string, uint32_t> &versions);
    // After xrCreateSession: `base` is the space everything is drawn in (LOCAL), `stage` may be XR_NULL_HANDLE.
    void on_session(XrSession session, XrSpace base, XrSpace stage);
    void on_session_end();
    // The session is running: first queries.
    void start();
    bool handle_event(const XrEventDataBuffer &event);  // true if it was an MR event
    void update(XrTime time);                            // locate entities, follow plane detection

    void refresh();  // FB query + META discovery + EXT plane detection + STAGE bounds
    void refresh_fb();
    void refresh_meta();
    void refresh_ext();
    void query_stage_bounds();
    void scene_capture();
    void request_boundary(bool suppress);
    void create_anchor(const Pose &pose_in_base);
    void save_anchors(bool meta);
    void erase_anchors(bool meta);
    void load_anchors(bool meta);
    void clear_anchors();
    bool enabled(const char *ext) const { return enabled_.count(ext) != 0; }
    bool running() const { return session_ != XR_NULL_HANDLE; }

private:
#define TQMR_DECLARE(name, ext) PFN_##name name##_ = nullptr;
    TQMR_MR_FUNCTIONS(TQMR_DECLARE)
#undef TQMR_DECLARE

    enum class Req { FbRooms, FbChildren, FbAnchors, MetaAll, MetaAnchors, SetStatus, CreateAnchor, SaveFb, EraseFb,
                     SaveMeta, EraseMeta, Capture };
    struct Pending {
        Req kind;
        double started;
        RoomSet *set = nullptr;
        XrSpace space = XR_NULL_HANDLE;
        std::vector<XrUuidEXT> uuids;
    };

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId system_ = XR_NULL_SYSTEM_ID;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace base_ = XR_NULL_HANDLE, stage_ = XR_NULL_HANDLE;
    XrTime time_ = 0;
    std::set<std::string> enabled_;
    std::map<XrAsyncRequestIdFB, Pending> pending_;
    std::set<XrSpace> owned_;  // entity spaces the app received (destroyed when no set shows them any more)
    XrPlaneDetectorEXT detector_ = XR_NULL_HANDLE;
    bool detecting_ = false, ext_pending_ = false;
    bool contour_ = false;
    std::vector<std::string> saved_uuids_;  // anchors this app saved (anchors.txt), for loading them again

    void track(XrAsyncRequestIdFB id, Req kind, RoomSet *set = nullptr, XrSpace space = XR_NULL_HANDLE,
               std::vector<XrUuidEXT> uuids = {});
    void clear_set(RoomSet &set);
    void release_unused_spaces();
    Entity &add_entity(RoomSet &set, XrSpace space, const XrUuidEXT *uuid);
    void read_entity(Entity &e, bool anchor);
    XrResult call(Entity &e, const char *name, XrResult r);
    void retrieve_fb(XrAsyncRequestIdFB id, Pending &p);
    void retrieve_meta(XrAsyncRequestIdFB id, Pending &p);
    void query_children(RoomSet &set);
    void finish_set(RoomSet &set, const char *how);
    void test_triangle_mesh(const Entity &e);
    void poll_planes();
    void load_saved_list();
    void store_saved_list();
    void dump_set(const RoomSet &set, const char *file) const;
    void fact(const std::string &label, const std::string &value, int level = 0);
};
