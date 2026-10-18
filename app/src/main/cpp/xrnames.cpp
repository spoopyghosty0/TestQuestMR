#include "xrnames.h"

#include <cstdio>
#include <cstring>

#include <openxr/openxr_reflection.h>

const char *xr_result_str(XrResult r) {
    if (r == kNotCalled) return "-";
    switch (r) {
#define TQMR_RESULT_CASE(name, value) \
    case name:                         \
        return #name;
        XR_LIST_ENUM_XrResult(TQMR_RESULT_CASE)
#undef TQMR_RESULT_CASE
        default:
            break;
    }
    static thread_local char buf[32];
    std::snprintf(buf, sizeof(buf), "XrResult %d", (int)r);
    return buf;
}

const char *component_name(XrSpaceComponentTypeFB t) {
    switch (t) {
        case XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB: return "LOCATABLE";
        case XR_SPACE_COMPONENT_TYPE_STORABLE_FB: return "STORABLE";
        case XR_SPACE_COMPONENT_TYPE_SHARABLE_FB: return "SHARABLE";
        case XR_SPACE_COMPONENT_TYPE_BOUNDED_2D_FB: return "BOUNDED_2D";
        case XR_SPACE_COMPONENT_TYPE_BOUNDED_3D_FB: return "BOUNDED_3D";
        case XR_SPACE_COMPONENT_TYPE_SEMANTIC_LABELS_FB: return "SEMANTIC_LABELS";
        case XR_SPACE_COMPONENT_TYPE_ROOM_LAYOUT_FB: return "ROOM_LAYOUT";
        case XR_SPACE_COMPONENT_TYPE_SPACE_CONTAINER_FB: return "SPACE_CONTAINER";
        case XR_SPACE_COMPONENT_TYPE_TRIANGLE_MESH_META: return "TRIANGLE_MESH";
        default: break;
    }
    static thread_local char buf[32];
    std::snprintf(buf, sizeof(buf), "component %d", (int)t);
    return buf;
}

const char *ref_space_name(XrReferenceSpaceType t) {
    switch (t) {
#define TQMR_SPACE_CASE(name, value) \
    case name:                        \
        return #name + 24;  // without "XR_REFERENCE_SPACE_TYPE_"
        XR_LIST_ENUM_XrReferenceSpaceType(TQMR_SPACE_CASE)
#undef TQMR_SPACE_CASE
        default:
            return "?";
    }
}

const char *session_state_name(XrSessionState s) {
    switch (s) {
#define TQMR_STATE_CASE(name, value) \
    case name:                        \
        return #name + 17;  // without "XR_SESSION_STATE_"
        XR_LIST_ENUM_XrSessionState(TQMR_STATE_CASE)
#undef TQMR_STATE_CASE
        default:
            return "?";
    }
}

std::string uuid_hex(const XrUuidEXT &u) {
    char s[33];
    for (int i = 0; i < XR_UUID_SIZE_EXT; ++i) std::snprintf(s + 2 * i, 3, "%02x", u.data[i]);
    return s;
}

bool parse_uuid(const std::string &hex, XrUuidEXT &out) {
    if (hex.size() != 32) return false;
    for (int i = 0; i < XR_UUID_SIZE_EXT; ++i) {
        unsigned v = 0;
        if (std::sscanf(hex.c_str() + 2 * i, "%2x", &v) != 1) return false;
        out.data[i] = (uint8_t)v;
    }
    return true;
}

bool uuid_eq(const XrUuidEXT &a, const XrUuidEXT &b) { return std::memcmp(a.data, b.data, XR_UUID_SIZE_EXT) == 0; }

std::string location_flags_str(XrSpaceLocationFlags f) {
    std::string s;
    s += (f & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) ? "rot" : "---";
    s += (f & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) ? "+T " : "   ";
    s += (f & XR_SPACE_LOCATION_POSITION_VALID_BIT) ? "pos" : "---";
    s += (f & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) ? "+T" : "  ";
    return s;
}
