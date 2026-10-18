#include "mr.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "mrlog.h"

namespace {

struct KnownExt {
    const char *name;
    bool enable;       // the app enables it when listed
    const char *note;  // what it's for
};

// MR-relevant extensions. Listed-only ones are shown so a runtime's offer is visible, but nothing here uses them.
const KnownExt kKnown[] = {
    {"XR_FB_spatial_entity", true, "spaces, components, anchors"},
    {"XR_FB_spatial_entity_query", true, "xrQuerySpacesFB"},
    {"XR_FB_spatial_entity_storage", true, "save / erase anchors (FB)"},
    {"XR_FB_spatial_entity_container", true, "room contents"},
    {"XR_FB_scene", true, "labels, bounds, room layout"},
    {"XR_FB_scene_capture", true, "room setup request"},
    {"XR_META_spatial_entity_mesh", true, "room mesh"},
    {"XR_META_spatial_entity_discovery", true, "xrDiscoverSpacesMETA"},
    {"XR_META_spatial_entity_persistence", true, "save / erase anchors (META)"},
    {"XR_META_boundary_visibility", true, "hide the guardian"},
    {"XR_FB_triangle_mesh", true, "mesh back to the runtime"},
    {"XR_FB_passthrough", true, "camera view"},
    {"XR_META_headset_id", true, "headset id"},
    {"XR_EXT_plane_detection", true, "detected planes"},
    {"XR_FB_spatial_entity_storage_batch", false, "(listed only)"},
    {"XR_FB_spatial_entity_sharing", false, "(listed only)"},
    {"XR_FB_spatial_entity_user", false, "(listed only)"},
    {"XR_META_spatial_entity_group_sharing", false, "(listed only)"},
    {"XR_META_spatial_entity_semantic_label", false, "(listed only)"},
    {"XR_META_environment_depth", false, "(listed only)"},
    {"XR_ANDROID_trackables", false, "(listed only)"},
    {"XR_EXT_spatial_entity", false, "(listed only)"},
    {"XR_EXT_spatial_plane_tracking", false, "(listed only)"},
    {"XR_EXT_spatial_anchor", false, "(listed only)"},
    {"XR_EXT_spatial_persistence", false, "(listed only)"},
};

// Every label Meta's scene model knows: asking with all of them (and every support flag) gets the runtime's real
// labels instead of the legacy set an app without XrSemanticLabelsSupportInfoFB gets.
const char *kAllLabels =
    "TABLE,COUCH,FLOOR,CEILING,WALL_FACE,WINDOW_FRAME,DOOR_FRAME,STORAGE,BED,SCREEN,LAMP,PLANT,WALL_ART,"
    "GLOBAL_MESH,INVISIBLE_WALL_FACE,OTHER";

bool failed_badly(XrResult r) {
    // "this space doesn't have that" is an answer, not a failure
    return XR_FAILED(r) && r != XR_ERROR_SPACE_COMPONENT_NOT_ENABLED_FB && r != XR_ERROR_SPACE_COMPONENT_NOT_SUPPORTED_FB;
}

std::string short_uuid(const XrUuidEXT &u) { return uuid_hex(u).substr(0, 8); }

bool has(const std::vector<XrSpaceComponentTypeFB> &v, XrSpaceComponentTypeFB t) {
    return std::find(v.begin(), v.end(), t) != v.end();
}

const char *plane_semantic_name(int t) {
    switch (t) {
        case XR_PLANE_DETECTOR_SEMANTIC_TYPE_CEILING_EXT: return "CEILING";
        case XR_PLANE_DETECTOR_SEMANTIC_TYPE_FLOOR_EXT: return "FLOOR";
        case XR_PLANE_DETECTOR_SEMANTIC_TYPE_WALL_EXT: return "WALL";
        case XR_PLANE_DETECTOR_SEMANTIC_TYPE_PLATFORM_EXT: return "PLATFORM";
        default: return "UNDEFINED";
    }
}

}  // namespace

// ---------------------------------------------------------------- extensions / instance
void Mr::extension_list(std::vector<ExtRow> &rows) {
    rows.clear();
    for (const KnownExt &k : kKnown) rows.push_back({k.name, k.note});
}

bool Mr::wants(const std::string &name) {
    for (const KnownExt &k : kKnown)
        if (k.enable && name == k.name) return true;
    return false;
}

void Mr::fact(const std::string &label, const std::string &value, int level) {
    for (Fact &f : st.facts)
        if (f.label == label) {
            f.value = value;
            f.level = level;
            return;
        }
    st.facts.push_back({label, value, level});
}

void Mr::on_instance(XrInstance instance, XrSystemId system, const std::set<std::string> &listed,
                     const std::set<std::string> &enabled, const std::map<std::string, uint32_t> &versions) {
    instance_ = instance;
    system_ = system;
    enabled_ = enabled;
    extension_list(st.extensions);
    for (ExtRow &row : st.extensions) {
        row.listed = listed.count(row.name) != 0;
        row.enabled = enabled.count(row.name) != 0;
        auto v = versions.find(row.name);
        if (v != versions.end()) row.version = v->second;
        MLOG("extension %-40s %s%s", row.name.c_str(), row.listed ? "listed" : "not listed",
             row.enabled ? ", enabled" : "");
    }

    // function pointers: only for enabled extensions (looking up others is invalid)
    int found = 0, wanted = 0;
    std::string missing;
#define TQMR_LOOKUP(name, ext)                                                                                  \
    if (enabled_.count(ext)) {                                                                                  \
        ++wanted;                                                                                               \
        XrResult r = xrGetInstanceProcAddr(instance_, #name, (PFN_xrVoidFunction *)&name##_);                    \
        if (XR_SUCCEEDED(r) && name##_) {                                                                       \
            ++found;                                                                                            \
        } else {                                                                                                \
            name##_ = nullptr;                                                                                  \
            MERR("xrGetInstanceProcAddr(%s) -> %s: %s offers no %s", #name, xr_result_str(r), ext, #name);      \
            missing += std::string(missing.empty() ? "" : ", ") + #name;                                        \
        }                                                                                                       \
    }
    TQMR_MR_FUNCTIONS(TQMR_LOOKUP)
#undef TQMR_LOOKUP
    MLOG("MR functions: %d of %d resolved", found, wanted);
    fact("MR functions", std::to_string(found) + " of " + std::to_string(wanted) + " resolved" +
                             (missing.empty() ? "" : " - missing: " + missing),
         missing.empty() ? 1 : 2);

    // system properties, one struct per enabled extension
    XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
    XrSystemSpatialEntityPropertiesFB entity{XR_TYPE_SYSTEM_SPATIAL_ENTITY_PROPERTIES_FB};
    XrSystemSpaceDiscoveryPropertiesMETA discovery{XR_TYPE_SYSTEM_SPACE_DISCOVERY_PROPERTIES_META};
    XrSystemSpacePersistencePropertiesMETA persistence{XR_TYPE_SYSTEM_SPACE_PERSISTENCE_PROPERTIES_META};
    XrSystemBoundaryVisibilityPropertiesMETA boundary{XR_TYPE_SYSTEM_BOUNDARY_VISIBILITY_PROPERTIES_META};
    XrSystemHeadsetIdPropertiesMETA headset{XR_TYPE_SYSTEM_HEADSET_ID_PROPERTIES_META};
    XrSystemPassthroughProperties2FB passthrough{XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES2_FB};
    XrSystemPlaneDetectionPropertiesEXT planes{XR_TYPE_SYSTEM_PLANE_DETECTION_PROPERTIES_EXT};
    const void **tail = (const void **)&sp.next;
    auto chain = [&](const char *ext, void *s) {
        if (!enabled_.count(ext)) return;
        *tail = s;
        tail = (const void **)&((XrBaseOutStructure *)s)->next;
    };
    chain("XR_FB_spatial_entity", &entity);
    chain("XR_META_spatial_entity_discovery", &discovery);
    chain("XR_META_spatial_entity_persistence", &persistence);
    chain("XR_META_boundary_visibility", &boundary);
    chain("XR_META_headset_id", &headset);
    chain("XR_FB_passthrough", &passthrough);
    chain("XR_EXT_plane_detection", &planes);
    XrResult r = xrGetSystemProperties(instance_, system_, &sp);
    MLOG("xrGetSystemProperties -> %s: %s, vendor 0x%x", xr_result_str(r), sp.systemName, sp.vendorId);
    auto yes_no = [](XrBool32 b) { return std::string(b ? "yes" : "NO"); };
    if (enabled_.count("XR_FB_spatial_entity")) {
        fact("supportsSpatialEntity (FB)", yes_no(entity.supportsSpatialEntity), entity.supportsSpatialEntity ? 1 : 2);
        MLOG("  XrSystemSpatialEntityPropertiesFB.supportsSpatialEntity = %d", entity.supportsSpatialEntity);
    }
    if (enabled_.count("XR_META_spatial_entity_discovery")) {
        fact("supportsSpaceDiscovery (META)", yes_no(discovery.supportsSpaceDiscovery),
             discovery.supportsSpaceDiscovery ? 1 : 2);
        MLOG("  XrSystemSpaceDiscoveryPropertiesMETA.supportsSpaceDiscovery = %d", discovery.supportsSpaceDiscovery);
    }
    if (enabled_.count("XR_META_spatial_entity_persistence")) {
        fact("supportsSpacePersistence (META)", yes_no(persistence.supportsSpacePersistence),
             persistence.supportsSpacePersistence ? 1 : 2);
        MLOG("  XrSystemSpacePersistencePropertiesMETA.supportsSpacePersistence = %d",
             persistence.supportsSpacePersistence);
    }
    if (enabled_.count("XR_META_boundary_visibility")) {
        fact("supportsBoundaryVisibility", yes_no(boundary.supportsBoundaryVisibility),
             boundary.supportsBoundaryVisibility ? 1 : 2);
        MLOG("  XrSystemBoundaryVisibilityPropertiesMETA.supportsBoundaryVisibility = %d",
             boundary.supportsBoundaryVisibility);
    }
    if (enabled_.count("XR_META_headset_id")) {
        fact("Headset id (META)", uuid_hex(headset.id));
        MLOG("  XrSystemHeadsetIdPropertiesMETA.id = %s", uuid_hex(headset.id).c_str());
    }
    if (enabled_.count("XR_FB_passthrough")) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "capabilities 0x%llx", (unsigned long long)passthrough.capabilities);
        fact("Passthrough (FB)", buf);
        MLOG("  XrSystemPassthroughProperties2FB.capabilities = 0x%llx", (unsigned long long)passthrough.capabilities);
    }
    if (enabled_.count("XR_EXT_plane_detection")) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "features 0x%llx", (unsigned long long)planes.supportedFeatures);
        contour_ = (planes.supportedFeatures & XR_PLANE_DETECTION_CAPABILITY_PLANE_HOLES_BIT_EXT) != 0 ||
                   (planes.supportedFeatures & XR_PLANE_DETECTION_CAPABILITY_PLANE_DETECTION_BIT_EXT) != 0;
        fact("Plane detection (EXT)", buf);
        MLOG("  XrSystemPlaneDetectionPropertiesEXT.supportedFeatures = 0x%llx",
             (unsigned long long)planes.supportedFeatures);
    }

    st.fb = RoomSet{"XR_FB_spatial_entity_query + XR_FB_scene", "FB"};
    st.meta = RoomSet{"XR_META_spatial_entity_discovery", "META"};
    st.ext = RoomSet{"XR_EXT_plane_detection", "EXT"};
    st.anchors = RoomSet{"Spatial anchors (XR_FB_spatial_entity)", "Anchors"};
    st.fb.status = enabled_.count("XR_FB_spatial_entity_query") ? "not asked yet" : "XR_FB_spatial_entity_query not enabled";
    st.meta.status = enabled_.count("XR_META_spatial_entity_discovery") ? "not asked yet"
                                                                        : "XR_META_spatial_entity_discovery not enabled";
    st.ext.status = enabled_.count("XR_EXT_plane_detection") ? "not asked yet" : "XR_EXT_plane_detection not offered";
    st.anchors.status = "Squeeze a grip to place an anchor at that controller";
    load_saved_list();
}

// ---------------------------------------------------------------- session
void Mr::on_session(XrSession session, XrSpace base, XrSpace stage) {
    session_ = session;
    base_ = base;
    stage_ = stage;
    st.stage_space = stage != XR_NULL_HANDLE;

    uint32_t n = 0;
    xrEnumerateReferenceSpaces(session_, 0, &n, nullptr);
    std::vector<XrReferenceSpaceType> spaces(n);
    XrResult r = xrEnumerateReferenceSpaces(session_, n, &n, spaces.data());
    std::string list;
    for (XrReferenceSpaceType t : spaces) list += std::string(list.empty() ? "" : " ") + ref_space_name(t);
    MLOG("xrEnumerateReferenceSpaces -> %s: %s", xr_result_str(r), list.c_str());
    fact("Reference spaces", list);
    fact("STAGE space", stage ? "created" : "not available", stage ? 0 : 2);
    query_stage_bounds();

    if (enabled_.count("XR_EXT_plane_detection") && xrCreatePlaneDetectorEXT_) {
        XrPlaneDetectorCreateInfoEXT info{XR_TYPE_PLANE_DETECTOR_CREATE_INFO_EXT};
        info.flags = contour_ ? XR_PLANE_DETECTOR_ENABLE_CONTOUR_BIT_EXT : 0;
        r = xrCreatePlaneDetectorEXT_(session_, &info, &detector_);
        MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrCreatePlaneDetectorEXT(contour %d) -> %s", contour_ ? 1 : 0,
                         xr_result_str(r));
        if (XR_FAILED(r)) {
            detector_ = XR_NULL_HANDLE;
            st.ext.status = std::string("xrCreatePlaneDetectorEXT -> ") + xr_result_str(r);
            st.ext.status_level = 2;
        }
    }
}

void Mr::on_session_end() {
    if (detector_ && xrDestroyPlaneDetectorEXT_) xrDestroyPlaneDetectorEXT_(detector_);
    detector_ = XR_NULL_HANDLE;
    detecting_ = false;
    for (XrSpace s : owned_) xrDestroySpace(s);
    owned_.clear();
    for (RoomSet *set : {&st.fb, &st.meta, &st.ext, &st.anchors}) set->entities.clear();
    pending_.clear();
    session_ = XR_NULL_HANDLE;
    base_ = stage_ = XR_NULL_HANDLE;
}

void Mr::start() {
    MLOG("session running: asking for the room");
    refresh();
    if (!saved_uuids_.empty()) load_anchors(false);
}

void Mr::refresh() {
    query_stage_bounds();
    refresh_fb();
    refresh_meta();
    refresh_ext();
}

void Mr::query_stage_bounds() {
    if (!session_) return;
    XrExtent2Df b{};
    XrResult r = xrGetReferenceSpaceBoundsRect(session_, XR_REFERENCE_SPACE_TYPE_STAGE, &b);
    st.stage_bounds_result = r;
    st.stage_bounds = b;
    char buf[96];
    if (r == XR_SUCCESS) std::snprintf(buf, sizeof(buf), "%.2f x %.2f m", b.width, b.height);
    else std::snprintf(buf, sizeof(buf), "%s", xr_result_str(r));
    MrLog::get().add(r == XR_SUCCESS ? 0 : 1, "xrGetReferenceSpaceBoundsRect(STAGE) -> %s, %.2f x %.2f m",
                     xr_result_str(r), b.width, b.height);
    fact("STAGE bounds rect", buf, r == XR_SUCCESS && b.width > 1.0f ? 1 : 2);
}

// ---------------------------------------------------------------- spaces
void Mr::track(XrAsyncRequestIdFB id, Req kind, RoomSet *set, XrSpace space, std::vector<XrUuidEXT> uuids) {
    Pending p{kind, MrLog::get().clock(), set, space, std::move(uuids)};
    pending_[id] = p;
}

void Mr::release_unused_spaces() {
    std::set<XrSpace> used;
    for (RoomSet *set : {&st.fb, &st.meta, &st.ext, &st.anchors})
        for (const Entity &e : set->entities) used.insert(e.space);
    for (auto it = owned_.begin(); it != owned_.end();) {
        if (!used.count(*it)) {
            xrDestroySpace(*it);
            it = owned_.erase(it);
        } else {
            ++it;
        }
    }
}

void Mr::clear_set(RoomSet &set) {
    set.entities.clear();
    release_unused_spaces();
}

Entity &Mr::add_entity(RoomSet &set, XrSpace space, const XrUuidEXT *uuid) {
    int i = uuid ? set.find(*uuid) : -1;
    if (i < 0) i = set.find_space(space);
    if (i < 0) {
        set.entities.emplace_back();
        i = (int)set.entities.size() - 1;
    }
    Entity &e = set.entities[i];
    e.space = space;
    if (uuid) {
        e.uuid = *uuid;
        e.has_uuid = true;
    }
    if (space) owned_.insert(space);
    return e;
}

XrResult Mr::call(Entity &e, const char *name, XrResult r) {
    bool seen = false;
    for (CallResult &c : e.calls)
        if (c.call == name) c.result = r, seen = true;
    if (!seen) e.calls.push_back({name, r});
    if (failed_badly(r)) ++e.failures;
    if (XR_FAILED(r))
        MrLog::get().add(failed_badly(r) ? 2 : 1, "  %s(%s) -> %s", name,
                         e.has_uuid ? short_uuid(e.uuid).c_str() : "?", xr_result_str(r));
    return r;
}

// Reads everything an entity offers. Calls a getter only for components the runtime says the space supports, unless
// it wouldn't say (then every getter is tried, so the panel shows what each one answers).
void Mr::read_entity(Entity &e, bool anchor) {
    e.failures = 0;
    if (!e.has_uuid && xrGetSpaceUuidFB_) {
        if (XR_SUCCEEDED(call(e, "xrGetSpaceUuidFB", xrGetSpaceUuidFB_(e.space, &e.uuid)))) e.has_uuid = true;
    }
    bool listed = false;
    if (xrEnumerateSpaceSupportedComponentsFB_) {
        uint32_t n = 0;
        XrResult r = xrEnumerateSpaceSupportedComponentsFB_(e.space, 0, &n, nullptr);
        if (XR_SUCCEEDED(r)) {
            e.components.assign(n, XR_SPACE_COMPONENT_TYPE_MAX_ENUM_FB);
            r = xrEnumerateSpaceSupportedComponentsFB_(e.space, n, &n, e.components.data());
            e.components.resize(XR_SUCCEEDED(r) ? n : 0);
        }
        listed = XR_SUCCEEDED(call(e, "xrEnumerateSpaceSupportedComponentsFB", r));
    }
    auto supported = [&](XrSpaceComponentTypeFB t) { return !listed || has(e.components, t); };

    e.enabled.clear();
    if (listed && xrGetSpaceComponentStatusFB_) {
        for (XrSpaceComponentTypeFB t : e.components) {
            XrSpaceComponentStatusFB status{XR_TYPE_SPACE_COMPONENT_STATUS_FB};
            XrResult r = xrGetSpaceComponentStatusFB_(e.space, t, &status);
            if (XR_FAILED(r)) {
                call(e, (std::string("xrGetSpaceComponentStatusFB ") + component_name(t)).c_str(), r);
                continue;
            }
            if (status.enabled) e.enabled.push_back(t);
            // LOCATABLE (and STORABLE for anchors) must be on before a space can be located (saved)
            bool want = t == XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB || (anchor && t == XR_SPACE_COMPONENT_TYPE_STORABLE_FB);
            if (want && !status.enabled && !status.changePending && xrSetSpaceComponentStatusFB_) {
                XrSpaceComponentStatusSetInfoFB set{XR_TYPE_SPACE_COMPONENT_STATUS_SET_INFO_FB};
                set.componentType = t;
                set.enabled = XR_TRUE;
                XrAsyncRequestIdFB id = 0;
                XrResult sr = xrSetSpaceComponentStatusFB_(e.space, &set, &id);
                MrLog::get().add(XR_FAILED(sr) ? 2 : 0, "  xrSetSpaceComponentStatusFB(%s, %s on) -> %s request %llu",
                                 short_uuid(e.uuid).c_str(), component_name(t), xr_result_str(sr),
                                 (unsigned long long)id);
                if (XR_SUCCEEDED(sr)) track(id, Req::SetStatus, nullptr, e.space);
                else call(e, "xrSetSpaceComponentStatusFB", sr);
            }
        }
    }

    if (supported(XR_SPACE_COMPONENT_TYPE_SEMANTIC_LABELS_FB) && xrGetSpaceSemanticLabelsFB_) {
        XrSemanticLabelsSupportInfoFB support{XR_TYPE_SEMANTIC_LABELS_SUPPORT_INFO_FB};
        support.flags = XR_SEMANTIC_LABELS_SUPPORT_MULTIPLE_SEMANTIC_LABELS_BIT_FB |
                        XR_SEMANTIC_LABELS_SUPPORT_ACCEPT_DESK_TO_TABLE_MIGRATION_BIT_FB |
                        XR_SEMANTIC_LABELS_SUPPORT_ACCEPT_INVISIBLE_WALL_FACE_BIT_FB;
        support.recognizedLabels = kAllLabels;
        XrSemanticLabelsFB labels{XR_TYPE_SEMANTIC_LABELS_FB};
        labels.next = &support;
        XrResult r = xrGetSpaceSemanticLabelsFB_(session_, e.space, &labels);
        if (XR_SUCCEEDED(r) && labels.bufferCountOutput) {
            std::vector<char> buf(labels.bufferCountOutput + 1, 0);
            labels.bufferCapacityInput = labels.bufferCountOutput;
            labels.buffer = buf.data();
            r = xrGetSpaceSemanticLabelsFB_(session_, e.space, &labels);
            if (XR_SUCCEEDED(r)) e.labels = buf.data();
        }
        call(e, "xrGetSpaceSemanticLabelsFB", r);
    }
    if (supported(XR_SPACE_COMPONENT_TYPE_BOUNDED_2D_FB)) {
        if (xrGetSpaceBoundingBox2DFB_) {
            XrResult r = xrGetSpaceBoundingBox2DFB_(session_, e.space, &e.b2d);
            e.has_b2d = XR_SUCCEEDED(call(e, "xrGetSpaceBoundingBox2DFB", r));
        }
        if (xrGetSpaceBoundary2DFB_) {
            XrBoundary2DFB b{XR_TYPE_BOUNDARY_2D_FB};
            XrResult r = xrGetSpaceBoundary2DFB_(session_, e.space, &b);
            e.boundary.clear();
            if (XR_SUCCEEDED(r) && b.vertexCountOutput) {
                e.boundary.resize(b.vertexCountOutput);
                b.vertexCapacityInput = b.vertexCountOutput;
                b.vertices = e.boundary.data();
                r = xrGetSpaceBoundary2DFB_(session_, e.space, &b);
                if (XR_FAILED(r)) e.boundary.clear();
            }
            call(e, "xrGetSpaceBoundary2DFB", r);
        }
    }
    if (supported(XR_SPACE_COMPONENT_TYPE_BOUNDED_3D_FB) && xrGetSpaceBoundingBox3DFB_) {
        XrResult r = xrGetSpaceBoundingBox3DFB_(session_, e.space, &e.b3d);
        e.has_b3d = XR_SUCCEEDED(call(e, "xrGetSpaceBoundingBox3DFB", r));
    }
    if (supported(XR_SPACE_COMPONENT_TYPE_ROOM_LAYOUT_FB) && xrGetSpaceRoomLayoutFB_) {
        XrRoomLayoutFB layout{XR_TYPE_ROOM_LAYOUT_FB};
        XrResult r = xrGetSpaceRoomLayoutFB_(session_, e.space, &layout);
        if (XR_SUCCEEDED(r)) {
            e.walls.assign(layout.wallUuidCountOutput, XrUuidEXT{});
            layout.wallUuidCapacityInput = layout.wallUuidCountOutput;
            layout.wallUuids = e.walls.data();
            if (layout.wallUuidCapacityInput) r = xrGetSpaceRoomLayoutFB_(session_, e.space, &layout);
            e.floor = layout.floorUuid;
            e.ceiling = layout.ceilingUuid;
            e.is_room = XR_SUCCEEDED(r);
        }
        call(e, "xrGetSpaceRoomLayoutFB", r);
    }
    if (supported(XR_SPACE_COMPONENT_TYPE_SPACE_CONTAINER_FB) && xrGetSpaceContainerFB_) {
        XrSpaceContainerFB c{XR_TYPE_SPACE_CONTAINER_FB};
        XrResult r = xrGetSpaceContainerFB_(session_, e.space, &c);
        if (XR_SUCCEEDED(r)) {
            e.contained.assign(c.uuidCountOutput, XrUuidEXT{});
            c.uuidCapacityInput = c.uuidCountOutput;
            c.uuids = e.contained.data();
            if (c.uuidCapacityInput) r = xrGetSpaceContainerFB_(session_, e.space, &c);
        }
        call(e, "xrGetSpaceContainerFB", r);
    }
    if (supported(XR_SPACE_COMPONENT_TYPE_TRIANGLE_MESH_META) && xrGetSpaceTriangleMeshMETA_ &&
        (listed || !anchor)) {
        XrSpaceTriangleMeshGetInfoMETA info{XR_TYPE_SPACE_TRIANGLE_MESH_GET_INFO_META};
        XrSpaceTriangleMeshMETA mesh{XR_TYPE_SPACE_TRIANGLE_MESH_META};
        XrResult r = xrGetSpaceTriangleMeshMETA_(e.space, &info, &mesh);
        if (XR_SUCCEEDED(r) && mesh.vertexCountOutput && mesh.indexCountOutput) {
            e.mesh_vertices.resize(mesh.vertexCountOutput);
            e.mesh_indices.resize(mesh.indexCountOutput);
            mesh.vertexCapacityInput = mesh.vertexCountOutput;
            mesh.indexCapacityInput = mesh.indexCountOutput;
            mesh.vertices = e.mesh_vertices.data();
            mesh.indices = e.mesh_indices.data();
            r = xrGetSpaceTriangleMeshMETA_(e.space, &info, &mesh);
            if (XR_FAILED(r)) {
                e.mesh_vertices.clear();
                e.mesh_indices.clear();
            } else {
                // drop indices that point past the vertices instead of drawing garbage
                size_t bad = 0;
                for (uint32_t &i : e.mesh_indices)
                    if (i >= e.mesh_vertices.size()) i = 0, ++bad;
                if (bad) MERR("  mesh %s: %zu indices past the %zu vertices", short_uuid(e.uuid).c_str(), bad,
                              e.mesh_vertices.size());
            }
            ++e.mesh_generation;
            MLOG("  mesh %s: %zu vertices, %zu triangles", short_uuid(e.uuid).c_str(), e.mesh_vertices.size(),
                 e.mesh_indices.size() / 3);
        }
        call(e, "xrGetSpaceTriangleMeshMETA", r);
        if (!e.mesh_indices.empty()) test_triangle_mesh(e);
    }

    std::string comps;
    for (XrSpaceComponentTypeFB t : e.components)
        comps += std::string(comps.empty() ? "" : " ") + component_name(t) + (has(e.enabled, t) ? "" : "(off)");
    MLOG("  space %s: [%s] %s%s%s%s%s", e.has_uuid ? short_uuid(e.uuid).c_str() : "?", e.labels.c_str(),
         comps.c_str(), e.has_b2d ? " 2D" : "", e.has_b3d ? " 3D" : "", e.is_room ? " room" : "",
         e.failures ? " (failures)" : "");
}

// XR_FB_triangle_mesh: the room mesh handed back to the runtime the way passthrough-geometry apps do.
void Mr::test_triangle_mesh(const Entity &e) {
    if (!xrCreateTriangleMeshFB_ || !xrDestroyTriangleMeshFB_) {
        st.triangle_mesh_status = enabled_.count("XR_FB_triangle_mesh") ? "functions missing" : "not offered";
        return;
    }
    XrTriangleMeshCreateInfoFB info{XR_TYPE_TRIANGLE_MESH_CREATE_INFO_FB};
    info.windingOrder = XR_WINDING_ORDER_CCW_FB;
    info.vertexCount = (uint32_t)e.mesh_vertices.size();
    info.vertexBuffer = e.mesh_vertices.data();
    info.triangleCount = (uint32_t)(e.mesh_indices.size() / 3);
    info.indexBuffer = e.mesh_indices.data();
    XrTriangleMeshFB mesh = XR_NULL_HANDLE;
    XrResult r = xrCreateTriangleMeshFB_(session_, &info, &mesh);
    MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrCreateTriangleMeshFB(%u triangles of the room mesh) -> %s",
                     info.triangleCount, xr_result_str(r));
    char buf[128];
    std::snprintf(buf, sizeof(buf), "xrCreateTriangleMeshFB(%u tris) -> %s", info.triangleCount, xr_result_str(r));
    st.triangle_mesh_status = buf;
    if (XR_SUCCEEDED(r) && mesh) xrDestroyTriangleMeshFB_(mesh);
}

// ---------------------------------------------------------------- FB query
void Mr::refresh_fb() {
    if (!session_) return;
    if (!xrQuerySpacesFB_ || !xrRetrieveSpaceQueryResultsFB_) {
        st.fb.status = enabled_.count("XR_FB_spatial_entity_query") ? "xrQuerySpacesFB missing"
                                                                     : "XR_FB_spatial_entity_query not offered";
        st.fb.status_level = 2;
        return;
    }
    clear_set(st.fb);
    // Meta's flow: the rooms first (ROOM_LAYOUT), then their contents by UUID
    XrSpaceStorageLocationFilterInfoFB local{XR_TYPE_SPACE_STORAGE_LOCATION_FILTER_INFO_FB};
    local.location = XR_SPACE_STORAGE_LOCATION_LOCAL_FB;
    XrSpaceComponentFilterInfoFB filter{XR_TYPE_SPACE_COMPONENT_FILTER_INFO_FB};
    filter.next = &local;
    filter.componentType = XR_SPACE_COMPONENT_TYPE_ROOM_LAYOUT_FB;
    XrSpaceQueryInfoFB info{XR_TYPE_SPACE_QUERY_INFO_FB};
    info.queryAction = XR_SPACE_QUERY_ACTION_LOAD_FB;
    info.maxResultCount = 64;
    info.filter = (const XrSpaceFilterInfoBaseHeaderFB *)&filter;
    XrAsyncRequestIdFB id = 0;
    XrResult r = xrQuerySpacesFB_(session_, (const XrSpaceQueryInfoBaseHeaderFB *)&info, &id);
    MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrQuerySpacesFB(LOAD, component ROOM_LAYOUT, LOCAL) -> %s request %llu",
                     xr_result_str(r), (unsigned long long)id);
    ++st.fb.requests;
    if (XR_FAILED(r)) {
        st.fb.status = std::string("xrQuerySpacesFB -> ") + xr_result_str(r);
        st.fb.status_level = 2;
        return;
    }
    st.fb.status = "querying rooms...";
    st.fb.status_level = 0;
    track(id, Req::FbRooms, &st.fb);
}

void Mr::retrieve_fb(XrAsyncRequestIdFB id, Pending &p) {
    XrSpaceQueryResultsFB results{XR_TYPE_SPACE_QUERY_RESULTS_FB};
    XrResult r = xrRetrieveSpaceQueryResultsFB_(session_, id, &results);
    std::vector<XrSpaceQueryResultFB> list;
    if (XR_SUCCEEDED(r) && results.resultCountOutput) {
        list.assign(results.resultCountOutput, XrSpaceQueryResultFB{});
        results.resultCapacityInput = results.resultCountOutput;
        results.results = list.data();
        r = xrRetrieveSpaceQueryResultsFB_(session_, id, &results);
        list.resize(XR_SUCCEEDED(r) ? results.resultCountOutput : 0);
    }
    MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrRetrieveSpaceQueryResultsFB(request %llu) -> %s, %zu spaces",
                     (unsigned long long)id, xr_result_str(r), list.size());
    RoomSet &set = *p.set;
    if (XR_FAILED(r)) {
        set.status = std::string("xrRetrieveSpaceQueryResultsFB -> ") + xr_result_str(r);
        set.status_level = 2;
        return;
    }
    bool anchors = p.kind == Req::FbAnchors;
    for (const XrSpaceQueryResultFB &q : list) {
        Entity &e = add_entity(set, q.space, &q.uuid);
        e.calls.clear();
        if (anchors) {
            e.anchor = true;
            e.anchor_state = "loaded (FB)";
        }
        read_entity(e, anchors);
    }
    if (p.kind == Req::FbRooms) {
        int rooms = 0;
        for (const Entity &e : set.entities) rooms += e.is_room;
        if (!rooms) {
            set.status = "no room: the ROOM_LAYOUT query found nothing (set up the room: Scene capture)";
            set.status_level = 2;
        }
        query_children(set);
    } else if (!anchors) {
        finish_set(set, "query");
    } else {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "loaded %zu saved anchors through xrQuerySpacesFB", list.size());
        set.status = buf;
        set.status_level = list.empty() ? 2 : 1;
    }
}

void Mr::query_children(RoomSet &set) {
    std::vector<XrUuidEXT> uuids;
    auto add = [&](const XrUuidEXT &u) {
        static const XrUuidEXT zero{};
        if (uuid_eq(u, zero)) return;
        for (const XrUuidEXT &x : uuids)
            if (uuid_eq(x, u)) return;
        if (set.find(u) < 0) uuids.push_back(u);
    };
    for (const Entity &e : set.entities) {
        if (!e.is_room && e.contained.empty()) continue;
        for (const XrUuidEXT &u : e.contained) add(u);
        add(e.floor);
        add(e.ceiling);
        for (const XrUuidEXT &u : e.walls) add(u);
    }
    if (uuids.empty()) {
        finish_set(set, "query");
        return;
    }
    XrSpaceStorageLocationFilterInfoFB local{XR_TYPE_SPACE_STORAGE_LOCATION_FILTER_INFO_FB};
    local.location = XR_SPACE_STORAGE_LOCATION_LOCAL_FB;
    XrSpaceUuidFilterInfoFB filter{XR_TYPE_SPACE_UUID_FILTER_INFO_FB};
    filter.next = &local;
    filter.uuidCount = (uint32_t)uuids.size();
    filter.uuids = uuids.data();
    XrSpaceQueryInfoFB info{XR_TYPE_SPACE_QUERY_INFO_FB};
    info.queryAction = XR_SPACE_QUERY_ACTION_LOAD_FB;
    info.maxResultCount = (uint32_t)uuids.size();
    info.filter = (const XrSpaceFilterInfoBaseHeaderFB *)&filter;
    XrAsyncRequestIdFB id = 0;
    XrResult r = xrQuerySpacesFB_(session_, (const XrSpaceQueryInfoBaseHeaderFB *)&info, &id);
    MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrQuerySpacesFB(LOAD, %zu room UUIDs) -> %s request %llu", uuids.size(),
                     xr_result_str(r), (unsigned long long)id);
    ++set.requests;
    if (XR_FAILED(r)) {
        set.status = std::string("contents query: xrQuerySpacesFB -> ") + xr_result_str(r);
        set.status_level = 2;
        return;
    }
    set.status = "querying the room's contents...";
    track(id, Req::FbChildren, &set, XR_NULL_HANDLE, uuids);
}

void Mr::finish_set(RoomSet &set, const char *how) {
    set.assign_roles();
    set.updated = MrLog::get().clock();
    int rooms = 0, floors = 0, ceilings = 0, walls = 0, planes = 0, volumes = 0, meshes = 0, anchors = 0, failures = 0;
    size_t tris = 0;
    for (const Entity &e : set.entities) {
        rooms += e.is_room;
        if (e.role == "floor") ++floors;
        else if (e.role == "ceiling") ++ceilings;
        else if (e.role.rfind("wall", 0) == 0) ++walls;
        else if (e.has_b3d) ++volumes;
        else if (e.has_b2d && !e.is_room) ++planes;
        else if (!e.is_room && e.mesh_indices.empty() && e.labels.empty()) ++anchors;  // e.g. saved anchors
        if (!e.mesh_indices.empty()) ++meshes, tris += e.mesh_indices.size() / 3;
        failures += e.failures;
    }
    if (set.entities.empty()) {
        set.status = "nothing found (is a room set up? Scene capture asks for one)";
        set.status_level = 2;
        MLOG("%s %s: nothing found", set.short_name.c_str(), how);
        return;
    }
    char buf[240];
    std::snprintf(buf, sizeof(buf),
                  "%zu spaces: %d room, %d floor, %d ceiling, %d walls, %d other planes, %d volumes, %d mesh (%zu tris)"
                  "%s%s",
                  set.entities.size(), rooms, floors, ceilings, walls, planes, volumes, meshes, tris,
                  anchors ? (", " + std::to_string(anchors) + " other").c_str() : "",
                  failures ? " - some calls failed" : "");
    set.status = buf;
    set.status_level = failures ? 2 : 1;
    if (!rooms && !set.entities.empty() && set.short_name != "EXT")
        set.status += " - no ROOM_LAYOUT space", set.status_level = 2;
    MLOG("%s %s: %s", set.short_name.c_str(), how, buf);
    dump_set(set, set.short_name == "FB" ? "room-fb.txt" : set.short_name == "META" ? "room-meta.txt" : "room-ext.txt");
}

// ---------------------------------------------------------------- META discovery
void Mr::refresh_meta() {
    if (!session_) return;
    if (!xrDiscoverSpacesMETA_ || !xrRetrieveSpaceDiscoveryResultsMETA_) {
        st.meta.status = enabled_.count("XR_META_spatial_entity_discovery") ? "xrDiscoverSpacesMETA missing"
                                                                             : "XR_META_spatial_entity_discovery not offered";
        st.meta.status_level = 2;
        return;
    }
    clear_set(st.meta);
    XrSpaceDiscoveryInfoMETA info{XR_TYPE_SPACE_DISCOVERY_INFO_META};  // no filter: everything
    XrAsyncRequestIdFB id = 0;
    XrResult r = xrDiscoverSpacesMETA_(session_, &info, &id);
    MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrDiscoverSpacesMETA(no filter) -> %s request %llu", xr_result_str(r),
                     (unsigned long long)id);
    ++st.meta.requests;
    if (XR_FAILED(r)) {
        st.meta.status = std::string("xrDiscoverSpacesMETA -> ") + xr_result_str(r);
        st.meta.status_level = 2;
        return;
    }
    st.meta.status = "discovering...";
    st.meta.status_level = 0;
    track(id, Req::MetaAll, &st.meta);
}

void Mr::retrieve_meta(XrAsyncRequestIdFB id, Pending &p) {
    XrSpaceDiscoveryResultsMETA results{XR_TYPE_SPACE_DISCOVERY_RESULTS_META};
    XrResult r = xrRetrieveSpaceDiscoveryResultsMETA_(session_, id, &results);
    std::vector<XrSpaceDiscoveryResultMETA> list;
    if (XR_SUCCEEDED(r) && results.resultCountOutput) {
        list.assign(results.resultCountOutput, XrSpaceDiscoveryResultMETA{});
        results.resultCapacityInput = results.resultCountOutput;
        results.results = list.data();
        r = xrRetrieveSpaceDiscoveryResultsMETA_(session_, id, &results);
        list.resize(XR_SUCCEEDED(r) ? results.resultCountOutput : 0);
    }
    MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrRetrieveSpaceDiscoveryResultsMETA(request %llu) -> %s, %zu spaces",
                     (unsigned long long)id, xr_result_str(r), list.size());
    RoomSet &set = *p.set;
    if (XR_FAILED(r)) {
        set.status = std::string("xrRetrieveSpaceDiscoveryResultsMETA -> ") + xr_result_str(r);
        set.status_level = 2;
        return;
    }
    bool anchors = p.kind == Req::MetaAnchors;
    for (const XrSpaceDiscoveryResultMETA &d : list) {
        Entity &e = add_entity(set, d.space, &d.uuid);
        e.calls.clear();
        if (anchors) {
            e.anchor = true;
            e.anchor_state = "loaded (META)";
        }
        read_entity(e, anchors);
    }
    if (anchors) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "loaded %zu saved anchors through xrDiscoverSpacesMETA", list.size());
        set.status = buf;
        set.status_level = list.empty() ? 2 : 1;
    } else {
        finish_set(set, "discovery");
    }
}

// ---------------------------------------------------------------- EXT plane detection
void Mr::refresh_ext() {
    if (!session_ || !detector_ || !xrBeginPlaneDetectionEXT_) return;
    if (!time_) {  // needs a frame time: the first update() starts it
        ext_pending_ = true;
        return;
    }
    ext_pending_ = false;
    clear_set(st.ext);
    XrPlaneDetectorBeginInfoEXT info{XR_TYPE_PLANE_DETECTOR_BEGIN_INFO_EXT};
    info.baseSpace = base_;
    info.time = time_;
    info.maxPlanes = 100;
    info.minArea = 0.05f;
    info.boundingBoxPose = Pose().xr();
    info.boundingBoxExtent = {20, 20, 20};
    XrResult r = xrBeginPlaneDetectionEXT_(detector_, &info);
    MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrBeginPlaneDetectionEXT(20 m box, 100 planes) -> %s", xr_result_str(r));
    ++st.ext.requests;
    detecting_ = XR_SUCCEEDED(r);
    st.ext.status = detecting_ ? "detecting..." : std::string("xrBeginPlaneDetectionEXT -> ") + xr_result_str(r);
    st.ext.status_level = detecting_ ? 0 : 2;
}

void Mr::poll_planes() {
    if (!detecting_ || !xrGetPlaneDetectionStateEXT_) return;
    XrPlaneDetectionStateEXT state = XR_PLANE_DETECTION_STATE_NONE_EXT;
    XrResult r = xrGetPlaneDetectionStateEXT_(detector_, &state);
    if (XR_FAILED(r) || state == XR_PLANE_DETECTION_STATE_ERROR_EXT || state == XR_PLANE_DETECTION_STATE_FATAL_EXT) {
        detecting_ = false;
        MERR("xrGetPlaneDetectionStateEXT -> %s, state %d", xr_result_str(r), (int)state);
        st.ext.status = std::string("plane detection failed: ") + xr_result_str(r) + " state " + std::to_string(state);
        st.ext.status_level = 2;
        return;
    }
    if (state != XR_PLANE_DETECTION_STATE_DONE_EXT) return;
    detecting_ = false;
    XrPlaneDetectorGetInfoEXT get{XR_TYPE_PLANE_DETECTOR_GET_INFO_EXT};
    get.baseSpace = base_;
    get.time = time_;
    XrPlaneDetectorLocationsEXT locations{XR_TYPE_PLANE_DETECTOR_LOCATIONS_EXT};
    r = xrGetPlaneDetectionsEXT_(detector_, &get, &locations);
    std::vector<XrPlaneDetectorLocationEXT> planes;
    if (XR_SUCCEEDED(r) && locations.planeLocationCountOutput) {
        planes.assign(locations.planeLocationCountOutput, {XR_TYPE_PLANE_DETECTOR_LOCATION_EXT});
        locations.planeLocationCapacityInput = locations.planeLocationCountOutput;
        locations.planeLocations = planes.data();
        r = xrGetPlaneDetectionsEXT_(detector_, &get, &locations);
        planes.resize(XR_SUCCEEDED(r) ? locations.planeLocationCountOutput : 0);
    }
    MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrGetPlaneDetectionsEXT -> %s, %zu planes", xr_result_str(r), planes.size());
    for (const XrPlaneDetectorLocationEXT &p : planes) {
        st.ext.entities.emplace_back();
        Entity &e = st.ext.entities.back();
        e.ext_plane_id = p.planeId;
        e.ext_semantic = p.semanticType;
        e.labels = plane_semantic_name(p.semanticType);
        e.role = e.labels;
        e.flags = p.locationFlags;
        e.located = (p.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
        e.locate_result = XR_SUCCESS;
        e.pose = Pose(p.pose);
        e.has_b2d = true;
        e.b2d = {{-p.extents.width / 2, -p.extents.height / 2}, {p.extents.width, p.extents.height}};
        if (p.polygonBufferCount && xrGetPlanePolygonBufferEXT_) {
            XrPlaneDetectorPolygonBufferEXT buf{XR_TYPE_PLANE_DETECTOR_POLYGON_BUFFER_EXT};
            XrResult pr = xrGetPlanePolygonBufferEXT_(detector_, p.planeId, 0, &buf);
            if (XR_SUCCEEDED(pr) && buf.vertexCountOutput) {
                e.boundary.resize(buf.vertexCountOutput);
                buf.vertexCapacityInput = buf.vertexCountOutput;
                buf.vertices = e.boundary.data();
                pr = xrGetPlanePolygonBufferEXT_(detector_, p.planeId, 0, &buf);
                if (XR_FAILED(pr)) e.boundary.clear();
            }
            call(e, "xrGetPlanePolygonBufferEXT", pr);
        }
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%zu planes detected", planes.size());
    st.ext.status = XR_SUCCEEDED(r) ? buf : std::string("xrGetPlaneDetectionsEXT -> ") + xr_result_str(r);
    st.ext.status_level = XR_SUCCEEDED(r) && !planes.empty() ? 1 : 2;
    st.ext.updated = MrLog::get().clock();
    dump_set(st.ext, "room-ext.txt");
}

// ---------------------------------------------------------------- per frame
void Mr::update(XrTime time) {
    time_ = time;
    if (!session_) return;
    for (RoomSet *set : {&st.fb, &st.meta, &st.anchors})
        for (Entity &e : set->entities) {
            if (!e.space) continue;
            XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
            XrResult r = xrLocateSpace(e.space, base_, time, &loc);
            bool ok = XR_SUCCEEDED(r) && (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                      (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
            if (r != e.locate_result || ok != e.located)
                MrLog::get().add(ok ? 0 : 1, "xrLocateSpace(%s %s) -> %s, flags %s", set->short_name.c_str(),
                                 e.has_uuid ? short_uuid(e.uuid).c_str() : "?", xr_result_str(r),
                                 location_flags_str(loc.locationFlags).c_str());
            e.locate_result = r;
            e.flags = XR_SUCCEEDED(r) ? loc.locationFlags : 0;
            e.located = ok;
            if (ok) e.pose = Pose(loc.pose);
        }
    if (stage_) {
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        XrResult r = xrLocateSpace(stage_, base_, time, &loc);
        st.stage_located = XR_SUCCEEDED(r) && (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
        if (st.stage_located) st.stage_pose = Pose(loc.pose);
    }
    if (ext_pending_) refresh_ext();
    poll_planes();
    // requests that never answered
    double now = MrLog::get().clock();
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (now - it->second.started > 20 && it->second.kind != Req::Capture) {
            MWARN("request %llu: no answer after 20 s", (unsigned long long)it->first);
            if (it->second.set && it->second.kind != Req::SetStatus) {
                it->second.set->status = "no answer after 20 s (request " + std::to_string(it->first) + ")";
                it->second.set->status_level = 2;
            }
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------- events
bool Mr::handle_event(const XrEventDataBuffer &ev) {
    auto take = [&](XrAsyncRequestIdFB id, Pending &out) {
        auto it = pending_.find(id);
        if (it == pending_.end()) return false;
        out = it->second;
        pending_.erase(it);
        return true;
    };
    switch (ev.type) {
        case XR_TYPE_EVENT_DATA_SPACE_QUERY_RESULTS_AVAILABLE_FB: {
            auto *e = (const XrEventDataSpaceQueryResultsAvailableFB *)&ev;
            MLOG("event SPACE_QUERY_RESULTS_AVAILABLE_FB request %llu", (unsigned long long)e->requestId);
            auto it = pending_.find(e->requestId);
            if (it == pending_.end()) {
                MWARN("  (no such request)");
                return true;
            }
            Pending p = it->second;  // a copy: retrieving can start new requests
            retrieve_fb(e->requestId, p);
            return true;
        }
        case XR_TYPE_EVENT_DATA_SPACE_QUERY_COMPLETE_FB: {
            auto *e = (const XrEventDataSpaceQueryCompleteFB *)&ev;
            MrLog::get().add(XR_FAILED(e->result) ? 2 : 0, "event SPACE_QUERY_COMPLETE_FB request %llu -> %s",
                             (unsigned long long)e->requestId, xr_result_str(e->result));
            Pending p{};
            if (take(e->requestId, p) && p.set) {
                // a query that found nothing may complete without a results-available event
                if (p.kind == Req::FbRooms && p.set->entities.empty()) {
                    p.set->status = XR_SUCCEEDED(e->result) || e->result == XR_ERROR_SPACE_COMPONENT_NOT_ENABLED_FB
                                        ? "no room: the ROOM_LAYOUT query found nothing (set up the room: Scene capture)"
                                        : std::string("room query completed with ") + xr_result_str(e->result);
                    p.set->status_level = 2;
                    p.set->updated = MrLog::get().clock();
                } else if (XR_FAILED(e->result)) {
                    p.set->status = std::string("query completed with ") + xr_result_str(e->result);
                    p.set->status_level = 2;
                } else if (p.kind == Req::FbChildren && p.set->status.rfind("querying", 0) == 0) {
                    finish_set(*p.set, "query");
                } else if (p.kind == Req::FbAnchors && p.set->status.rfind("loading", 0) == 0) {
                    p.set->status = "xrQuerySpacesFB found none of the saved anchors";
                    p.set->status_level = 2;
                }
            }
            return true;
        }
        case XR_TYPE_EVENT_DATA_SPACE_DISCOVERY_RESULTS_AVAILABLE_META: {
            auto *e = (const XrEventDataSpaceDiscoveryResultsAvailableMETA *)&ev;
            MLOG("event SPACE_DISCOVERY_RESULTS_AVAILABLE_META request %llu", (unsigned long long)e->requestId);
            auto it = pending_.find(e->requestId);
            if (it == pending_.end()) {
                MWARN("  (no such request)");
                return true;
            }
            Pending p = it->second;
            retrieve_meta(e->requestId, p);
            return true;
        }
        case XR_TYPE_EVENT_DATA_SPACE_DISCOVERY_COMPLETE_META: {
            auto *e = (const XrEventDataSpaceDiscoveryCompleteMETA *)&ev;
            MrLog::get().add(XR_FAILED(e->result) ? 2 : 0, "event SPACE_DISCOVERY_COMPLETE_META request %llu -> %s",
                             (unsigned long long)e->requestId, xr_result_str(e->result));
            Pending p{};
            if (take(e->requestId, p) && p.set) {
                if (XR_FAILED(e->result)) {
                    p.set->status = std::string("discovery completed with ") + xr_result_str(e->result);
                    p.set->status_level = 2;
                } else if (p.kind == Req::MetaAnchors && p.set->status.rfind("loading", 0) == 0) {
                    p.set->status = "xrDiscoverSpacesMETA found none of the saved anchors";
                    p.set->status_level = 2;
                } else if (p.set->status.rfind("discovering", 0) == 0) {
                    p.set->status = "discovery found nothing";
                    p.set->status_level = 2;
                    p.set->updated = MrLog::get().clock();
                }
            }
            return true;
        }
        case XR_TYPE_EVENT_DATA_SPACE_SET_STATUS_COMPLETE_FB: {
            auto *e = (const XrEventDataSpaceSetStatusCompleteFB *)&ev;
            MrLog::get().add(XR_FAILED(e->result) && e->result != XR_ERROR_SPACE_COMPONENT_STATUS_ALREADY_SET_FB ? 2 : 0,
                             "event SPACE_SET_STATUS_COMPLETE_FB request %llu %s %s=%d -> %s",
                             (unsigned long long)e->requestId, short_uuid(e->uuid).c_str(),
                             component_name(e->componentType), e->enabled, xr_result_str(e->result));
            Pending p{};
            take(e->requestId, p);
            // the space's component list in every set that has it
            for (RoomSet *set : {&st.fb, &st.meta, &st.anchors}) {
                int i = set->find_space(e->space);
                if (i >= 0) {
                    Entity &ent = set->entities[i];
                    if (!has(ent.enabled, e->componentType) && e->enabled && XR_SUCCEEDED(e->result))
                        ent.enabled.push_back(e->componentType);
                }
            }
            return true;
        }
        case XR_TYPE_EVENT_DATA_SPATIAL_ANCHOR_CREATE_COMPLETE_FB: {
            auto *e = (const XrEventDataSpatialAnchorCreateCompleteFB *)&ev;
            MrLog::get().add(XR_FAILED(e->result) ? 2 : 0,
                             "event SPATIAL_ANCHOR_CREATE_COMPLETE_FB request %llu -> %s, anchor %s",
                             (unsigned long long)e->requestId, xr_result_str(e->result), short_uuid(e->uuid).c_str());
            Pending p{};
            take(e->requestId, p);
            if (XR_SUCCEEDED(e->result) && e->space) {
                Entity &a = add_entity(st.anchors, e->space, &e->uuid);
                a.anchor = true;
                a.anchor_state = "created";
                read_entity(a, true);
                st.anchors.status = "anchor " + short_uuid(e->uuid) + " created";
                st.anchors.status_level = 1;
            } else {
                st.anchors.status = std::string("anchor creation failed: ") + xr_result_str(e->result);
                st.anchors.status_level = 2;
            }
            return true;
        }
        case XR_TYPE_EVENT_DATA_SPACE_SAVE_COMPLETE_FB: {
            auto *e = (const XrEventDataSpaceSaveCompleteFB *)&ev;
            MrLog::get().add(XR_FAILED(e->result) ? 2 : 0, "event SPACE_SAVE_COMPLETE_FB request %llu %s -> %s",
                             (unsigned long long)e->requestId, short_uuid(e->uuid).c_str(), xr_result_str(e->result));
            Pending p{};
            take(e->requestId, p);
            int i = st.anchors.find(e->uuid);
            if (i >= 0)
                st.anchors.entities[i].anchor_state =
                    XR_SUCCEEDED(e->result) ? "saved (FB)" : std::string("save failed: ") + xr_result_str(e->result);
            if (XR_SUCCEEDED(e->result)) {
                std::string hex = uuid_hex(e->uuid);
                if (std::find(saved_uuids_.begin(), saved_uuids_.end(), hex) == saved_uuids_.end())
                    saved_uuids_.push_back(hex);
                store_saved_list();
            }
            return true;
        }
        case XR_TYPE_EVENT_DATA_SPACE_ERASE_COMPLETE_FB: {
            auto *e = (const XrEventDataSpaceEraseCompleteFB *)&ev;
            MrLog::get().add(XR_FAILED(e->result) ? 2 : 0, "event SPACE_ERASE_COMPLETE_FB request %llu %s -> %s",
                             (unsigned long long)e->requestId, short_uuid(e->uuid).c_str(), xr_result_str(e->result));
            Pending p{};
            take(e->requestId, p);
            int i = st.anchors.find(e->uuid);
            if (i >= 0)
                st.anchors.entities[i].anchor_state =
                    XR_SUCCEEDED(e->result) ? "erased (FB)" : std::string("erase failed: ") + xr_result_str(e->result);
            if (XR_SUCCEEDED(e->result)) {
                saved_uuids_.erase(std::remove(saved_uuids_.begin(), saved_uuids_.end(), uuid_hex(e->uuid)),
                                   saved_uuids_.end());
                store_saved_list();
            }
            return true;
        }
        case XR_TYPE_EVENT_DATA_SPACES_SAVE_RESULT_META:
        case XR_TYPE_EVENT_DATA_SPACES_ERASE_RESULT_META: {
            bool save = ev.type == XR_TYPE_EVENT_DATA_SPACES_SAVE_RESULT_META;
            auto *e = (const XrEventDataSpacesSaveResultMETA *)&ev;  // same layout for both
            MrLog::get().add(XR_FAILED(e->result) ? 2 : 0, "event SPACES_%s_RESULT_META request %llu -> %s",
                             save ? "SAVE" : "ERASE", (unsigned long long)e->requestId, xr_result_str(e->result));
            Pending p{};
            if (!take(e->requestId, p)) return true;
            for (const XrUuidEXT &u : p.uuids) {
                int i = st.anchors.find(u);
                if (i >= 0)
                    st.anchors.entities[i].anchor_state =
                        XR_SUCCEEDED(e->result) ? (save ? "saved (META)" : "erased (META)")
                                                : std::string(save ? "save" : "erase") + " failed: " + xr_result_str(e->result);
                if (!XR_SUCCEEDED(e->result)) continue;
                std::string hex = uuid_hex(u);
                saved_uuids_.erase(std::remove(saved_uuids_.begin(), saved_uuids_.end(), hex), saved_uuids_.end());
                if (save) saved_uuids_.push_back(hex);
            }
            if (XR_SUCCEEDED(e->result)) store_saved_list();
            st.anchors.status = std::string(save ? "xrSaveSpacesMETA: " : "xrEraseSpacesMETA: ") + xr_result_str(e->result);
            st.anchors.status_level = XR_SUCCEEDED(e->result) ? 1 : 2;
            return true;
        }
        case XR_TYPE_EVENT_DATA_SCENE_CAPTURE_COMPLETE_FB: {
            auto *e = (const XrEventDataSceneCaptureCompleteFB *)&ev;
            MrLog::get().add(XR_FAILED(e->result) ? 2 : 0, "event SCENE_CAPTURE_COMPLETE_FB request %llu -> %s",
                             (unsigned long long)e->requestId, xr_result_str(e->result));
            Pending p{};
            take(e->requestId, p);
            st.capture_status = std::string("capture complete: ") + xr_result_str(e->result) + " - asked again";
            refresh();
            return true;
        }
        case XR_TYPE_EVENT_DATA_BOUNDARY_VISIBILITY_CHANGED_META: {
            auto *e = (const XrEventDataBoundaryVisibilityChangedMETA *)&ev;
            st.boundary_visibility = e->boundaryVisibility == XR_BOUNDARY_VISIBILITY_SUPPRESSED_META ? "suppressed"
                                     : e->boundaryVisibility == XR_BOUNDARY_VISIBILITY_NOT_SUPPRESSED_META
                                         ? "not suppressed"
                                         : "value " + std::to_string(e->boundaryVisibility);
            MLOG("event BOUNDARY_VISIBILITY_CHANGED_META -> %s", st.boundary_visibility.c_str());
            return true;
        }
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
            auto *e = (const XrEventDataReferenceSpaceChangePending *)&ev;
            MLOG("event REFERENCE_SPACE_CHANGE_PENDING %s (pose valid %d)", ref_space_name(e->referenceSpaceType),
                 e->poseValid);
            if (e->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE) query_stage_bounds();
            return true;
        }
        default:
            return false;
    }
}

// ---------------------------------------------------------------- actions
void Mr::scene_capture() {
    if (!session_) return;
    if (!xrRequestSceneCaptureFB_) {
        st.capture_status = enabled_.count("XR_FB_scene_capture") ? "xrRequestSceneCaptureFB missing"
                                                                  : "XR_FB_scene_capture not offered";
        MERR("scene capture: %s", st.capture_status.c_str());
        return;
    }
    XrSceneCaptureRequestInfoFB info{XR_TYPE_SCENE_CAPTURE_REQUEST_INFO_FB};
    XrAsyncRequestIdFB id = 0;
    XrResult r = xrRequestSceneCaptureFB_(session_, &info, &id);
    MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrRequestSceneCaptureFB -> %s request %llu", xr_result_str(r),
                     (unsigned long long)id);
    st.capture_status = XR_SUCCEEDED(r) ? "requested, waiting for the event..."
                                        : std::string("xrRequestSceneCaptureFB -> ") + xr_result_str(r);
    if (XR_SUCCEEDED(r)) track(id, Req::Capture);
}

void Mr::request_boundary(bool suppress) {
    if (!session_) return;
    st.boundary_suppress_requested = suppress;
    if (!xrRequestBoundaryVisibilityMETA_) {
        MERR("boundary visibility: XR_META_boundary_visibility not offered");
        st.boundary_request_result = XR_ERROR_FUNCTION_UNSUPPORTED;
        return;
    }
    XrResult r = xrRequestBoundaryVisibilityMETA_(
        session_, suppress ? XR_BOUNDARY_VISIBILITY_SUPPRESSED_META : XR_BOUNDARY_VISIBILITY_NOT_SUPPRESSED_META);
    st.boundary_request_result = r;
    MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrRequestBoundaryVisibilityMETA(%s) -> %s",
                     suppress ? "SUPPRESSED" : "NOT_SUPPRESSED", xr_result_str(r));
}

void Mr::create_anchor(const Pose &pose_in_base) {
    if (!session_) return;
    if (!xrCreateSpatialAnchorFB_) {
        st.anchors.status = "xrCreateSpatialAnchorFB not available";
        st.anchors.status_level = 2;
        MERR("anchor: %s", st.anchors.status.c_str());
        return;
    }
    XrSpatialAnchorCreateInfoFB info{XR_TYPE_SPATIAL_ANCHOR_CREATE_INFO_FB};
    info.space = base_;
    info.poseInSpace = pose_in_base.xr();
    info.time = time_;
    XrAsyncRequestIdFB id = 0;
    XrResult r = xrCreateSpatialAnchorFB_(session_, &info, &id);
    MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrCreateSpatialAnchorFB(at %.2f %.2f %.2f) -> %s request %llu",
                     pose_in_base.p.x, pose_in_base.p.y, pose_in_base.p.z, xr_result_str(r), (unsigned long long)id);
    if (XR_SUCCEEDED(r)) {
        track(id, Req::CreateAnchor, &st.anchors);
        st.anchors.status = "creating an anchor...";
        st.anchors.status_level = 0;
    } else {
        st.anchors.status = std::string("xrCreateSpatialAnchorFB -> ") + xr_result_str(r);
        st.anchors.status_level = 2;
    }
}

void Mr::save_anchors(bool meta) {
    if (!session_ || st.anchors.entities.empty()) {
        st.anchors.status = "no anchors to save: squeeze a grip to place one";
        st.anchors.status_level = 2;
        return;
    }
    if (meta) {
        if (!xrSaveSpacesMETA_) {
            st.anchors.status = "xrSaveSpacesMETA not available";
            st.anchors.status_level = 2;
            MERR("%s", st.anchors.status.c_str());
            return;
        }
        std::vector<XrSpace> spaces;
        std::vector<XrUuidEXT> uuids;
        for (const Entity &e : st.anchors.entities)
            if (e.space) spaces.push_back(e.space), uuids.push_back(e.uuid);
        XrSpacesSaveInfoMETA info{XR_TYPE_SPACES_SAVE_INFO_META};
        info.spaceCount = (uint32_t)spaces.size();
        info.spaces = spaces.data();
        XrAsyncRequestIdFB id = 0;
        XrResult r = xrSaveSpacesMETA_(session_, &info, &id);
        MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrSaveSpacesMETA(%zu anchors) -> %s request %llu", spaces.size(),
                         xr_result_str(r), (unsigned long long)id);
        if (XR_SUCCEEDED(r)) track(id, Req::SaveMeta, &st.anchors, XR_NULL_HANDLE, uuids);
        st.anchors.status = std::string("xrSaveSpacesMETA -> ") + xr_result_str(r);
        st.anchors.status_level = XR_SUCCEEDED(r) ? 0 : 2;
        return;
    }
    if (!xrSaveSpaceFB_) {
        st.anchors.status = "xrSaveSpaceFB not available";
        st.anchors.status_level = 2;
        MERR("%s", st.anchors.status.c_str());
        return;
    }
    int ok = 0;
    for (Entity &e : st.anchors.entities) {
        XrSpaceSaveInfoFB info{XR_TYPE_SPACE_SAVE_INFO_FB};
        info.space = e.space;
        info.location = XR_SPACE_STORAGE_LOCATION_LOCAL_FB;
        info.persistenceMode = XR_SPACE_PERSISTENCE_MODE_INDEFINITE_FB;
        XrAsyncRequestIdFB id = 0;
        XrResult r = xrSaveSpaceFB_(session_, &info, &id);
        MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrSaveSpaceFB(%s, LOCAL) -> %s request %llu",
                         short_uuid(e.uuid).c_str(), xr_result_str(r), (unsigned long long)id);
        if (XR_SUCCEEDED(r)) track(id, Req::SaveFb, &st.anchors, e.space, {e.uuid}), ++ok;
        else e.anchor_state = std::string("xrSaveSpaceFB -> ") + xr_result_str(r);
    }
    st.anchors.status = "xrSaveSpaceFB sent for " + std::to_string(ok) + " of " +
                        std::to_string(st.anchors.entities.size()) + " anchors";
    st.anchors.status_level = ok ? 0 : 2;
}

void Mr::erase_anchors(bool meta) {
    if (!session_) return;
    if (meta) {
        if (!xrEraseSpacesMETA_) {
            st.anchors.status = "xrEraseSpacesMETA not available";
            st.anchors.status_level = 2;
            MERR("%s", st.anchors.status.c_str());
            return;
        }
        // spaces in hand, plus the UUIDs of saved ones that aren't loaded
        std::vector<XrSpace> spaces;
        std::vector<XrUuidEXT> uuids, all;
        for (const Entity &e : st.anchors.entities)
            if (e.space) spaces.push_back(e.space), all.push_back(e.uuid);
        for (const std::string &hex : saved_uuids_) {
            XrUuidEXT u;
            if (parse_uuid(hex, u) && st.anchors.find(u) < 0) uuids.push_back(u), all.push_back(u);
        }
        if (all.empty()) {
            st.anchors.status = "nothing to erase";
            return;
        }
        XrSpacesEraseInfoMETA info{XR_TYPE_SPACES_ERASE_INFO_META};
        info.spaceCount = (uint32_t)spaces.size();
        info.spaces = spaces.empty() ? nullptr : spaces.data();
        info.uuidCount = (uint32_t)uuids.size();
        info.uuids = uuids.empty() ? nullptr : uuids.data();
        XrAsyncRequestIdFB id = 0;
        XrResult r = xrEraseSpacesMETA_(session_, &info, &id);
        MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrEraseSpacesMETA(%zu spaces, %zu UUIDs) -> %s request %llu",
                         spaces.size(), uuids.size(), xr_result_str(r), (unsigned long long)id);
        if (XR_SUCCEEDED(r)) track(id, Req::EraseMeta, &st.anchors, XR_NULL_HANDLE, all);
        st.anchors.status = std::string("xrEraseSpacesMETA -> ") + xr_result_str(r);
        st.anchors.status_level = XR_SUCCEEDED(r) ? 0 : 2;
        return;
    }
    if (!xrEraseSpaceFB_) {
        st.anchors.status = "xrEraseSpaceFB not available";
        st.anchors.status_level = 2;
        MERR("%s", st.anchors.status.c_str());
        return;
    }
    int ok = 0;
    for (Entity &e : st.anchors.entities) {
        XrSpaceEraseInfoFB info{XR_TYPE_SPACE_ERASE_INFO_FB};
        info.space = e.space;
        info.location = XR_SPACE_STORAGE_LOCATION_LOCAL_FB;
        XrAsyncRequestIdFB id = 0;
        XrResult r = xrEraseSpaceFB_(session_, &info, &id);
        MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrEraseSpaceFB(%s, LOCAL) -> %s request %llu",
                         short_uuid(e.uuid).c_str(), xr_result_str(r), (unsigned long long)id);
        if (XR_SUCCEEDED(r)) track(id, Req::EraseFb, &st.anchors, e.space, {e.uuid}), ++ok;
        else e.anchor_state = std::string("xrEraseSpaceFB -> ") + xr_result_str(r);
    }
    st.anchors.status = "xrEraseSpaceFB sent for " + std::to_string(ok) + " anchors (loaded ones only)";
    st.anchors.status_level = ok ? 0 : 2;
}

void Mr::load_anchors(bool meta) {
    if (!session_) return;
    std::vector<XrUuidEXT> uuids;
    for (const std::string &hex : saved_uuids_) {
        XrUuidEXT u;
        if (parse_uuid(hex, u)) uuids.push_back(u);
    }
    if (uuids.empty()) {
        st.anchors.status = "no saved anchors (this app's anchors.txt is empty)";
        st.anchors.status_level = 2;
        return;
    }
    clear_set(st.anchors);
    XrAsyncRequestIdFB id = 0;
    XrResult r;
    if (meta) {
        if (!xrDiscoverSpacesMETA_) {
            st.anchors.status = "xrDiscoverSpacesMETA not available";
            st.anchors.status_level = 2;
            return;
        }
        XrSpaceFilterUuidMETA filter{XR_TYPE_SPACE_FILTER_UUID_META};
        filter.uuidCount = (uint32_t)uuids.size();
        filter.uuids = uuids.data();
        const XrSpaceFilterBaseHeaderMETA *filters[] = {(const XrSpaceFilterBaseHeaderMETA *)&filter};
        XrSpaceDiscoveryInfoMETA info{XR_TYPE_SPACE_DISCOVERY_INFO_META};
        info.filterCount = 1;
        info.filters = filters;
        r = xrDiscoverSpacesMETA_(session_, &info, &id);
        MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrDiscoverSpacesMETA(%zu saved UUIDs) -> %s request %llu",
                         uuids.size(), xr_result_str(r), (unsigned long long)id);
        if (XR_SUCCEEDED(r)) track(id, Req::MetaAnchors, &st.anchors);
    } else {
        if (!xrQuerySpacesFB_) {
            st.anchors.status = "xrQuerySpacesFB not available";
            st.anchors.status_level = 2;
            return;
        }
        XrSpaceStorageLocationFilterInfoFB local{XR_TYPE_SPACE_STORAGE_LOCATION_FILTER_INFO_FB};
        local.location = XR_SPACE_STORAGE_LOCATION_LOCAL_FB;
        XrSpaceUuidFilterInfoFB filter{XR_TYPE_SPACE_UUID_FILTER_INFO_FB};
        filter.next = &local;
        filter.uuidCount = (uint32_t)uuids.size();
        filter.uuids = uuids.data();
        XrSpaceQueryInfoFB info{XR_TYPE_SPACE_QUERY_INFO_FB};
        info.queryAction = XR_SPACE_QUERY_ACTION_LOAD_FB;
        info.maxResultCount = (uint32_t)uuids.size();
        info.filter = (const XrSpaceFilterInfoBaseHeaderFB *)&filter;
        r = xrQuerySpacesFB_(session_, (const XrSpaceQueryInfoBaseHeaderFB *)&info, &id);
        MrLog::get().add(XR_FAILED(r) ? 2 : 0, "xrQuerySpacesFB(LOAD, %zu saved UUIDs) -> %s request %llu",
                         uuids.size(), xr_result_str(r), (unsigned long long)id);
        if (XR_SUCCEEDED(r)) track(id, Req::FbAnchors, &st.anchors);
    }
    st.anchors.status = XR_SUCCEEDED(r) ? "loading saved anchors..." : std::string("load: ") + xr_result_str(r);
    st.anchors.status_level = XR_SUCCEEDED(r) ? 0 : 2;
}

void Mr::clear_anchors() {
    size_t n = st.anchors.entities.size();
    clear_set(st.anchors);
    MLOG("anchors: %zu dropped from view (xrDestroySpace; saved ones stay saved)", n);
    st.anchors.status = "cleared (saved anchors stay saved: Load brings them back)";
    st.anchors.status_level = 0;
}

// ---------------------------------------------------------------- files
void Mr::load_saved_list() {
    saved_uuids_.clear();
    std::ifstream f(files_dir + "/anchors.txt");
    std::string line;
    XrUuidEXT u;
    while (std::getline(f, line))
        if (parse_uuid(line, u)) saved_uuids_.push_back(line);
    if (!saved_uuids_.empty()) MLOG("anchors.txt: %zu saved anchors remembered", saved_uuids_.size());
}

void Mr::store_saved_list() {
    std::ofstream f(files_dir + "/anchors.txt");
    for (const std::string &s : saved_uuids_) f << s << "\n";
}

void Mr::dump_set(const RoomSet &set, const char *file) const {
    if (files_dir.empty()) return;
    std::ofstream f(files_dir + "/" + file);
    f << "# " << set.name << "\n# " << set.status << "\n# poses in the app's LOCAL space (metres)\n";
    char buf[512];
    for (const Entity &e : set.entities) {
        std::snprintf(buf, sizeof(buf), "space %s role=%s labels=%s located=%d pos=%.3f,%.3f,%.3f rot=%.3f,%.3f,%.3f,%.3f\n",
                      e.has_uuid ? uuid_hex(e.uuid).c_str() : "-", e.role.empty() ? "-" : e.role.c_str(),
                      e.labels.empty() ? "-" : e.labels.c_str(), e.located ? 1 : 0, e.pose.p.x, e.pose.p.y, e.pose.p.z,
                      e.pose.q.x, e.pose.q.y, e.pose.q.z, e.pose.q.w);
        f << buf;
        f << "  components:";
        for (XrSpaceComponentTypeFB t : e.components) f << " " << component_name(t) << (has(e.enabled, t) ? "" : "(off)");
        f << "\n";
        if (e.has_b2d) {
            std::snprintf(buf, sizeof(buf), "  bounded2d offset %.3f,%.3f extent %.3f x %.3f, outline %zu points\n",
                          e.b2d.offset.x, e.b2d.offset.y, e.b2d.extent.width, e.b2d.extent.height, e.boundary.size());
            f << buf;
        }
        if (e.has_b3d) {
            std::snprintf(buf, sizeof(buf), "  bounded3d offset %.3f,%.3f,%.3f extent %.3f x %.3f x %.3f\n",
                          e.b3d.offset.x, e.b3d.offset.y, e.b3d.offset.z, e.b3d.extent.width, e.b3d.extent.height,
                          e.b3d.extent.depth);
            f << buf;
        }
        if (e.is_room) f << "  room layout: floor " << uuid_hex(e.floor) << " ceiling " << uuid_hex(e.ceiling) << ", "
                         << e.walls.size() << " walls, " << e.contained.size() << " contained\n";
        if (!e.mesh_indices.empty())
            f << "  mesh " << e.mesh_vertices.size() << " vertices, " << e.mesh_indices.size() / 3 << " triangles\n";
        for (const CallResult &c : e.calls) f << "  " << c.call << " -> " << xr_result_str(c.result) << "\n";
    }
}
