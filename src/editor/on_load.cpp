#include "application.hpp"
#include "configuration.hpp"
#include "gl_context_provider.hpp"
#include "log.hpp"
#include "rendering.hpp"
#include "time.hpp"
#include "tools.hpp"
#include "view.hpp"
#include "window.hpp"

#include "operations/operation_stack.hpp"

#include "renderers/forward_renderer.hpp"
#include "renderers/headset_renderer.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "renderers/program_interface.hpp"
#include "renderers/shadow_renderer.hpp"
#include "renderers/text_renderer.hpp"
#include "renderers/line_renderer.hpp"

#include "tools/fly_camera_tool.hpp"
#include "tools/grid_tool.hpp"
#include "tools/hover_tool.hpp"
#include "tools/physics_tool.hpp"
#include "tools/selection_tool.hpp"
#include "tools/trs_tool.hpp"

#include "windows/brushes.hpp"
#include "windows/camera_properties.hpp"
#include "windows/light_properties.hpp"
#include "windows/material_properties.hpp"
#include "windows/mesh_properties.hpp"
#include "windows/node_properties.hpp"
#include "windows/node_tree_window.hpp"
#include "windows/operations.hpp"
#include "windows/physics_window.hpp"
#include "windows/viewport_config.hpp"
#include "windows/viewport_window.hpp"

#include "scene/debug_draw.hpp"
#include "scene/scene_manager.hpp"
#include "scene/scene_root.hpp"

#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics_experimental/image_transfer.hpp"
#include "erhe/graphics_experimental/shader_monitor.hpp"
#include "erhe/toolkit/tracy_client.hpp"
#include "erhe/toolkit/window.hpp"
#include "erhe/toolkit/tracy_client.hpp"

#include <future>

namespace editor {

using erhe::graphics::OpenGL_state_tracker;
using erhe::graphics::Shader_monitor;
using std::shared_ptr;
using std::make_shared;

void Application::run()
{
    get<Window>()->get_context_window()->enter_event_loop();
}

auto Application::initialize_components(int argc, char** argv)
-> bool
{
    ZoneScoped;

    auto configuration        = make_shared<Configuration>(argc, argv);
    auto window               = make_shared<Window>();
    auto opengl_state_tracker = make_shared<OpenGL_state_tracker>();
    auto gl_context_provider  = make_shared<Gl_context_provider>();

    {
        ZoneScopedN("add components");
        m_components.add(configuration);
        m_components.add(window);
        m_components.add(shared_from_this());
        m_components.add(gl_context_provider);
        m_components.add(make_shared<Brushes             >());
        m_components.add(make_shared<Camera_properties   >());
        m_components.add(make_shared<Debug_draw          >());
        m_components.add(make_shared<Editor_rendering    >());
        m_components.add(make_shared<Editor_time         >());
        m_components.add(make_shared<Editor_tools        >());
        m_components.add(make_shared<Editor_view         >());
        m_components.add(make_shared<Fly_camera_tool     >());
        m_components.add(make_shared<Forward_renderer    >());
        m_components.add(make_shared<Grid_tool           >());
        m_components.add(make_shared<Headset_renderer    >());
        m_components.add(make_shared<Hover_tool          >());
        m_components.add(make_shared<Id_renderer         >());
        m_components.add(make_shared<Light_properties    >());
        m_components.add(make_shared<Line_renderer       >());
        m_components.add(make_shared<Material_properties >());
        m_components.add(make_shared<Mesh_memory         >());
        m_components.add(make_shared<Mesh_properties     >());
        m_components.add(make_shared<Node_properties     >());
        m_components.add(make_shared<Node_tree_window    >());
        m_components.add(make_shared<OpenGL_state_tracker>());
        m_components.add(make_shared<Operations          >());
        m_components.add(make_shared<Operation_stack     >());
        m_components.add(make_shared<Physics_tool        >());
        m_components.add(make_shared<Physics_window      >());
        m_components.add(make_shared<Programs            >());
        m_components.add(make_shared<Program_interface   >());
        m_components.add(make_shared<Selection_tool      >());
        m_components.add(make_shared<Shader_monitor      >());
        m_components.add(make_shared<Shadow_renderer     >());
        m_components.add(make_shared<Scene_root          >());
        m_components.add(make_shared<Scene_manager       >());
        m_components.add(make_shared<Text_renderer       >());
        m_components.add(make_shared<Trs_tool            >());
        m_components.add(make_shared<Viewport_config     >());
        m_components.add(make_shared<Viewport_window     >());
    }

    if (!window->create_gl_window())
    {
        log_startup.error("GL window creation failed, aborting\n");
        return false;
    }

    m_components.launch_component_initialization();

    gl_context_provider->provide_worker_contexts(opengl_state_tracker,
                                                 window->get_context_window(),
                                                 [this]() -> bool {
                                                     return !m_components.is_component_initialization_complete();
                                                 });

    m_components.wait_component_initialization_complete();

    component_initialization_complete(true);

    opengl_state_tracker->on_thread_enter();

    return true;
}

void Application::component_initialization_complete(bool initialization_succeeded)
{
    if (initialization_succeeded)
    {
        gl::enable(gl::Enable_cap::primitive_restart);
        gl::primitive_restart_index(0xffffu);

        auto window = get<Window>();
        if (!window)
        {
            return;
        }

        auto* context_window = window->get_context_window();
        if (context_window == nullptr)
        {
            return;
        }

        auto& root_view = context_window->get_root_view();

        root_view.reset_view(get<Editor_view>());
    }
}

}
