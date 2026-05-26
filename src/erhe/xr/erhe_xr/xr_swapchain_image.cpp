#include "erhe_xr/xr_swapchain_image.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/debug_label.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_xr/xr.hpp"
#include "erhe_xr/xr_log.hpp"

#if defined(ERHE_OS_WINDOWS)
#   include <unknwn.h>
#endif

// volk.h must precede <openxr/openxr_platform.h> when XR_USE_GRAPHICS_API_VULKAN
// is defined: openxr_platform.h references VkFormat / VkImage / etc. without
// including <vulkan/vulkan.h> itself.
#if defined(XR_USE_GRAPHICS_API_VULKAN)
# include "volk.h"
#endif

// jni.h must precede openxr_platform.h on Android.
#if defined(XR_USE_PLATFORM_ANDROID)
# include <jni.h>
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdint>
#include <cstring>

namespace erhe::xr {

Swapchain_image::Swapchain_image(Swapchain* swapchain, const uint32_t image_index)
    : m_swapchain  {swapchain}
    , m_image_index{image_index}
{
}

Swapchain_image::~Swapchain_image() noexcept
{
    if (m_swapchain != nullptr) {
        m_swapchain->release();
    }
}

Swapchain_image::Swapchain_image(Swapchain_image&& other) noexcept
    : m_swapchain  {other.m_swapchain}
    , m_image_index{other.m_image_index}
{
    other.m_swapchain = nullptr;
}

void Swapchain_image::operator=(Swapchain_image&& other) noexcept
{
    m_swapchain       = other.m_swapchain;
    m_image_index     = other.m_image_index;
    other.m_swapchain = nullptr;
}

auto Swapchain_image::get_image_index() const -> uint32_t
{
    ERHE_VERIFY(m_swapchain != nullptr);

    return m_image_index;
}

auto Swapchain_image::get_texture() const -> erhe::graphics::Texture*
{
    ERHE_VERIFY(m_swapchain != nullptr);

    return m_swapchain->get_texture(m_image_index);
}

auto Swapchain_image::get_fragment_density_map_texture() const -> erhe::graphics::Texture*
{
    ERHE_VERIFY(m_swapchain != nullptr);

    return m_swapchain->get_fragment_density_map_texture(m_image_index);
}

Swapchain::Swapchain(
    erhe::graphics::Device&        device,
    XrSwapchain                    xr_swapchain,
    erhe::dataformat::Format       pixelformat,
    uint32_t                       width,
    uint32_t                       height,
    uint32_t                       sample_count,
    uint32_t                       array_layer_count,
    uint64_t                       texture_usage_mask,
    bool                           want_fragment_density_map,
    const std::string&             debug_label
)
    : m_xr_swapchain     {xr_swapchain}
    , m_array_layer_count{array_layer_count == 0 ? 1u : array_layer_count}
{
    ERHE_PROFILE_FUNCTION();

    if (xr_swapchain != XR_NULL_HANDLE) {
        if (!enumerate_images(device, pixelformat, width, height, sample_count, m_array_layer_count, texture_usage_mask, want_fragment_density_map, debug_label)) {
            log_xr->warn("Swapchain::enumerate_images() failed");
        }
    }
}

Swapchain::~Swapchain() noexcept
{
    ERHE_PROFILE_FUNCTION();

    if (m_xr_swapchain != XR_NULL_HANDLE) {
        check_gl_context_in_current_in_this_thread();
        check(xrDestroySwapchain(m_xr_swapchain));
    }
}

Swapchain::Swapchain(Swapchain&& other) noexcept
    : m_xr_swapchain     {other.m_xr_swapchain}
    , m_array_layer_count{other.m_array_layer_count}
    , m_textures         {std::move(other.m_textures)}
    , m_textures_fdm     {std::move(other.m_textures_fdm)}
{
    other.m_xr_swapchain      = XR_NULL_HANDLE;
    other.m_array_layer_count = 1;
}

void Swapchain::operator=(Swapchain&& other) noexcept
{
    ERHE_VERIFY(other.m_xr_swapchain != XR_NULL_HANDLE);
    if (m_xr_swapchain != XR_NULL_HANDLE) {
        check_gl_context_in_current_in_this_thread();
        check(xrDestroySwapchain(m_xr_swapchain));
    }
    m_xr_swapchain            = other.m_xr_swapchain;
    m_array_layer_count       = other.m_array_layer_count;
    m_textures                = std::move(other.m_textures);
    m_textures_fdm            = std::move(other.m_textures_fdm);
    other.m_xr_swapchain      = XR_NULL_HANDLE;
    other.m_array_layer_count = 1;
}

auto Swapchain::get_array_layer_count() const -> uint32_t
{
    return m_array_layer_count;
}

auto Swapchain::acquire() -> std::optional<Swapchain_image>
{
    ERHE_PROFILE_FUNCTION();

    if (m_xr_swapchain == XR_NULL_HANDLE) {
        return {};
    }

    const XrSwapchainImageAcquireInfo swapchain_image_acquire_info{
        .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO,
        .next = nullptr
    };

    uint32_t image_index = 0;

    check_gl_context_in_current_in_this_thread();
    if (!check(xrAcquireSwapchainImage(m_xr_swapchain, &swapchain_image_acquire_info, &image_index))) {
        return {};
    }

    assert(image_index < 16);
    return Swapchain_image(this, image_index);
}

auto Swapchain::release() -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (m_xr_swapchain == XR_NULL_HANDLE) {
        return true;
    }

    const XrSwapchainImageReleaseInfo swapchain_image_release_info{
        .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
        .next = nullptr,
    };

    check_gl_context_in_current_in_this_thread();
    return check(
        xrReleaseSwapchainImage(m_xr_swapchain, &swapchain_image_release_info)
    );
}

auto Swapchain::wait() -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (m_xr_swapchain == XR_NULL_HANDLE) {
        return false;
    }

    const XrSwapchainImageWaitInfo swapchain_image_wait_info{
        .type    = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
        .next    = nullptr,
        .timeout = XR_INFINITE_DURATION
    };

    check_gl_context_in_current_in_this_thread();
    return check(
        xrWaitSwapchainImage(m_xr_swapchain, &swapchain_image_wait_info)
    );
}

auto Swapchain::get_texture(const uint32_t image_index) const -> erhe::graphics::Texture*
{
    ERHE_VERIFY(image_index < m_textures.size());

    return m_textures[image_index].get();
}

auto Swapchain::get_fragment_density_map_texture(const uint32_t image_index) const -> erhe::graphics::Texture*
{
    // m_textures_fdm is empty unless the swapchain was created with foveation;
    // return nullptr for any index in that case (and for out-of-range indices).
    if (image_index >= m_textures_fdm.size()) {
        return nullptr;
    }
    return m_textures_fdm[image_index].get();
}

auto Swapchain::get_xr_swapchain() const -> XrSwapchain
{
    return m_xr_swapchain;
}

namespace {

[[nodiscard]] auto wrap_swapchain_texture(
    erhe::graphics::Device&        device,
    uint64_t                       wrap_texture_name,
    erhe::dataformat::Format       pixelformat,
    uint32_t                       width,
    uint32_t                       height,
    uint32_t                       sample_count,
    uint32_t                       array_layer_count,
    uint64_t                       texture_usage_mask,
    const std::string&             debug_label
) -> std::shared_ptr<erhe::graphics::Texture>
{
    // The codebase convention is sample_count == 0 for non-MSAA textures;
    // OpenXR's recommendedSwapchainSampleCount is 1 for non-MSAA, so map 1 to 0.
    const int normalized_sample_count = (sample_count > 1) ? static_cast<int>(sample_count) : 0;
    // Layered XR images (arraySize > 1) back the multiview render path: a
    // single layered image holds one image per view. Wrap as a 2D array
    // texture so the render pass can attach a 2D_ARRAY image view spanning
    // every layer, which is what VK_KHR_multiview's viewMask reads from.
    const bool                        is_layered = (array_layer_count > 1);
    const erhe::graphics::Texture_type texture_type = is_layered
        ? erhe::graphics::Texture_type::texture_2d_array
        : erhe::graphics::Texture_type::texture_2d;
    erhe::graphics::Texture_create_info create_info{
        .device            = device,
        .usage_mask        = texture_usage_mask,
        .type              = texture_type,
        .pixelformat       = pixelformat,
        .sample_count      = normalized_sample_count,
        .width             = static_cast<int>(width),
        .height            = static_cast<int>(height),
        .array_layer_count = is_layered ? static_cast<int>(array_layer_count) : 0,
        .wrap_texture_name = wrap_texture_name,
        .debug_label       = erhe::utility::Debug_label{debug_label}
    };
    return std::make_shared<erhe::graphics::Texture>(device, create_info);
}

} // namespace

auto Swapchain::enumerate_images(
    erhe::graphics::Device&        device,
    erhe::dataformat::Format       pixelformat,
    uint32_t                       width,
    uint32_t                       height,
    uint32_t                       sample_count,
    uint32_t                       array_layer_count,
    uint64_t                       texture_usage_mask,
    bool                           want_fragment_density_map,
    const std::string&             debug_label
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (m_xr_swapchain == XR_NULL_HANDLE){
        return false;
    }

    uint32_t image_count{0};

    check_gl_context_in_current_in_this_thread();
    ERHE_XR_CHECK(xrEnumerateSwapchainImages(m_xr_swapchain, 0, &image_count, nullptr));

    m_textures.resize(image_count);

#if defined(XR_USE_GRAPHICS_API_OPENGL)
    // Fixed foveated rendering is Vulkan-only; the OpenGL path never requests it.
    static_cast<void>(want_fragment_density_map);
    std::vector<XrSwapchainImageOpenGLKHR> swapchain_images(image_count);
    for (uint32_t i = 0; i < image_count; i++) {
        swapchain_images[i].type  = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
        swapchain_images[i].next  = nullptr;
        swapchain_images[i].image = 0;
    }
    check_gl_context_in_current_in_this_thread();
    ERHE_XR_CHECK(
        xrEnumerateSwapchainImages(
            m_xr_swapchain,
            image_count,
            &image_count,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchain_images.data())
        )
    );

    for (uint32_t i = 0; i < image_count; i++) {
        m_textures[i] = wrap_swapchain_texture(
            device,
            static_cast<uint64_t>(swapchain_images[i].image),
            pixelformat,
            width,
            height,
            sample_count,
            array_layer_count,
            texture_usage_mask,
            debug_label + " " + std::to_string(i)
        );
    }
#elif defined(XR_USE_GRAPHICS_API_VULKAN)
    std::vector<XrSwapchainImageVulkanKHR> swapchain_images(image_count);
    // Fixed foveated rendering: a parallel fragment-density-map image struct per
    // swapchain image, chained into each color image struct's next so
    // xrEnumerateSwapchainImages fills in the FDM VkImage and its dimensions.
    // Only built when the swapchain was created with the foveation flag.
    std::vector<XrSwapchainImageFoveationVulkanFB> fdm_images;
    if (want_fragment_density_map) {
        fdm_images.resize(image_count);
    }
    for (uint32_t i = 0; i < image_count; i++) {
        swapchain_images[i].type  = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
        swapchain_images[i].next  = nullptr;
        swapchain_images[i].image = VK_NULL_HANDLE;
        if (want_fragment_density_map) {
            fdm_images[i].type       = XR_TYPE_SWAPCHAIN_IMAGE_FOVEATION_VULKAN_FB;
            fdm_images[i].next       = nullptr;
            fdm_images[i].image      = VK_NULL_HANDLE;
            fdm_images[i].width      = 0;
            fdm_images[i].height     = 0;
            swapchain_images[i].next = &fdm_images[i];
        }
    }
    ERHE_XR_CHECK(
        xrEnumerateSwapchainImages(
            m_xr_swapchain,
            image_count,
            &image_count,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchain_images.data())
        )
    );

    if (want_fragment_density_map) {
        m_textures_fdm.resize(image_count);
    }
    for (uint32_t i = 0; i < image_count; i++) {
        const VkImage vk_image = swapchain_images[i].image;
        uint64_t wrap_handle = 0;
        static_assert(sizeof(vk_image) <= sizeof(wrap_handle), "VkImage handle does not fit in uint64_t");
        std::memcpy(&wrap_handle, &vk_image, sizeof(vk_image));
        m_textures[i] = wrap_swapchain_texture(
            device,
            wrap_handle,
            pixelformat,
            width,
            height,
            sample_count,
            array_layer_count,
            texture_usage_mask,
            debug_label + " " + std::to_string(i)
        );

        if (want_fragment_density_map) {
            const VkImage fdm_vk_image = fdm_images[i].image;
            if (fdm_vk_image != VK_NULL_HANDLE) {
                uint64_t fdm_wrap_handle = 0;
                std::memcpy(&fdm_wrap_handle, &fdm_vk_image, sizeof(fdm_vk_image));
                // FDM images are VK_FORMAT_R8G8_UNORM at tile resolution (much
                // smaller than the color image); use the runtime-reported FDM
                // dimensions. Same array layer count as the color swapchain (one
                // FDM layer per view for multiview). Non-MSAA. Usage is
                // fragment-density-map only.
                m_textures_fdm[i] = wrap_swapchain_texture(
                    device,
                    fdm_wrap_handle,
                    erhe::dataformat::Format::format_8_vec2_unorm,
                    fdm_images[i].width,
                    fdm_images[i].height,
                    1,
                    array_layer_count,
                    erhe::graphics::Image_usage_flag_bit_mask::fragment_density_map,
                    debug_label + " FDM " + std::to_string(i)
                );
                log_xr->info(
                    "OpenXR FDM image {}: {}x{} layers {}",
                    i, fdm_images[i].width, fdm_images[i].height, array_layer_count
                );
            } else {
                log_xr->warn("OpenXR FDM image {} is VK_NULL_HANDLE; foveation inactive for this image", i);
            }
        }
    }
#else
    static_cast<void>(device);
    static_cast<void>(pixelformat);
    static_cast<void>(width);
    static_cast<void>(height);
    static_cast<void>(sample_count);
    static_cast<void>(array_layer_count);
    static_cast<void>(texture_usage_mask);
    static_cast<void>(want_fragment_density_map);
    static_cast<void>(debug_label);
#endif

    return true;
}

} // namespace erhe::xr
