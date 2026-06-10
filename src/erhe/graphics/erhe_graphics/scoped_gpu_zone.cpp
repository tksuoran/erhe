#include "erhe_graphics/scoped_gpu_zone.hpp"
#include "erhe_profile/profile.hpp"

#if defined(ERHE_PROFILE_LIBRARY_TRACY) && defined(TRACY_ENABLE) && (defined(ERHE_GRAPHICS_API_VULKAN) || defined(ERHE_GRAPHICS_API_OPENGL))
#   include "erhe_graphics/command_buffer.hpp"
#   include <cstring>
#endif
#if defined(ERHE_PROFILE_LIBRARY_TRACY) && defined(TRACY_ENABLE) && defined(ERHE_GRAPHICS_API_VULKAN)
#   include "erhe_graphics/vulkan/vulkan_command_buffer.hpp"
#   include "erhe_graphics/vulkan/vulkan_device.hpp"
#endif

namespace erhe::graphics {

#if defined(ERHE_PROFILE_LIBRARY_TRACY) && defined(TRACY_ENABLE) && defined(ERHE_GRAPHICS_API_VULKAN)

// Vulkan: explicit TracyVkCtx + VkCommandBuffer. The zone is inactive (writes
// no timestamps) when the device, the Tracy context, or the command buffer is
// unavailable -- VkCtxScope's is_active flag short-circuits the GPU commands.
class Scoped_gpu_zone_impl
{
public:
    Scoped_gpu_zone_impl(Command_buffer& command_buffer, const char* name, const std::source_location location)
        : m_zone{
            tracy_vk_ctx(),
            static_cast<uint32_t>(location.line()),
            location.file_name(),     std::strlen(location.file_name()),
            location.function_name(), std::strlen(location.function_name()),
            name,                     std::strlen(name),
            command_buffer.get_impl().get_vulkan_command_buffer(),
            (tracy_vk_ctx() != nullptr) && (command_buffer.get_impl().get_vulkan_command_buffer() != VK_NULL_HANDLE)
        }
    {
    }

private:
    [[nodiscard]] static auto tracy_vk_ctx() -> TracyVkCtx
    {
        Device_impl* const device_impl = Device_impl::get_device_impl();
        return (device_impl != nullptr) ? device_impl->get_tracy_vk_ctx() : nullptr;
    }

    tracy::VkCtxScope m_zone;
};

#elif defined(ERHE_PROFILE_LIBRARY_TRACY) && defined(TRACY_ENABLE) && defined(ERHE_GRAPHICS_API_OPENGL)

// OpenGL: Tracy's GL GPU context is thread-local, so the command buffer is not
// needed here (the zone brackets the same GL command stream regardless).
class Scoped_gpu_zone_impl
{
public:
    Scoped_gpu_zone_impl(Command_buffer&, const char* name, const std::source_location location)
        : m_zone{
            static_cast<uint32_t>(location.line()),
            location.file_name(),     std::strlen(location.file_name()),
            location.function_name(), std::strlen(location.function_name()),
            name,                     std::strlen(name),
            true
        }
    {
    }

private:
    tracy::GpuCtxScope m_zone;
};

#else

// No Tracy, or a backend without a Tracy GPU integration (Metal, null): no-op.
class Scoped_gpu_zone_impl
{
public:
    Scoped_gpu_zone_impl(Command_buffer&, const char*, const std::source_location) {}
};

#endif

static_assert(sizeof(Scoped_gpu_zone_impl) <= 128, "Scoped_gpu_zone_impl exceeds its pimpl_ptr buffer");

Scoped_gpu_zone::Scoped_gpu_zone(Command_buffer& command_buffer, const char* name, const std::source_location location)
    : m_impl{command_buffer, name, location}
{
}

Scoped_gpu_zone::~Scoped_gpu_zone() noexcept = default;

} // namespace erhe::graphics
