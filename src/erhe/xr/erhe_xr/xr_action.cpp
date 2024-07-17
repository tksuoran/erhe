#include "erhe_xr/xr_action.hpp"
#include "erhe_xr/xr_log.hpp"
#include "erhe_xr/xr.hpp"
#include "erhe_verify/verify.hpp"

#ifdef _WIN32
#   include <unknwn.h>
#   define XR_USE_PLATFORM_WIN32      1
#endif

#ifdef linux
#   define XR_USE_PLATFORM_LINUX      1
#endif

//#define XR_USE_GRAPHICS_API_VULKAN 1
#define XR_USE_GRAPHICS_API_OPENGL 1
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <fmt/format.h>

namespace erhe::xr {

Xr_action::Xr_action(const XrInstance instance, const std::string_view name, const unsigned int profile_mask)
    : name        {name}
    , profile_mask{profile_mask}
{
    const XrResult result = xrStringToPath(instance, name.data(), &path);
    log_xr->info("xrStringToPath({}) = {:x}", name, path);
    if (result != XR_SUCCESS) {
        log_xr->warn("xrStringToPath({}) returned error {}", name, c_str(result));
    }
}

Xr_action_boolean::Xr_action_boolean(
    const XrInstance       instance,
    const XrActionSet      action_set,
    const std::string_view name,
    const unsigned int     profile_mask
)
    : Xr_action{instance, name, profile_mask}
{
    if (name.length() >= XR_MAX_ACTION_NAME_SIZE) {
        ERHE_FATAL("name too long");
        return;
    }

    XrActionCreateInfo create_info;
    create_info.type = XR_TYPE_ACTION_CREATE_INFO;
    create_info.next = nullptr;
    strncpy(create_info.actionName, name.data(), name.size());
    for (auto& s : create_info.actionName) {
        s = (s == '/') ? '_' : s;
    }
    create_info.actionName[name.size()] = '\0';
    strncpy(
        create_info.localizedActionName,
        create_info.actionName,
        XR_MAX_LOCALIZED_ACTION_NAME_SIZE
    );
    create_info.localizedActionName[XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1] = '\0';
    create_info.actionType             = XR_ACTION_TYPE_BOOLEAN_INPUT;
    create_info.countSubactionPaths    = 0;
    create_info.subactionPaths         = nullptr;

    const XrResult result = xrCreateAction(action_set, &create_info, &action);
    log_xr->info(
        "xrCreateAction(.actionSet = {} .actionType = XR_ACTION_TYPE_BOOLEAN_INPUT, .actionName = '{}', .localizedActionName = '{}') result = {}, action = {}",
        fmt::ptr(action_set),
        &create_info.actionName[0],
        &create_info.localizedActionName[0],
        c_str(result),
        fmt::ptr(action)
    );

    if (result != XR_SUCCESS) {
        log_xr->error("xrCreateAction() returned error {} for {}", c_str(result), name);
    }
}

void Xr_action_boolean::get(const XrSession session)
{
    if (action == nullptr) {
        return;
    }
    const XrActionStateGetInfo action_state_get_info{
        .type          = XR_TYPE_ACTION_STATE_GET_INFO,
        .next          = nullptr,
        .action        = action,
        .subactionPath = XR_NULL_PATH
    };
    const XrResult result = xrGetActionStateBoolean(session, &action_state_get_info, &state);
    if (result != XR_SUCCESS) {
        log_xr->error(
            "xrGetActionStateBoolean() returned error {}",
            c_str(result)
        );
    }
}

//

Xr_action_float::Xr_action_float(
    const XrInstance       instance,
    const XrActionSet      action_set,
    const std::string_view name,
    const unsigned int     profile_mask
)
    : Xr_action{instance, name, profile_mask}
{
    if (name.length() >= XR_MAX_ACTION_NAME_SIZE) {
        ERHE_FATAL("name too long");
        return;
    }

    XrActionCreateInfo create_info;
    create_info.type = XR_TYPE_ACTION_CREATE_INFO;
    create_info.next = nullptr;
    strncpy(create_info.actionName, name.data(), name.size());
    for (auto& s : create_info.actionName) {
        s = (s == '/') ? '_' : s;
    }
    create_info.actionName[name.size()] = '\0';
    strncpy(
        create_info.localizedActionName,
        create_info.actionName,
        XR_MAX_LOCALIZED_ACTION_NAME_SIZE
    );
    create_info.localizedActionName[XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1] = '\0';
    create_info.actionType             = XR_ACTION_TYPE_FLOAT_INPUT;
    create_info.countSubactionPaths    = 0;
    create_info.subactionPaths         = nullptr;

    const XrResult result = xrCreateAction(action_set, &create_info, &action);
    log_xr->info(
        "xrCreateAction(.actionSet = {}, .actionType = XR_ACTION_TYPE_FLOAT_INPUT, .actionName = '{}', .localizedActionName = '{}') result = {}, action = {}",
        fmt::ptr(action_set),
        &create_info.actionName[0],
        &create_info.localizedActionName[0],
        c_str(result),
        fmt::ptr(action)
    );

    if (result != XR_SUCCESS) {
        log_xr->error("xrCreateAction() returned error {} for {}", c_str(result), name);
    }
}

void Xr_action_float::get(const XrSession session)
{
    if (action == nullptr) {
        return;
    }
    const XrActionStateGetInfo action_state_get_info{
        .type          = XR_TYPE_ACTION_STATE_GET_INFO,
        .next          = nullptr,
        .action        = action,
        .subactionPath = XR_NULL_PATH
    };
    const XrResult result = xrGetActionStateFloat(session, &action_state_get_info, &state);
    if (result != XR_SUCCESS) {
        log_xr->error("xrGetActionStateFloat() returned error {}", c_str(result));
    }
}

//

Xr_action_vector2f::Xr_action_vector2f(
    const XrInstance       instance,
    const XrActionSet      action_set,
    const std::string_view name,
    const unsigned int     profile_mask
)
    : Xr_action{instance, name, profile_mask}
{
    if (name.length() >= XR_MAX_ACTION_NAME_SIZE) {
        ERHE_FATAL("name too long");
        return;
    }

    XrActionCreateInfo create_info;
    create_info.type = XR_TYPE_ACTION_CREATE_INFO;
    create_info.next = nullptr;
    strncpy(create_info.actionName, name.data(), name.size());
    for (auto& s : create_info.actionName) {
        s = (s == '/') ? '_' : s;
    }
    create_info.actionName[name.size()] = '\0';
    strncpy(create_info.localizedActionName, create_info.actionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
    create_info.localizedActionName[XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1] = '\0';
    create_info.actionType             = XR_ACTION_TYPE_VECTOR2F_INPUT;
    create_info.countSubactionPaths    = 0;
    create_info.subactionPaths         = nullptr;

    const XrResult result = xrCreateAction(action_set, &create_info, &action);
    log_xr->info(
        "xrCreateAction(.actionSet = {}, .actionType = XR_ACTION_TYPE_VECTOR2F_INPUT, .actionName = '{}', .localizedActionName = '{}') result = {}, action = {}",
        fmt::ptr(action_set),
        &create_info.actionName[0],
        &create_info.localizedActionName[0],
        c_str(result),
        fmt::ptr(action)
    );

    if (result != XR_SUCCESS) {
        log_xr->error("xrCreateAction() returned error {} for {}", c_str(result), name);
    }
}

void Xr_action_vector2f::get(const XrSession session)
{
    if (action == nullptr) {
        return;
    }
    const XrActionStateGetInfo action_state_get_info{
        .type          = XR_TYPE_ACTION_STATE_GET_INFO,
        .next          = nullptr,
        .action        = action,
        .subactionPath = XR_NULL_PATH
    };
    const XrResult get_result = xrGetActionStateVector2f(session, &action_state_get_info, &state);
    if (get_result != XR_SUCCESS) {
        log_xr->error("xrGetActionStateVector2f() returned error {}", c_str(get_result));
    }
}

//

Xr_action_pose::Xr_action_pose(
    const XrInstance       instance,
    const XrActionSet      action_set,
    const std::string_view name,
    const unsigned int     profile_mask
)
    : Xr_action{instance, name, profile_mask}
{
    if (name.length() >= XR_MAX_ACTION_NAME_SIZE) {
        ERHE_FATAL("name too long");
        return;
    }

    XrActionCreateInfo create_info;
    create_info.type = XR_TYPE_ACTION_CREATE_INFO;
    create_info.next = nullptr;
    strncpy(create_info.actionName, name.data(), name.size());
    for (auto& s : create_info.actionName) {
        s = (s == '/') ? '_' : s;
    }
    create_info.actionName[name.size()] = '\0';
    strncpy(
        create_info.localizedActionName,
        create_info.actionName,
        XR_MAX_LOCALIZED_ACTION_NAME_SIZE
    );
    create_info.localizedActionName[XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1] = '\0';
    create_info.actionType             = XR_ACTION_TYPE_POSE_INPUT;
    create_info.countSubactionPaths    = 0;
    create_info.subactionPaths         = nullptr;

    const XrResult result = xrCreateAction(action_set, &create_info, &action);
    log_xr->info(
        "xrCreateAction(.actionSet = {}, .actionType = XR_ACTION_TYPE_POSE_INPUT, .actionName = '{}', .localizedActionName = '{}') result = {}, action = {}",
        fmt::ptr(action_set),
        &create_info.actionName[0],
        &create_info.localizedActionName[0],
        c_str(result),
        fmt::ptr(action)
    );

    if (result != XR_SUCCESS) {
        log_xr->error("xrCreateAction() returned error {} for {}", c_str(result), name);
    }
}

void Xr_action_pose::attach(const XrSession session)
{
    if (action == nullptr) {
        return;
    }
    const XrActionSpaceCreateInfo create_info
    {
        .type              = XR_TYPE_ACTION_SPACE_CREATE_INFO,
        .next              = nullptr,
        .action            = action,
        .subactionPath     = XR_NULL_PATH,
        .poseInActionSpace = {
            .orientation = {
                .x = 0.0f,
                .y = 0.0f,
                .z = 0.0f,
                .w = 1.0f
            },
            .position = {
                .x = 0.0f,
                .y = 0.0f,
                .z = 0.0f,
            }
        }
    };
    const XrResult result = xrCreateActionSpace(session, &create_info, &space);
    if (result != XR_SUCCESS) {
        log_xr->error("xrCreateActionSpace() returned error {}", c_str(result));
    }
}

void Xr_action_pose::get(const XrSession session, const XrTime time, const XrSpace base_space)
{
    if (action == nullptr) {
        return;
    }
    const XrActionStateGetInfo action_state_get_info{
        .type          = XR_TYPE_ACTION_STATE_GET_INFO,
        .next          = nullptr,
        .action        = action,
        .subactionPath = XR_NULL_PATH
    };
    const XrResult get_result = xrGetActionStatePose(session, &action_state_get_info, &state);
    if (get_result != XR_SUCCESS) {
        log_xr->error("xrGetActionStatePose() returned error {}", c_str(get_result));
    }

    const XrResult locate_result = xrLocateSpace(space, base_space, time, &location);
    if (locate_result != XR_SUCCESS) {
        log_xr->error("xrLocateSpace() returned error {}", c_str(locate_result));
    }

    if ((location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == XR_SPACE_LOCATION_POSITION_VALID_BIT) {
        position = to_glm(location.pose.position);
    }
    if ((location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) {
        orientation = to_glm(location.pose.orientation);
    }
}

} // namespace erhe::xr
