#include "erhe/xr/xr.hpp"
#include "erhe/xr/xr_log.hpp"

#include <openxr/openxr_reflection.h>

#include <sstream>
#include <thread>

#define GEN_C_STR_CASE(name, val) case name: return #name;
#define GEN_C_STR(T)                            \
    const char* c_str(::T e) noexcept           \
    {                                           \
        switch (e) {                            \
            XR_LIST_ENUM_##T(GEN_C_STR_CASE)    \
            default: return "?";                \
        }                                       \
    }                                           \

namespace erhe::xr {

GEN_C_STR(XrActionType           )
GEN_C_STR(XrEnvironmentBlendMode )
GEN_C_STR(XrEyeVisibility        )
GEN_C_STR(XrFormFactor           )
GEN_C_STR(XrObjectType           )
GEN_C_STR(XrResult               )
GEN_C_STR(XrReferenceSpaceType   )
GEN_C_STR(XrSessionState         )
GEN_C_STR(XrStructureType        )
GEN_C_STR(XrViewConfigurationType)
GEN_C_STR(XrHandJointEXT         )

auto to_string_message_severity(XrDebugUtilsMessageSeverityFlagsEXT severity) -> std::string
{
    std::stringstream ss;
    bool first{true};

    if ((severity & XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) == XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ss << "error";
        first = false;
    }
    if ((severity & XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) == XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        if (!first) {
            ss << " | ";
        }
        ss << "warning";
        first = false;
    }
    if ((severity & XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) == XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        if (!first) {
            ss << " | ";
        }
        ss << "info";
        first = false;
    }
    if ((severity & XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) == XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        if (!first) {
            ss << " | ";
        }
        ss << "verbose";
        //first = false;
    }
    return ss.str();
}

auto to_string_message_type(XrDebugUtilsMessageTypeFlagsEXT type_flags) -> std::string
{
    std::stringstream ss;
    bool first{true};

    if ((type_flags & XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) == XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
        ss << "general";
        first = false;
    }
    if ((type_flags & XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) == XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        if (!first) {
            ss << " | ";
        }
        ss << "validation";
        first = false;
    }
    if ((type_flags & XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) == XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        if (!first) {
            ss << " | ";
        }
        ss << "performance";
        first = false;
    }
    if ((type_flags & XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT) == XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT) {
        if (!first) {
            ss << " | ";
        }
        ss << "conformance";
        //first = false;
    }
    return ss.str();
}

auto check(XrResult result, const spdlog::level::level_enum log_level) -> bool
{
    if (result == XR_ERROR_RUNTIME_FAILURE) {
        log_xr->error("OpenXR runtime failure");
        return false;
    }
    if (result != XR_SUCCESS) {
        log_xr->log(
            log_level,
            "OpenXR returned error {}",
            c_str(result)
        );
        return false;
    }
    return true;
}

void check_gl_context_in_current_in_this_thread()
{
    static std::thread::id xr_gl_thread_id{std::this_thread::get_id()};

    if (xr_gl_thread_id != std::this_thread::get_id()) {
        log_xr->error("XR GL thread error");
    }
}

}

#undef GEN_C_STR_CASE
#undef GEN_C_STR
