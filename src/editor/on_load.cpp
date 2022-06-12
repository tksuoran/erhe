#include "editor_rendering.hpp"
#include "view_client.hpp"
#include "graphics/icon_set.hpp"
#include "graphics/image_transfer.hpp"
//#include "graphics/textures.hpp"
#include "operations/operation_stack.hpp"

#include "renderers/forward_renderer.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_renderer.hpp"
#   include "xr/hand_tracker.hpp"
#   include "xr/theremin.hpp"
#endif
#include "renderers/id_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/post_processing.hpp"
#include "renderers/program_interface.hpp"
#include "renderers/programs.hpp"
#include "renderers/shadow_renderer.hpp"
//#include "renderers/texture_renderer.hpp"

#include "tools/fly_camera_tool.hpp"
#include "tools/grid_tool.hpp"
#include "tools/hover_tool.hpp"
#include "tools/material_paint_tool.hpp"
#include "tools/physics_tool.hpp"
#include "tools/pointer_context.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "tools/trs_tool.hpp"

#include "windows/brushes.hpp"
#include "windows/debug_view_window.hpp"
#include "windows/layers_window.hpp"
#include "windows/material_properties.hpp"
#include "windows/materials.hpp"
#include "windows/mesh_properties.hpp"
#include "windows/node_properties.hpp"
#include "windows/node_tree_window.hpp"
#include "windows/operations.hpp"
#include "windows/physics_window.hpp"
#include "windows/tool_properties_window.hpp"
#include "windows/viewport_config.hpp"
#include "windows/viewport_window.hpp"

#include "scene/debug_draw.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"

#include "erhe/application/application.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/log.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/time.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/window.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/graphics/shader_monitor.hpp"
#include "erhe/application/renderers/imgui_renderer.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/application/windows/imgui_demo_window.hpp"
#include "erhe/application/windows/log_window.hpp"
#include "erhe/application/windows/performance_window.hpp"
#include "erhe/application/windows/pipelines.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/window.hpp"

#include <future>

namespace erhe::application {

using erhe::graphics::OpenGL_state_tracker;
using std::shared_ptr;
using std::make_shared;

auto Application::initialize_components(int argc, char** argv) -> bool
{
    ERHE_PROFILE_FUNCTION

    auto configuration        = make_shared<erhe::application::Configuration>(argc, argv);
    auto window               = make_shared<erhe::application::Window>();
    auto gl_context_provider  = make_shared<erhe::application::Gl_context_provider>();
    auto opengl_state_tracker = make_shared<erhe::graphics::OpenGL_state_tracker>();

    {
        ERHE_PROFILE_SCOPE("add components");

        m_components.add(configuration);
        m_components.add(window);
        m_components.add(shared_from_this());
        m_components.add(gl_context_provider);
        m_components.add(make_shared<erhe::application::Imgui_windows         >());
        m_components.add(make_shared<erhe::application::Time                  >());
        m_components.add(make_shared<erhe::application::View                  >());
        m_components.add(make_shared<erhe::application::Log_window            >());
        m_components.add(make_shared<erhe::application::Imgui_demo_window     >());
        m_components.add(make_shared<erhe::application::Imgui_renderer        >());
        m_components.add(make_shared<erhe::application::Performance_window    >());
        m_components.add(make_shared<erhe::application::Pipelines             >());
        m_components.add(make_shared<erhe::application::Shader_monitor        >());
        m_components.add(make_shared<erhe::application::Text_renderer         >());
        m_components.add(make_shared<erhe::application::Line_renderer_set     >());

        m_components.add(make_shared<erhe::graphics::OpenGL_state_tracker >());

        m_components.add(make_shared<editor::Program_interface   >());
        m_components.add(make_shared<editor::Programs            >());
        m_components.add(make_shared<editor::Image_transfer      >());
        m_components.add(make_shared<editor::Brushes             >());
        m_components.add(make_shared<editor::Debug_draw          >());
        m_components.add(make_shared<editor::Debug_view_window   >());
        m_components.add(make_shared<editor::Editor_rendering    >());
        m_components.add(make_shared<editor::Fly_camera_tool     >());
        m_components.add(make_shared<editor::Forward_renderer    >());
        m_components.add(make_shared<editor::Grid_tool           >());
        m_components.add(make_shared<editor::Hover_tool          >());
        m_components.add(make_shared<editor::Icon_set            >());
        m_components.add(make_shared<editor::Id_renderer         >());
        m_components.add(make_shared<editor::Layers_window       >());
        m_components.add(make_shared<editor::Material_paint_tool >());
        m_components.add(make_shared<editor::Material_properties >());
        m_components.add(make_shared<editor::Materials           >());
        m_components.add(make_shared<editor::Mesh_memory         >());
        m_components.add(make_shared<editor::Mesh_properties     >());
        m_components.add(make_shared<editor::Node_properties     >());
        m_components.add(make_shared<editor::Node_tree_window    >());
        m_components.add(make_shared<editor::Operation_stack     >());
        m_components.add(make_shared<editor::Operations          >());
        m_components.add(make_shared<editor::Physics_tool        >());
        m_components.add(make_shared<editor::Physics_window      >());
        m_components.add(make_shared<editor::Pointer_context     >());
        m_components.add(make_shared<editor::Post_processing     >());
        m_components.add(make_shared<editor::Scene_builder       >());
        m_components.add(make_shared<editor::Scene_root          >());
        m_components.add(make_shared<editor::Selection_tool      >());
        m_components.add(make_shared<editor::Shadow_renderer     >());
        //m_components.add(make_shared<editor::Texture_renderer    >());
        //m_components.add(make_shared<editor::Textures            >());
        m_components.add(make_shared<editor::Tools                 >());
        m_components.add(make_shared<editor::Tool_properties_window>());
        m_components.add(make_shared<editor::Trs_tool            >());
        m_components.add(make_shared<editor::Viewport_config     >());
        m_components.add(make_shared<editor::Viewport_windows    >());
        m_components.add(make_shared<editor::View_client         >());

#if defined(ERHE_XR_LIBRARY_OPENXR)
        m_components.add(make_shared<Hand_tracker    >());
        m_components.add(make_shared<Headset_renderer>());
        m_components.add(make_shared<Theremin        >());
#endif

    }

    if (!window->create_gl_window())
    {
        log_startup->error("GL window creation failed, aborting");
        return false;
    }

    m_components.launch_component_initialization(configuration->threading.parallel_initialization);

    if (configuration->threading.parallel_initialization)
    {
        gl_context_provider->provide_worker_contexts(
            opengl_state_tracker,
            window->get_context_window(),
            [this]() -> bool {
                return !m_components.is_component_initialization_complete();
            }
        );
    }

    m_components.wait_component_initialization_complete();

    component_initialization_complete(true);

    if (m_components.get<erhe::application::Imgui_demo_window >()) m_components.get<erhe::application::Imgui_demo_window >()->hide();
    if (m_components.get<erhe::application::Log_window        >()) m_components.get<erhe::application::Log_window        >()->hide();
    if (m_components.get<erhe::application::Performance_window>()) m_components.get<erhe::application::Performance_window>()->hide();
    if (m_components.get<erhe::application::Pipelines         >()) m_components.get<erhe::application::Pipelines         >()->hide();
    if (m_components.get<erhe::application::View              >()) m_components.get<erhe::application::View              >()->hide();
    //if (m_components.get<erhe::application::Tool_properties_window>()) m_components.get<Terhe::application::ool_properties_window>()->hide();

    if (m_components.get<editor::Debug_view_window  >()) m_components.get<editor::Debug_view_window >()->hide();
    if (m_components.get<editor::Fly_camera_tool    >()) m_components.get<editor::Fly_camera_tool   >()->hide();
    if (m_components.get<editor::Grid_tool          >()) m_components.get<editor::Grid_tool         >()->hide();
    if (m_components.get<editor::Layers_window      >()) m_components.get<editor::Layers_window     >()->hide();
    if (m_components.get<editor::Mesh_properties    >()) m_components.get<editor::Mesh_properties   >()->hide();
    if (m_components.get<editor::Operation_stack    >()) m_components.get<editor::Operation_stack   >()->hide();
    if (m_components.get<editor::Physics_window     >()) m_components.get<editor::Physics_window    >()->hide();
    if (m_components.get<editor::Post_processing    >()) m_components.get<editor::Post_processing   >()->hide();
    //if (m_components.get<editor::Node_tree_window   >()) m_components.get<editor::Node_tree_window   >()->hide();
    //if (m_components.get<editor::Node_properties    >()) m_components.get<editor::Node_properties    >()->hide();
    //if (m_components.get<editor::Materials          >()) m_components.get<editor::Materials          >()->hide();
    //if (m_components.get<editor::Material_properties>()) m_components.get<editor::Material_properties>()->hide();
    //if (m_components.get<editor::Brushes            >()) m_components.get<editor::Brushes            >()->hide();
    //if (m_components.get<editor::Operations         >()) m_components.get<editor::Operations         >()->hide();
    //if (m_components.get<editor::Trs_tool           >()) m_components.get<editor::Trs_tool           >()->hide();
    if (m_components.get<editor::Viewport_config       >()) m_components.get<editor::Viewport_config       >()->hide();
    //if (m_components.get<editor::Scene_root>()) m_components.get<editor::Scene_root>()->physics_world().enable_physics_updates();

    opengl_state_tracker->on_thread_enter();

    return true;
}

void Application::component_initialization_complete(const bool initialization_succeeded)
{
    if (initialization_succeeded)
    {
        gl::enable(gl::Enable_cap::primitive_restart);
        gl::primitive_restart_index(0xffffu);

        const auto window = get<Window>();
        if (!window)
        {
            return;
        }

        auto* const context_window = window->get_context_window();
        if (context_window == nullptr)
        {
            return;
        }

        auto& root_view = context_window->get_root_view();

        root_view.reset_view(get<erhe::application::View>().get());
    }
}

}
