#pragma once

#if defined(ERHE_XR_LIBRARY_OPENXR)
#include <openxr/openxr.h>
#else
#define XR_DEFINE_HANDLE(object) typedef struct object##_T* object;
#define XR_DEFINE_ATOM(object) typedef uint64_t object;
typedef int64_t XrTime;
typedef uint32_t XrBool32;
typedef uint64_t XrFlags64;
typedef XrFlags64 XrSpaceLocationFlags;
XR_DEFINE_ATOM(XrPath);
XR_DEFINE_HANDLE(XrInstance)
XR_DEFINE_HANDLE(XrSession)
XR_DEFINE_HANDLE(XrSpace)
XR_DEFINE_HANDLE(XrAction)
XR_DEFINE_HANDLE(XrSwapchain)
XR_DEFINE_HANDLE(XrActionSet)
#define XR_NULL_PATH 0
#define XR_NULL_HANDLE nullptr
#define XR_TRUE                           1
#define XR_FALSE                          0
#define XR_MAY_ALIAS

typedef enum XrStructureType {
    XR_TYPE_ACTION_STATE_BOOLEAN = 23,
    XR_TYPE_ACTION_STATE_FLOAT = 24,
    XR_TYPE_ACTION_STATE_VECTOR2F = 25,
    XR_TYPE_ACTION_STATE_POSE = 27,
    XR_TYPE_SPACE_LOCATION = 42,
    XR_STRUCTURE_TYPE_MAX_ENUM = 0x7FFFFFFF
} XrStructureType;
typedef struct XrQuaternionf {
    float    x;
    float    y;
    float    z;
    float    w;
} XrQuaternionf;
typedef struct XrVector3f {
    float    x;
    float    y;
    float    z;
} XrVector3f;
typedef struct XrPosef {
    XrQuaternionf    orientation;
    XrVector3f       position;
} XrPosef;
typedef struct XrActionStateBoolean {
    XrStructureType       type;
    void* XR_MAY_ALIAS    next;
    XrBool32              currentState;
    XrBool32              changedSinceLastSync;
    XrTime                lastChangeTime;
    XrBool32              isActive;
} XrActionStateBoolean;

typedef struct XrActionStateFloat {
    XrStructureType       type;
    void* XR_MAY_ALIAS    next;
    float                 currentState;
    XrBool32              changedSinceLastSync;
    XrTime                lastChangeTime;
    XrBool32              isActive;
} XrActionStateFloat;
typedef struct XrVector2f {
    float    x;
    float    y;
} XrVector2f;
typedef struct XrActionStateVector2f {
    XrStructureType       type;
    void* XR_MAY_ALIAS    next;
    XrVector2f            currentState;
    XrBool32              changedSinceLastSync;
    XrTime                lastChangeTime;
    XrBool32              isActive;
} XrActionStateVector2f;
typedef struct XrActionStatePose {
    XrStructureType       type;
    void* XR_MAY_ALIAS    next;
    XrBool32              isActive;
} XrActionStatePose;
typedef struct XrSpaceLocation {
    XrStructureType         type;
    void* XR_MAY_ALIAS      next;
    XrSpaceLocationFlags    locationFlags;
    XrPosef                 pose;
} XrSpaceLocation;
#endif

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <string_view>

namespace erhe::xr {

class Profile_mask
{
public:
    static constexpr unsigned int khr_simple   = (1u << 0);
    static constexpr unsigned int oculus_touch = (1u << 1);
    static constexpr unsigned int valve_index  = (1u << 2);
    static constexpr unsigned int htc_vive     = (1u << 3);
};

class Xr_action
{
public:
    Xr_action(XrInstance instance, const std::string_view name, unsigned int profile_mask);

    std::string  name;
    XrPath       path        {XR_NULL_PATH};
    XrAction     action      {XR_NULL_HANDLE};
    unsigned int profile_mask{0};
};

class Xr_action_boolean : public Xr_action
{
public:
    Xr_action_boolean(XrInstance instance, const XrActionSet action_set, const std::string_view name, unsigned int profile_mask);

    void get(const XrSession session);

    XrActionStateBoolean state{
        .type                 = XR_TYPE_ACTION_STATE_BOOLEAN,
        .next                 = nullptr,
        .currentState         = XR_FALSE,
        .changedSinceLastSync = XR_FALSE,
        .lastChangeTime       = 0,
        .isActive             = XR_FALSE
    };
};

class Xr_action_float : public Xr_action
{
public:
    Xr_action_float(XrInstance instance, const XrActionSet action_set, const std::string_view name, unsigned int profile_mask);

    void get(const XrSession session);

    XrActionStateFloat state{
        .type                 = XR_TYPE_ACTION_STATE_FLOAT,
        .next                 = nullptr,
        .currentState         = 0.0f,
        .changedSinceLastSync = XR_FALSE,
        .lastChangeTime       = 0,
        .isActive             = XR_FALSE
    };
};

class Xr_action_vector2f : public Xr_action
{
public:
    Xr_action_vector2f(XrInstance instance, const XrActionSet action_set, const std::string_view name, unsigned int profile_mask);

    void get(const XrSession session);

    XrActionStateVector2f state
    {
        .type         = XR_TYPE_ACTION_STATE_VECTOR2F,
        .next         = nullptr,
        .currentState = {
            .x = 0.0f,
            .y = 0.0f
        },
        .changedSinceLastSync = XR_FALSE,
        .lastChangeTime       = 0,
        .isActive             = XR_FALSE
    };
};

class Xr_action_pose : public Xr_action
{
public:
    Xr_action_pose(XrInstance instance, const XrActionSet action_set, const std::string_view name, unsigned int profile_mask);

    void attach(const XrSession session);
    void get   (const XrSession session, const XrTime predicted_display_time, const XrSpace base_space);

    XrActionStatePose state
    {
        .type     = XR_TYPE_ACTION_STATE_POSE,
        .next     = nullptr,
        .isActive = XR_FALSE
    };
    XrSpace           space{XR_NULL_HANDLE};
    XrSpaceLocation   location{
        .type          = XR_TYPE_SPACE_LOCATION,
        .next          = nullptr,
        .locationFlags = 0,
        .pose = {
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
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 position   {0.0f, 0.0f, 0.0f};
};

class Xr_actions
{
public:
    // Boolean                                       left  | right 
    Xr_action_boolean*  select_click    {nullptr}; // K___  | K___  
    Xr_action_boolean*  system_click    {nullptr}; // _V_I  | _VOI  
    Xr_action_boolean*  menu_click      {nullptr}; // KVO_  | KV__  
    Xr_action_boolean*  squeeze_click   {nullptr}; // _V__  | _V__  
    Xr_action_boolean*  x_click         {nullptr}; // __O_  | ____  
    Xr_action_boolean*  x_touch         {nullptr}; // __O_  | ____  
    Xr_action_boolean*  y_click         {nullptr}; // __O_  | ____  
    Xr_action_boolean*  y_touch         {nullptr}; // __O_  | ____  
    Xr_action_boolean*  a_click         {nullptr}; // ____  | __O_  
    Xr_action_boolean*  a_touch         {nullptr}; // 
    Xr_action_boolean*  b_click         {nullptr}; // 
    Xr_action_boolean*  b_touch         {nullptr}; // 
    Xr_action_boolean*  trigger_click   {nullptr}; // 
    Xr_action_boolean*  trigger_touch   {nullptr}; // 
    Xr_action_boolean*  trackpad_click  {nullptr}; // 
    Xr_action_boolean*  trackpad_touch  {nullptr}; // 
    Xr_action_boolean*  thumbstick_click{nullptr}; // 
    Xr_action_boolean*  thumbstick_touch{nullptr}; // 
    Xr_action_boolean*  thumbrest_touch {nullptr}; // 

    // Float
    Xr_action_float*    squeeze_value   {nullptr};
    Xr_action_float*    squeeze_force   {nullptr};
    Xr_action_float*    trigger_value   {nullptr};
    Xr_action_float*    trackpad_force  {nullptr};

    // Vector2f
    Xr_action_vector2f* trackpad        {nullptr};
    Xr_action_vector2f* thumbstick      {nullptr};

    // Pose
    Xr_action_pose*     grip_pose       {nullptr};
    Xr_action_pose*     aim_pose        {nullptr};
};

} // namespace erhe::ui
