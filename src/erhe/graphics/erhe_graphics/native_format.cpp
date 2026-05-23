#include "erhe_graphics/native_format.hpp"

#if defined(ERHE_GRAPHICS_API_VULKAN)
# include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_gl/enum_string_functions.hpp"
# include "erhe_gl/gl_helpers.hpp"
# include "erhe_gl/wrapper_enums.hpp"
#endif

namespace erhe::graphics {

auto native_swapchain_format_to_dataformat(int64_t native_format) -> erhe::dataformat::Format
{
#if defined(ERHE_GRAPHICS_API_VULKAN)
    const VkFormat vk_format = static_cast<VkFormat>(native_format);
    return to_erhe(vk_format);
#elif defined(ERHE_GRAPHICS_API_OPENGL)
    const gl::Internal_format gl_internal_format = static_cast<gl::Internal_format>(native_format);
    return gl_helpers::convert_from_gl(gl_internal_format);
#else
    static_cast<void>(native_format);
    return erhe::dataformat::Format::format_undefined;
#endif
}

auto dataformat_to_native_swapchain_format(erhe::dataformat::Format format) -> int64_t
{
#if defined(ERHE_GRAPHICS_API_VULKAN)
    const VkFormat vk_format = to_vulkan(format);
    return static_cast<int64_t>(vk_format);
#elif defined(ERHE_GRAPHICS_API_OPENGL)
    const std::optional<gl::Internal_format> gl_internal_format = gl_helpers::convert_to_gl(format);
    return gl_internal_format.has_value() ? static_cast<int64_t>(gl_internal_format.value()) : int64_t{0};
#else
    static_cast<void>(format);
    return int64_t{0};
#endif
}

auto native_swapchain_format_c_str(int64_t native_format) -> std::string
{
#if defined(ERHE_GRAPHICS_API_VULKAN)
    const VkFormat vk_format = static_cast<VkFormat>(native_format);
    return std::string{c_str(vk_format)};
#elif defined(ERHE_GRAPHICS_API_OPENGL)
    const gl::Internal_format gl_internal_format = static_cast<gl::Internal_format>(native_format);
    return std::string{gl::c_str(gl_internal_format)};
#else
    static_cast<void>(native_format);
    return std::string{};
#endif
}

} // namespace erhe::graphics
