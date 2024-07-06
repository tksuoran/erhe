#include "erhe_xr/xr_swapchain_image.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_xr/xr.hpp"
#include "erhe_xr/xr_log.hpp"

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

auto Swapchain_image::get_gl_texture() const -> uint32_t
{
    ERHE_VERIFY(m_swapchain != nullptr);

    return m_swapchain->get_gl_texture(m_image_index);
}

Swapchain::Swapchain(XrSwapchain xr_swapchain)
    : m_xr_swapchain{xr_swapchain}
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(xr_swapchain != XR_NULL_HANDLE);
    if (!enumerate_images()) {
        log_xr->warn("Swapchain::enumerate_images() failed");
    }
}

Swapchain::~Swapchain()
{
    ERHE_PROFILE_FUNCTION();

    if (m_xr_swapchain != XR_NULL_HANDLE) {
        check_gl_context_in_current_in_this_thread();
        check(xrDestroySwapchain(m_xr_swapchain));
    }
}

Swapchain::Swapchain(Swapchain&& other) noexcept
    : m_xr_swapchain{other.m_xr_swapchain}
    , m_gl_textures {std::move(other.m_gl_textures)}
{
    other.m_xr_swapchain = XR_NULL_HANDLE;
}

void Swapchain::operator=(Swapchain&& other) noexcept
{
    ERHE_VERIFY(other.m_xr_swapchain != XR_NULL_HANDLE);
    if (m_xr_swapchain != XR_NULL_HANDLE) {
        check_gl_context_in_current_in_this_thread();
        check(xrDestroySwapchain(m_xr_swapchain));
    }
    m_xr_swapchain = other.m_xr_swapchain;
    m_gl_textures  = std::move(other.m_gl_textures);
    other.m_xr_swapchain = XR_NULL_HANDLE;
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
    if (
        !check(
            xrAcquireSwapchainImage(
                m_xr_swapchain,
                &swapchain_image_acquire_info,
                &image_index
            )
        )
    ) {
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

auto Swapchain::get_gl_texture(const uint32_t image_index) const -> unsigned int
{
    ERHE_VERIFY(image_index < m_gl_textures.size());

    return m_gl_textures[image_index];
}

auto Swapchain::get_xr_swapchain() const -> XrSwapchain
{
    return m_xr_swapchain;
}

auto Swapchain::enumerate_images() -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (m_xr_swapchain == XR_NULL_HANDLE){
        return false;
    }

    uint32_t image_count{0};

    check_gl_context_in_current_in_this_thread();
    ERHE_XR_CHECK(xrEnumerateSwapchainImages(m_xr_swapchain, 0, &image_count, nullptr));

    m_gl_textures.resize(image_count);
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
        m_gl_textures[i] = swapchain_images[i].image;
    }

    return true;
}


}
