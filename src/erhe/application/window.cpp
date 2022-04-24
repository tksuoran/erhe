#include "erhe/application/window.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/log.hpp"
#include "erhe/application/renderdoc_capture_support.hpp"

#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/png_loader.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/toolkit/filesystem.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/window.hpp"

#if defined(ERHE_WINDOW_LIBRARY_GLFW)
#   include <GLFW/glfw3.h>
#endif

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include <openxr/openxr.h>
#endif


namespace erhe::application {

using std::shared_ptr;
using View = erhe::toolkit::View;

Window::Window()
    : erhe::components::Component{c_label}
    /////, m_renderdoc_api            {get_renderdoc_api()}
{
}

auto Window::create_gl_window() -> bool
{
    ERHE_PROFILE_FUNCTION

    const auto& configuration = *get<Configuration>();

    const char* month_name[] = {
        "January",
        "February",
        "March",
        "April",
        "May",
        "June",
        "July",
        "August",
        "September",
        "October",
        "November",
        "December"
    };

    const time_t now = time(0);
    tm* l = localtime(&now);
    std::string title = fmt::format(
        "erhe by Timo Suoranta {} {}. {}",
        month_name[l->tm_mon],
        l->tm_mday,
        1900 + l->tm_year
    );

    m_context_window = std::make_unique<erhe::toolkit::Context_window>(
        erhe::toolkit::Window_configuration{
            .fullscreen        = configuration.fullscreen,
            .width             = configuration.window_width,
            .height            = configuration.window_height,
            .msaa_sample_count = configuration.window_msaa_sample_count,
            .title             = title.c_str()
        }
    );

#if defined(ERHE_WINDOW_LIBRARY_GLFW)
    erhe::graphics::PNG_loader loader;
    erhe::graphics::Image_info image_info;
    const fs::path current_path = fs::current_path();
    const fs::path path         = current_path / "res" / "images" / "gl32w.png";
    log_startup.trace("current directory is {}\n", current_path.string());
    const bool exists          = fs::exists(path);
    const bool is_regular_file = fs::is_regular_file(path);
    if (exists && is_regular_file)
    {
        ERHE_PROFILE_SCOPE("icon");

        const bool open_ok = loader.open(path, image_info);
        if (open_ok)
        {
            std::vector<std::byte> data;
            data.resize(static_cast<size_t>(image_info.width) * static_cast<size_t>(image_info.height) * 4);
            const auto span = gsl::span<std::byte>(data.data(), data.size());
            const bool load_ok = loader.load(span);
            loader.close();
            if (load_ok)
            {
                GLFWimage icons[1] = {
                    {
                        .width  = image_info.width,
                        .height = image_info.height,
                        .pixels = reinterpret_cast<unsigned char*>(span.data())
                    }
                };
                glfwSetWindowIcon(
                    reinterpret_cast<GLFWwindow*>(
                        m_context_window->get_glfw_window()
                    ),
                    1,
                    &icons[0]
                );
            }
        }
    }
#endif

    ERHE_PROFILE_GPU_CONTEXT

    erhe::graphics::Instance::initialize();

    for (size_t i = 0; i < 3; ++i)
    {
        gl::clear_color(0.0f, 0.0f, 0.0f, 1.0f);
        gl::clear(gl::Clear_buffer_mask::color_buffer_bit | gl::Clear_buffer_mask::depth_buffer_bit);
        m_context_window->swap_buffers();
    }

    log_startup.info("Created OpenGL Window\n");

    return true;
}

auto Window::get_context_window() const -> erhe::toolkit::Context_window*
{
    return m_context_window.get();
}

void Window::begin_renderdoc_capture()
{
    ///// if (!m_renderdoc_api)
    ///// {
    /////     return;
    ///// }
    ///// log_renderdoc.trace("RenderDoc: StartFrameCapture()\n");
    ///// m_renderdoc_api->StartFrameCapture(nullptr, nullptr);
}

void Window::end_renderdoc_capture()
{
    ///// if (!m_renderdoc_api)
    ///// {
    /////     return;
    ///// }
    ///// log_renderdoc.trace("RenderDoc: EndFrameCapture()\n");
    ///// m_renderdoc_api->EndFrameCapture(nullptr, nullptr);
    ///// log_renderdoc.trace("RenderDoc: LaunchReplayUI(1, nullptr)\n");
    ///// m_renderdoc_api->LaunchReplayUI(1, nullptr);
}

}
