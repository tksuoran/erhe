#pragma once

// The per-backend coordinate conventions (depth range, framebuffer and
// texture origin, clip-space Y flip). Only enums and a class of enums, so
// a header that carries a Coordinate_conventions value (Device_info in
// erhe_graphics/device.hpp) does not pull in math_util.hpp and glm.

namespace erhe::math {

enum class Depth_range : unsigned int {
    zero_to_one,        // GL 4.5 with glClipControl(lower_left, zero_to_one), Metal, or Vulkan
    negative_one_to_one // Default OpenGL NDC (when glClipControl is not applied)
};

enum class Framebuffer_origin : unsigned int {
    bottom_left, // OpenGL
    top_left     // Metal, Vulkan
};

enum class Texture_origin : unsigned int {
    bottom_left, // OpenGL
    top_left     // Metal, Vulkan
};

// Whether the projection matrix must negate clip space Y to render correctly.
// disabled: The API's viewport transform already produces correct screen Y
//           (OpenGL: bottom-left origin matches Y-up NDC;
//            Metal: viewport uses (1-y_ndc) which flips Y internally).
// enabled:  Positive clip Y maps to the bottom of the screen; the projection
//           must negate Y to compensate (Vulkan without VK_KHR_maintenance1).
enum class Clip_space_y_flip : unsigned int {
    disabled,
    enabled
};

class Coordinate_conventions
{
public:
    Depth_range        native_depth_range{Depth_range::negative_one_to_one};
    Framebuffer_origin framebuffer_origin{Framebuffer_origin::bottom_left};
    Texture_origin     texture_origin    {Texture_origin::bottom_left};
    Clip_space_y_flip  clip_space_y_flip {Clip_space_y_flip::disabled};
};

} // namespace erhe::math
