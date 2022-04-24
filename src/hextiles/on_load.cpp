#include "game.hpp"
#include "game_window.hpp"
#include "map_renderer.hpp"
#include "map_window.hpp"
#include "menu_window.hpp"
#include "new_game_window.hpp"
#include "rendering.hpp"
#include "tiles.hpp"
#include "view_client.hpp"
#include "map_editor/map_editor.hpp"
#include "map_editor/map_tool_window.hpp"
#include "map_editor/terrain_palette_window.hpp"
#include "map_generator/map_generator.hpp"
#include "type_editors/terrain_editor_window.hpp"
#include "type_editors/terrain_group_editor_window.hpp"
#include "type_editors/terrain_replacement_rule_editor_window.hpp"
#include "type_editors/type_editor.hpp"
#include "type_editors/unit_editor_window.hpp"

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

#include "erhe/application/windows/log_window.hpp"
#include "erhe/application/windows/performance_window.hpp"
#include "erhe/application/windows/pipelines.hpp"

#include "erhe/application/windows/imgui_demo_window.hpp"

#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/window.hpp"

#include <future>

namespace erhe::application {

using std::shared_ptr;
using std::make_shared;

auto Application::initialize_components(int argc, char** argv) -> bool
{
    ERHE_PROFILE_FUNCTION

    auto configuration        = make_shared<erhe::application::Configuration      >(argc, argv);
    auto window               = make_shared<erhe::application::Window             >();
    auto gl_context_provider  = make_shared<erhe::application::Gl_context_provider>();
    auto opengl_state_tracker = make_shared<erhe::graphics::OpenGL_state_tracker  >();

    {
        ERHE_PROFILE_SCOPE("add components");

        m_components.add(configuration);
        m_components.add(window);
        m_components.add(shared_from_this());
        m_components.add(gl_context_provider);
        m_components.add(make_shared<erhe::application::Imgui_windows     >());
        m_components.add(make_shared<erhe::application::Time              >());
        m_components.add(make_shared<erhe::application::View              >());
        m_components.add(make_shared<erhe::application::Log_window        >());
        m_components.add(make_shared<erhe::application::Imgui_demo_window >());
        m_components.add(make_shared<erhe::application::Imgui_renderer    >());
        m_components.add(make_shared<erhe::application::Performance_window>());
        m_components.add(make_shared<erhe::application::Pipelines         >());
        m_components.add(make_shared<erhe::application::Shader_monitor    >());
        m_components.add(make_shared<erhe::application::Text_renderer     >());
        m_components.add(make_shared<erhe::graphics::OpenGL_state_tracker >());
        m_components.add(make_shared<hextiles::Game                                  >());
        m_components.add(make_shared<hextiles::Game_window                           >());
        m_components.add(make_shared<hextiles::Map_editor                            >());
        m_components.add(make_shared<hextiles::Map_generator                         >());
        m_components.add(make_shared<hextiles::Map_renderer                          >());
        m_components.add(make_shared<hextiles::Map_tool_window                       >());
        m_components.add(make_shared<hextiles::Map_window                            >());
        m_components.add(make_shared<hextiles::Menu_window                           >());
        m_components.add(make_shared<hextiles::New_game_window                       >());
        m_components.add(make_shared<hextiles::Rendering                             >());
        m_components.add(make_shared<hextiles::Terrain_editor_window                 >());
        m_components.add(make_shared<hextiles::Terrain_group_editor_window           >());
        m_components.add(make_shared<hextiles::Terrain_replacement_rule_editor_window>());
        m_components.add(make_shared<hextiles::Terrain_palette_window                >());
        m_components.add(make_shared<hextiles::Tiles                                 >());
        m_components.add(make_shared<hextiles::Type_editor                           >());
        m_components.add(make_shared<hextiles::Unit_editor_window                    >());
        m_components.add(make_shared<hextiles::View_client                           >());
    }

    if (!window->create_gl_window())
    {
        log_startup.error("GL window creation failed, aborting\n");
        return false;
    }

    m_components.launch_component_initialization(configuration->parallel_initialization);

    if (configuration->parallel_initialization)
    {
        gl_context_provider->provide_worker_contexts(
            opengl_state_tracker,
            window->get_context_window(),
            [this]() -> bool
            {
                return !m_components.is_component_initialization_complete();
            }
        );
    }

    m_components.wait_component_initialization_complete();

    component_initialization_complete(true);

    m_components.get<erhe::application::View              >()->hide();
    m_components.get<erhe::application::Imgui_demo_window >()->hide();
    m_components.get<erhe::application::Log_window        >()->hide();
    m_components.get<erhe::application::Performance_window>()->hide();
    m_components.get<erhe::application::Pipelines         >()->hide();

    m_components.get<hextiles::Menu_window>()->show_menu();

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
