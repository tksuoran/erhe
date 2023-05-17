#include "editor_message_bus.hpp"

#include "editor_application.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "editor_view_client.hpp"
#include "tools/brushes/brush_tool.hpp"
#include "tools/brushes/create/create.hpp"
#include "graphics/icon_set.hpp"
#include "graphics/image_transfer.hpp"
#include "operations/operation_stack.hpp"
#include "rendertarget_mesh.hpp"
#include "rendertarget_imgui_viewport.hpp"

#include "rendergraph/shadow_render_node.hpp"

#include "renderers/forward_renderer.hpp"
#include "renderers/programs.hpp"
#include "renderers/program_interface.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "xr/hand_tracker.hpp"
#   include "xr/theremin.hpp"
#endif
#include "renderers/id_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/program_interface.hpp"
#include "renderers/programs.hpp"
#include "renderers/shadow_renderer.hpp"
#include "rendergraph/post_processing.hpp"
#include "scene/material_preview.hpp"

#include "tools/debug_visualizations.hpp"
#include "tools/fly_camera_tool.hpp"
#include "tools/grid_tool.hpp"
#include "tools/hotbar.hpp"
#include "tools/hover_tool.hpp"
#include "tools/material_paint_tool.hpp"
#include "tools/hud.hpp"
#include "tools/paint_tool.hpp"
#include "tools/physics_tool.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "tools/transform/move_tool.hpp"
#include "tools/transform/rotate_tool.hpp"
#include "tools/transform/scale_tool.hpp"
#include "tools/transform/transform_tool.hpp"

#include "windows/brdf_slice_window.hpp"
#include "windows/debug_view_window.hpp"
#include "windows/imgui_viewport_window.hpp"
#include "windows/layers_window.hpp"
#include "windows/content_library_window.hpp"
#include "windows/node_tree_window.hpp"
#include "windows/operations.hpp"
#include "windows/post_processing_window.hpp"
#include "windows/physics_window.hpp"
#include "windows/properties.hpp"
#include "windows/rendergraph_window.hpp"
#include "windows/settings.hpp"
#include "windows/tool_properties_window.hpp"
#include "windows/viewport_config_window.hpp"

#include "scene/debug_draw.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"

#include "erhe/application/application_log.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/graphics/shader_monitor.hpp"
#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include "erhe/application/imgui/imgui_renderer.hpp"
#endif
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/renderdoc_capture_support.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/time.hpp"
#include "erhe/application/window.hpp"
#include "erhe/application/windows/commands_window.hpp"
#include "erhe/application/windows/log_window.hpp"
#include "erhe/application/windows/performance_window.hpp"
#include "erhe/application/windows/pipelines.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/scene_message_bus.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/window.hpp"
#include "erhe/toolkit/verify.hpp"

#include "editor_log.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/components/components_log.hpp"
#include "erhe/geometry/geometry_log.hpp"
#include "erhe/gl/gl_log.hpp"
#include "erhe/graphics/graphics_log.hpp"
#include "erhe/net/net_log.hpp"
#include "erhe/log/log.hpp"
#include "erhe/physics/physics_log.hpp"
#include "erhe/primitive/primitive_log.hpp"
#include "erhe/raytrace/raytrace_log.hpp"
#include "erhe/scene/scene_log.hpp"
#include "erhe/toolkit/toolkit_log.hpp"
#include "erhe/ui/ui_log.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#include "erhe/xr/xr_log.hpp"
#endif

namespace editor {

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
    erhe::application::Line_renderer_set         line_renderer_set;
    erhe::application::Log_window                log_window;
    erhe::application::Performance_window        performance_window;
    erhe::application::Pipelines                 pipelines;
    erhe::application::Renderdoc_capture_support renderdoc_capture_support;
    erhe::application::Rendergraph               rendergraph;
    erhe::application::Shader_monitor            shader_monitor;
    erhe::application::Text_renderer             text_renderer;
    erhe::application::Time                      application_time;
    erhe::application::View                      view;
    erhe::application::Window                    window;

    erhe::graphics::OpenGL_state_tracker         opengl_state_tracker;
    erhe::scene::Scene_message_bus               scene_message_bus;

    editor::Brdf_slice_window      brdf_slice_window     ;
    editor::Brush_tool             brush_tool            ;
    editor::Content_library_window content_library_window;
    editor::Create                 create                ;
    editor::Debug_draw             debug_draw            ;
    editor::Debug_view_window      debug_view_window     ;
    editor::Debug_visualizations   debug_visualizations  ;
    editor::Editor_message_bus     editor_message_bus    ;
    editor::Editor_rendering       editor_rendering      ;
    editor::Editor_scenes          editor_scenes         ;
    editor::Editor_view_client     editor_view_client    ;
    editor::Fly_camera_tool        fly_camera_tool       ;
    editor::Forward_renderer       forward_renderer      ;
    editor::Grid_tool              grid_tool             ;
    editor::Hotbar                 hotbar                ;
    editor::Hover_tool             hover_tool            ;
    editor::Hud                    hud                   ;
    editor::Icon_set               icon_set              ;
    editor::Id_renderer            id_renderer           ;
    editor::Image_transfer         image_transfer        ;
    editor::Layers_window          layers_window         ;
    editor::Material_paint_tool    material_paint_tool   ;
    editor::Material_preview       material_preview      ;
    editor::Mesh_memory            mesh_memory           ;
    editor::Move_tool              move_tool             ;
    editor::Node_tree_window       node_tree_window      ;
    editor::Operation_stack        operation_stack       ;
    editor::Operations             operations            ;
    editor::Paint_tool             paint_tool            ;
    editor::Physics_tool           physics_tool          ;
    editor::Physics_window         physics_window        ;
    editor::Post_processing        post_processing       ;
    editor::Post_processing_window post_processing_window;
    editor::Program_interface      program_interface     ;
    editor::Programs               programs              ;
    editor::Properties             properties            ;
    editor::Rendergraph_window     rendergraph_window    ;
    editor::Rotate_tool            rotate_tool           ;
    editor::Scale_tool             scale_tool            ;
    editor::Scene_builder          scene_builder         ;
    editor::Scene_commands         scene_commands        ;
    editor::Selection_tool         selection_tool        ;
    editor::Settings_window        settings_window       ;
    editor::Shadow_renderer        shadow_renderer       ;
    editor::Tool_properties_window tool_properties_window;
    editor::Tools                  tools                 ;
    editor::Transform_tool         transform_tool        ;
    editor::Viewport_config_window viewport_config_window;
    editor::Viewport_windows       viewport_windows      ;

#if defined(ERHE_XR_LIBRARY_OPENXR)
    editor::Hand_tracker hand_tracker;
    editor::Headset_view headset_view;
    editor::Theremin     theremin    ;
#endif
};

using erhe::graphics::OpenGL_state_tracker;
using std::shared_ptr;
using std::make_shared;

void init_logs()
{
    //erhe::log::console_init();
    erhe::log::initialize_log_sinks();
    erhe::application::initialize_logging();
    erhe::components::initialize_logging();
    gl::initialize_logging();
    erhe::geometry::initialize_logging();
    erhe::graphics::initialize_logging();
    erhe::net::initialize_logging();
    erhe::physics::initialize_logging();
    erhe::primitive::initialize_logging();
    erhe::raytrace::initialize_logging();
    erhe::scene::initialize_logging();
    erhe::toolkit::initialize_logging();
    erhe::ui::initialize_logging();
#if defined(ERHE_XR_LIBRARY_OPENXR)
    erhe::xr::initialize_logging();
#endif

    editor::initialize_logging();
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
    ERHE_PROFILE_FUNCTION();

    erhe::application::log_startup->info("Parsing args");
    configuration.parse_args(argc, argv);
    {
        ERHE_PROFILE_SCOPE("add components");

        erhe::application::log_startup->info("Adding components");
        m_components.add(application               );
        m_components.add(&configuration            );
        m_components.add(&renderdoc_capture_support);
        m_components.add(&window                   );
        m_components.add(&gl_context_provider      );
        m_components.add(&commands                 );
        m_components.add(&commands_window   );
        m_components.add(&imgui_windows     );
        m_components.add(&application_time  );
        m_components.add(&view              );
        m_components.add(&log_window        );
#if defined(ERHE_GUI_LIBRARY_IMGUI)
        m_components.add(&imgui_renderer    );
#endif
        m_components.add(&performance_window);
        m_components.add(&pipelines         );
        m_components.add(&rendergraph       );
        m_components.add(&shader_monitor    );
        m_components.add(&text_renderer     );
        m_components.add(&line_renderer_set );
        m_components.add(&opengl_state_tracker);

        m_components.add(&brdf_slice_window     );
        m_components.add(&brush_tool            );
        m_components.add(&content_library_window);
        m_components.add(&create                );
        m_components.add(&debug_draw            );
        m_components.add(&debug_view_window     );
        m_components.add(&debug_visualizations  );
        m_components.add(&editor_message_bus    );
        m_components.add(&editor_rendering      );
        m_components.add(&editor_scenes         );
        m_components.add(&editor_view_client    );
        m_components.add(&fly_camera_tool       );
        m_components.add(&forward_renderer      );
        m_components.add(&grid_tool             );
        m_components.add(&hotbar                );
        m_components.add(&hover_tool            );
        m_components.add(&hud                   );
        m_components.add(&icon_set              );
        m_components.add(&id_renderer           );
        m_components.add(&image_transfer        );
        m_components.add(&layers_window         );
        m_components.add(&material_paint_tool   );
        m_components.add(&material_preview      );
        m_components.add(&mesh_memory           );
        m_components.add(&move_tool             );
        m_components.add(&node_tree_window      );
        m_components.add(&operation_stack       );
        m_components.add(&operations            );
        m_components.add(&paint_tool            );
        m_components.add(&physics_tool          );
        m_components.add(&physics_window        );
        m_components.add(&post_processing       );
        m_components.add(&post_processing_window);
        m_components.add(&program_interface     );
        m_components.add(&programs              );
        m_components.add(&properties            );
        m_components.add(&rendergraph_window    );
        m_components.add(&rotate_tool           );
        m_components.add(&scale_tool            );
        m_components.add(&scene_builder         );
        m_components.add(&scene_commands        );
        m_components.add(&scene_message_bus     );
        m_components.add(&selection_tool        );
        m_components.add(&settings_window       );
        m_components.add(&shadow_renderer       );
        m_components.add(&tool_properties_window);
        m_components.add(&tools                 );
        m_components.add(&transform_tool        );
        m_components.add(&viewport_config_window);
        m_components.add(&viewport_windows      );

#if defined(ERHE_XR_LIBRARY_OPENXR)
        m_components.add(&hand_tracker);
        m_components.add(&headset_view);
        m_components.add(&theremin    );
#endif
    }

    erhe::application::log_startup->info("Initializing early components");
    configuration.initialize_component();
    renderdoc_capture_support.initialize_component();

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
            [this]() -> bool
            {
                return !m_components.is_component_initialization_complete();
            }
        );
    }

    erhe::application::log_startup->info("Waiting for component initialization to complete");
    m_components.wait_component_initialization_complete();

    erhe::application::log_startup->info("Component initialization complete");
    component_initialization_complete(true);

    if (
        g_physics_window->config.static_enable &&
        g_physics_window->config.dynamic_enable
    ) {
        const auto& test_scene_root = scene_builder.get_scene_root();
        test_scene_root->physics_world().enable_physics_updates();
    }

    tools.set_priority_tool(&physics_tool);

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

} // namespace editor

