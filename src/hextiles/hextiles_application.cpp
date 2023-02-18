#include "hextiles_application.hpp"
#include "game/game.hpp"
#include "game/game_window.hpp"
#include "hextiles_view_client.hpp"
#include "map.hpp"
#include "map_editor/map_editor.hpp"
#include "map_editor/map_tool_window.hpp"
#include "map_editor/terrain_palette_window.hpp"
#include "map_generator/map_generator.hpp"
#include "map_window.hpp"
#include "menu_window.hpp"
#include "new_game_window.hpp"
#include "rendering.hpp"
#include "tile_renderer.hpp"
#include "tiles.hpp"
#include "type_editors/type_editor.hpp"
#include "type_editors/terrain_editor_window.hpp"
#include "type_editors/terrain_group_editor_window.hpp"
#include "type_editors/terrain_replacement_rule_editor_window.hpp"
#include "type_editors/unit_editor_window.hpp"

#include "erhe/application/application_log.hpp"
#include "erhe/components/components_log.hpp"
#include "erhe/gl/gl_log.hpp"
#include "erhe/graphics/graphics_log.hpp"
#include "erhe/toolkit/toolkit_log.hpp"
#include "erhe/ui/ui_log.hpp"
#include "erhe/log/log.hpp"
#include "hextiles_log.hpp"

#include "erhe/application/application_view.hpp"
#include "erhe/application/application_view.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/graphics/shader_monitor.hpp"
#include "erhe/application/imgui/imgui_renderer.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/renderdoc_capture_support.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/application/graphics/shader_monitor.hpp"
#include "erhe/application/window.hpp"
#include "erhe/application/windows/commands_window.hpp"
#include "erhe/application/windows/log_window.hpp"
#include "erhe/application/time.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/toolkit/verify.hpp"

namespace hextiles {

class Application_impl
{
public:
    Application_impl();
    ~Application_impl();

    auto initialize_components(Application* application, int argc, char** argv) -> bool;
    void component_initialization_complete(bool initialization_succeeded);

private:
    erhe::components::Components                 m_components;

    erhe::application::Commands                  commands;
    erhe::application::Commands_window           commands_window;
    erhe::application::Configuration             configuration;
    erhe::application::Gl_context_provider       gl_context_provider;
    erhe::application::Imgui_renderer            imgui_renderer;
    erhe::application::Imgui_windows             imgui_windows;
    erhe::application::Log_window                log_window;
    erhe::application::Renderdoc_capture_support renderdoc_capture_support;
    erhe::application::Rendergraph               rendergraph;
    erhe::application::Shader_monitor            shader_monitor;
    erhe::application::Text_renderer             text_renderer;
    erhe::application::Time                      application_time;
    erhe::application::View                      view;
    erhe::application::Window                    window;

    erhe::graphics::OpenGL_state_tracker         opengl_state_tracker;

    Game                        game                  ;
    Game_window                 game_window           ;
    Hextiles_view_client        hextiles_view_client  ;
    Map_editor                  map_editor            ;
    Map_generator               map_generator         ;
    Map_tool_window             map_tool_window       ;
    Map_window                  map_window            ;
    Menu_window                 menu_window           ;
    New_game_window             new_game_window       ;
    Rendering                   rendering             ;
    Terrain_editor_window       terrain_editor_window ;
    Terrain_group_editor_window terrain_group_editor_window;
    Terrain_palette_window      terrain_palette_window;
    Terrain_replacement_rule_editor_window terrain_replacement_rule_editor_window;
    Tiles                       tiles                 ;
    Tile_renderer               tile_renderer         ;
    Type_editor                 type_editor           ;
    Unit_editor_window          unit_editor_window    ;
};

void init_logs()
{
    erhe::log::console_init();
    erhe::log::initialize_log_sinks();
    erhe::application::initialize_logging();
    erhe::components::initialize_logging();
    gl::initialize_logging();
    //erhe::geometry::initialize_logging();
    erhe::graphics::initialize_logging();
    //erhe::physics::initialize_logging();
    //erhe::primitive::initialize_logging();
    //erhe::raytrace::initialize_logging();
    //erhe::scene::initialize_logging();
    erhe::toolkit::initialize_logging();
    erhe::ui::initialize_logging();

    hextiles::initialize_logging();
}

Application_impl::Application_impl() = default;

Application_impl::~Application_impl()
{
    m_components.cleanup_components();
}

auto Application_impl::initialize_components(
    Application* application,
    const int    argc,
    char**       argv
) -> bool
{
    erhe::application::log_startup->info("Parsing args");
    configuration.parse_args(argc, argv);

    m_components.add(application          );
    m_components.add(&application_time    );
    m_components.add(&commands            );
    m_components.add(&commands_window     );
    m_components.add(&configuration       );
    m_components.add(&gl_context_provider );
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    m_components.add(&imgui_renderer      );
    m_components.add(&imgui_windows       );
#endif
    m_components.add(&log_window          );
    m_components.add(&renderdoc_capture_support);
    m_components.add(&rendergraph         );
    m_components.add(&view                );
    m_components.add(&window              );
    m_components.add(&shader_monitor      );
    m_components.add(&text_renderer       );
    m_components.add(&opengl_state_tracker);

    m_components.add(&game                  );
    m_components.add(&game_window           );
    m_components.add(&hextiles_view_client  );
    m_components.add(&map_editor            );
    m_components.add(&map_generator         );
    m_components.add(&map_tool_window       );
    m_components.add(&map_window            );
    m_components.add(&menu_window           );
    m_components.add(&new_game_window       );
    m_components.add(&rendering             );
    m_components.add(&terrain_editor_window );
    m_components.add(&terrain_group_editor_window);
    m_components.add(&terrain_palette_window);
    m_components.add(&terrain_replacement_rule_editor_window);
    m_components.add(&tiles                 );
    m_components.add(&tile_renderer         );
    m_components.add(&type_editor           );
    m_components.add(&unit_editor_window    );

    erhe::application::log_startup->info("Initializing early components");
    configuration.initialize_component();
    //renderdoc_capture_support.initialize_component();

    erhe::application::log_startup->info("Creating window");
    if (!window.create_gl_window()) {
        erhe::application::log_startup->error("GL window creation failed, aborting");
        return false;
    }

    erhe::application::log_startup->info("Launching component initialization");
    m_components.launch_component_initialization(configuration.threading.parallel_initialization);

    if (configuration.threading.parallel_initialization) {
        erhe::application::log_startup->info("Parallel init -> Providing worker GL contexts");
        gl_context_provider.provide_worker_contexts(
            window.get_context_window(),
            [this]() -> bool {
                return !m_components.is_component_initialization_complete();
            }
        );
    }

    erhe::application::log_startup->info("Waiting for component initialization to complete");
    m_components.wait_component_initialization_complete();

    erhe::application::log_startup->info("Component initialization complete");
    component_initialization_complete(true);

    gl::clip_control(gl::Clip_control_origin::lower_left, gl::Clip_control_depth::zero_to_one);
    gl::disable     (gl::Enable_cap::primitive_restart);
    gl::enable      (gl::Enable_cap::primitive_restart_fixed_index);
    gl::enable      (gl::Enable_cap::texture_cube_map_seamless);
    gl::enable      (gl::Enable_cap::framebuffer_srgb);

    opengl_state_tracker.on_thread_enter();

    return true;
}

void Application_impl::component_initialization_complete(const bool initialization_succeeded)
{
    if (initialization_succeeded) {
        gl::enable(gl::Enable_cap::primitive_restart);
        gl::primitive_restart_index(0xffffu);

        if (erhe::application::g_window == nullptr) {
            return;
        }

        auto* const context_window = window.get_context_window();
        if (context_window == nullptr) {
            return;
        }

        auto& root_view = context_window->get_root_view();

        root_view.reset_view(erhe::application::g_view);
    }
}

Application* g_application{nullptr};

Application::Application()
    : erhe::components::Component{c_type_name}
{
    init_logs();
    erhe::application::log_startup->info("Logs have been initialized");
    m_impl = new Application_impl;
}

Application::~Application() noexcept
{
    ERHE_VERIFY(g_application == this);
    delete m_impl;
    g_application = this;
}

void Application::initialize_component()
{
    ERHE_VERIFY(g_application == nullptr);
    g_application = this;
}

auto Application::initialize_components(const int argc, char** argv) -> bool
{
    return m_impl->initialize_components(this, argc, argv);
}

void Application::run()
{
    erhe::application::g_view->on_enter();
    erhe::application::g_view->run();
}

} // namespace hextiles
