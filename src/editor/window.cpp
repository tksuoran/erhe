#include "window.hpp"
#include "configuration.hpp"
#include "log.hpp"
#include "renderdoc_capture_support.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/png_loader.hpp"
#include "erhe/toolkit/tracy_client.hpp"
#include "erhe/toolkit/window.hpp"

#if defined(ERHE_WINDOW_LIBRARY_GLFW)
#   include <GLFW/glfw3.h>
#endif

#include <openxr/openxr.h>

namespace editor {

using std::shared_ptr;
using View = erhe::toolkit::View;

Window::Window()
    : erhe::components::Component{c_name}
    , m_renderdoc_api            {get_renderdoc_api()}
{
}

auto Window::create_gl_window() -> bool
{
    ZoneScoped;

    const auto& configuration = *get<Configuration>().get();

    int msaa_sample_count = configuration.gui ? 0 : 16;

    m_context_window = std::make_unique<erhe::toolkit::Context_window>(1920, 1080, msaa_sample_count); // 1080p
    //m_context_window = std::make_unique<erhe::toolkit::Context_window>(1280,  720, msaa_sample_count); // 720p

#if defined(ERHE_WINDOW_LIBRARY_GLFW)
    erhe::graphics::PNG_loader loader;
    erhe::graphics::Image_info image_info;
    std::filesystem::path current_path = std::filesystem::current_path();
    log_startup.trace("current directory is {}\n", current_path.string());
    std::filesystem::path path = current_path / "res" / "images" / "gl32w.png";
    bool exists          = std::filesystem::exists(path);
    bool is_regular_file = std::filesystem::is_regular_file(path);
    if (exists && is_regular_file)
    {
        ZoneScopedN("icon");

        bool ok = loader.open(path, image_info);
        if (ok)
        {
            std::vector<std::byte> data;
            data.resize(static_cast<size_t>(image_info.width) * static_cast<size_t>(image_info.height) * 4);
            auto span = gsl::span<std::byte>(data.data(), data.size());
            ok = loader.load(span);
            loader.close();
            if (ok)
            {
                GLFWimage icons[1];
                icons[0].width  = image_info.width;
                icons[0].height = image_info.height;
                icons[0].pixels = reinterpret_cast<unsigned char*>(span.data());
                glfwSetWindowIcon(reinterpret_cast<GLFWwindow*>(m_context_window->get_glfw_window()), 1, &icons[0]);
            }
        }
    }
#endif

    TracyGpuContext;

    erhe::graphics::Instance::initialize();

    log_startup.info("Created OpenGL Window\n");

    return true;
}

auto Window::get_context_window() const -> erhe::toolkit::Context_window*
{
    return m_context_window.get();
}

void Window::begin_renderdoc_capture()
{
    if (!m_renderdoc_api)
    {
        return;
    }
    log_renderdoc.trace("RenderDoc: StartFrameCapture()\n");
    m_renderdoc_api->StartFrameCapture(nullptr, nullptr);
}

void Window::end_renderdoc_capture()
{
    if (!m_renderdoc_api)
    {
        return;
    }
    log_renderdoc.trace("RenderDoc: EndFrameCapture()\n");
    m_renderdoc_api->EndFrameCapture(nullptr, nullptr);
    log_renderdoc.trace("RenderDoc: LaunchReplayUI(1, nullptr)\n");
    m_renderdoc_api->LaunchReplayUI(1, nullptr);
}

}
