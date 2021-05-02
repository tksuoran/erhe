#include "application.hpp"
#include "log.hpp"
//#include "textures.hpp"
#include "renderers/programs.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/forward_renderer.hpp"
#include "renderers/shadow_renderer.hpp"
#include "renderers/text_renderer.hpp"
#include "renderers/line_renderer.hpp"
#include "scene/scene_manager.hpp"
#include "imgui_demo.hpp"
#include "editor.hpp"

#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/png_loader.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"

#include "erhe/graphics_experimental/image_transfer.hpp"
#include "erhe/graphics_experimental/shader_monitor.hpp"

#include "erhe/toolkit/window.hpp"

#include "erhe_tracy.hpp"

#if defined(ERHE_WINDOW_TOOLKIT_GLFW)
#   include <GLFW/glfw3.h>
#endif

#include <future>

namespace editor {

using erhe::graphics::Configuration;
using erhe::graphics::Shader_monitor;
using std::shared_ptr;
using std::make_shared;

class AsyncInit
    : public erhe::toolkit::View
{
public:
    AsyncInit(Application&                  application,
              erhe::components::Components& components)
        : m_application{application}
        , m_components {components}
    {
#if 0 && defined(ERHE_WINDOW_TOOLKIT_GLFW)
        erhe::toolkit::Context_window* main_window = application.get_context_window();
        m_loading_context = std::make_unique<erhe::toolkit::Context_window>(main_window);
        m_future = std::async(
            std::launch::async, [&components, this]
            {
                this->m_loading_context->make_current();

                initialize(components);

                components.on_thread_exit();
                m_loading_context->clear_current();
                m_loading_context.reset();
                return true;
            }
        );
#else
        // Mango does not provide context sharing at least on Windows yet
        initialize(components);
#endif
    }

    void initialize(erhe::components::Components& components)
    {
        components.initialize_components();
    }

    auto is_initialization_ready(bool& result) -> bool
    {
#if 0 && defined(ERHE_WINDOW_TOOLKIT_GLFW)
        auto status = m_future.wait_for(std::chrono::milliseconds(0));
        bool is_ready = (status == std::future_status::ready);
        if (is_ready)
        {
            result = m_future.get();
            m_components.on_thread_enter();
        }
        return is_ready;
#else
        result = true;
        return true;
#endif
    }

    void update() override
    {
        bool result;
        if (is_initialization_ready(result))
        {
            m_application.component_initialization_complete(result);
        }
        else
        {
            if (false)
            {
                gl::clear_color(0.3f, 0.3f, 0.3f, 1.0f);
                gl::clear(gl::Clear_buffer_mask::color_buffer_bit);
                m_application.get_context_window()->swap_buffers();

            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

private:
    Application&                                   m_application;
    erhe::components::Components&                  m_components;
    std::future<bool>                              m_future;
    std::unique_ptr<erhe::toolkit::Context_window> m_loading_context;
};


auto Application::create_gl_window()
-> bool
{
    m_context_window = std::make_unique<erhe::toolkit::Context_window>(1920, 1080);
    //m_context_window = std::make_unique<erhe::toolkit::Context_window>(1024, 1024);

#if defined(ERHE_WINDOW_TOOLKIT_GLFW)
    erhe::graphics::PNG_loader loader;
    erhe::graphics::Texture::Create_info create_info;
    std::filesystem::path current_path = std::filesystem::current_path();
    log_startup.trace("current directory is {}\n", current_path.string());
    std::filesystem::path path = current_path / "res" / "images" / "gl32w.png";
    bool exists          = std::filesystem::exists(path);
    bool is_regular_file = std::filesystem::is_regular_file(path);
    if (exists && is_regular_file)
    {
        bool ok = loader.open(path, create_info);
        if (ok)
        {
            std::vector<std::byte> data;
            data.resize(create_info.width * create_info.height * 4);
            auto span = gsl::span<std::byte>(data.data(), data.size());
            ok = loader.load(span);
            loader.close();
            if (ok)
            {
                GLFWimage icons[1];
                icons[0].width  = create_info.width;
                icons[0].height = create_info.height;
                icons[0].pixels = reinterpret_cast<unsigned char*>(span.data());
                glfwSetWindowIcon(reinterpret_cast<GLFWwindow*>(m_context_window->get_glfw_window()), 1, &icons[0]);
            }
        }
    }
#endif

    TracyGpuContext;

    Configuration::initialize(glfwGetProcAddress);

    return true;
}

auto Application::on_load()
-> bool
{
    if (!create_gl_window())
    {
        return false;
    }

    if (!launch_component_initialization())
    {
        return false;
    }

    return true;
}

void Application::run()
{
    Expects(m_context_window);

    m_context_window->enter_event_loop();
}

auto Application::launch_component_initialization()
-> bool
{
    std::shared_ptr<Programs>         programs;
    std::shared_ptr<Id_renderer>      id_renderer;
    std::shared_ptr<Forward_renderer> forward_renderer;
    std::shared_ptr<Shadow_renderer>  shadow_renderer;
    std::shared_ptr<Scene_manager>    scene_manager;

    auto application      = shared_from_this();

    if (true)
    {
        programs         = make_shared<Programs>();
        id_renderer      = make_shared<Id_renderer>();
        forward_renderer = make_shared<Forward_renderer>();
        shadow_renderer  = make_shared<Shadow_renderer>();
        scene_manager    = make_shared<Scene_manager>();
    }

    auto text_renderer   = make_shared<Text_renderer>();
    auto line_renderer   = make_shared<Line_renderer>();
    auto scene_view      = make_shared<Editor>();

    //auto textures               = make_shared<Textures>();
    //auto menu                   = make_shared<Menu>();
    //auto image_transfer         = make_shared<erhe::graphics::ImageTransfer>();
    auto pipeline_state_tracker = make_shared<erhe::graphics::OpenGL_state_tracker>();
    auto shader_monitor         = make_shared<Shader_monitor>();
    //auto imgui_demo             = make_shared<Imgui_demo>();

    if (programs)         m_components.add(programs);
    if (id_renderer)      m_components.add(id_renderer);
    if (forward_renderer) m_components.add(forward_renderer);
    if (shadow_renderer)  m_components.add(shadow_renderer);
    if (scene_manager)    m_components.add(scene_manager);
    m_components.add(text_renderer);
    m_components.add(line_renderer);
    m_components.add(scene_view);
    m_components.add(shader_monitor);
    //m_components.add(textures);
    //m_components.add(menu);
    //m_components.add(image_transfer);
    m_components.add(pipeline_state_tracker);
    //m_components.add(imgui_demo);
    m_components.add(application);

    //command_buffer->connect(pipeline_state_tracker);
    //textures->connect(image_transfer);
    //menu->connect(application, command_buffer, pipeline_state_tracker, programs, textures);
    if (programs)         programs        ->connect(shader_monitor);
    if (id_renderer)      id_renderer     ->connect(pipeline_state_tracker, scene_manager, programs);
    if (forward_renderer) forward_renderer->connect(pipeline_state_tracker, shadow_renderer, scene_manager, programs);
    if (shadow_renderer)  shadow_renderer ->connect(pipeline_state_tracker, scene_manager, programs);
    if (text_renderer)    text_renderer   ->connect(pipeline_state_tracker);
    if (line_renderer)    line_renderer   ->connect(pipeline_state_tracker, shader_monitor);
    if (scene_manager)    scene_manager   ->connect(programs);
    if (scene_view)       scene_view      ->connect(application, forward_renderer, id_renderer, scene_manager, pipeline_state_tracker, shader_monitor, shadow_renderer, text_renderer, line_renderer);
    //////scene_view      ->connect(application, {}, {}, {}, shader_monitor, {}, stream_renderer);
    //imgui_demo->connect(application, pipeline_state_tracker);

    //connect(menu);
    //connect(imgui_demo);
    connect(scene_view);

    // if (false)
    // {
    //     m_context_window->get_root_view().set_view(make_shared<AsyncInit>(*this, m_components));
    // }
    // else
    {
        bool ok{true};
        try
        {
            m_components.initialize_components();
        }
        catch(...)
        {
            ok = false;
        }
        component_initialization_complete(ok);
    }

    return true;
}

void Application::component_initialization_complete(bool initialization_succeeded)
{
    if (initialization_succeeded)
    {
        gl::enable(gl::Enable_cap::primitive_restart);
        gl::primitive_restart_index(0xffffu);
        //m_context_window->get_root_view().reset_view(m_menu);
        //m_context_window->get_root_view().reset_view(m_imgui_demo);
        m_context_window->get_root_view().reset_view(m_scene_view);
    }
}

}
