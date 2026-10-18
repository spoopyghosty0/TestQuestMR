// OpenXR stand-ins for the host renderer: mr.cpp links against these but the renderer never runs a session; the
// room comes from demo_room.cpp.
#include <openxr/openxr.h>

extern "C" {
XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance, const char *, PFN_xrVoidFunction *f) {
    *f = nullptr;
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetSystemProperties(XrInstance, XrSystemId, XrSystemProperties *) {
    return XR_ERROR_RUNTIME_FAILURE;
}
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateReferenceSpaces(XrSession, uint32_t, uint32_t *n, XrReferenceSpaceType *) {
    *n = 0;
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetReferenceSpaceBoundsRect(XrSession, XrReferenceSpaceType, XrExtent2Df *) {
    return XR_SPACE_BOUNDS_UNAVAILABLE;
}
XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace, XrSpace, XrTime, XrSpaceLocation *) {
    return XR_ERROR_RUNTIME_FAILURE;
}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpace(XrSpace) { return XR_SUCCESS; }
}
