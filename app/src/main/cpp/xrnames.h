// Names for OpenXR values (results, components, reference spaces) without needing an instance, and UUID helpers.
#pragma once

#include <openxr/openxr.h>

#include <string>

// "Not called yet": kept apart from every real XrResult.
constexpr XrResult kNotCalled = (XrResult)0x7FFFFFF0;

const char *xr_result_str(XrResult r);  // "XR_SUCCESS", "XR_ERROR_...", "-" for kNotCalled
const char *component_name(XrSpaceComponentTypeFB t);  // "LOCATABLE", "BOUNDED_2D", ...
const char *ref_space_name(XrReferenceSpaceType t);
const char *session_state_name(XrSessionState s);
std::string uuid_hex(const XrUuidEXT &u);  // 32 lowercase hex digits
bool parse_uuid(const std::string &hex, XrUuidEXT &out);
bool uuid_eq(const XrUuidEXT &a, const XrUuidEXT &b);
std::string location_flags_str(XrSpaceLocationFlags f);  // "OV PV OT PT"-style short form
