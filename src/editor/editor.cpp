#include "editor.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "editor_settings.hpp"
#include "input_state.hpp"
#include "time.hpp"

#include "graphics/image_transfer.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "rendergraph/post_processing.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "rendertarget_imgui_viewport.hpp"
#include "rendertarget_mesh.hpp"
#include "scene/debug_draw.hpp"
#include "scene/material_preview.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/tools.hpp"
#include "tools/brushes/brush.hpp"
#include "windows/animation_window.hpp"
#include "windows/brdf_slice_window.hpp"
#include "windows/commands_window.hpp"
#include "windows/content_library_window.hpp"
#include "windows/debug_view_window.hpp"
#include "windows/imgui_viewport_window.hpp"
#include "windows/layers_window.hpp"
#include "windows/network_window.hpp"
#include "windows/node_tree_window.hpp"
#include "windows/operations.hpp"
#include "windows/physics_window.hpp"
#include "windows/post_processing_window.hpp"
#include "windows/properties.hpp"
#include "windows/rendergraph_window.hpp"
#include "windows/settings.hpp"
#include "windows/tool_properties_window.hpp"
#include "windows/viewport_config_window.hpp"

#include "xr/headset_view.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/hand_tracker.hpp"
#   include "xr/theremin.hpp"
#   include "erhe/xr/xr_log.hpp"
#endif

#include "erhe/commands/commands.hpp"
#include "erhe/commands/commands_log.hpp"
#include "erhe/configuration/configuration.hpp"
#include "erhe/gl/enum_bit_mask_operators.hpp"
#include "erhe/gl/gl_log.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/gltf/gltf_log.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/graphics/graphics_log.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/renderbuffer.hpp"
#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include "erhe/imgui/imgui_log.hpp"
#   include "erhe/imgui/imgui_renderer.hpp"
#   include "erhe/imgui/imgui_windows.hpp"
#   include "erhe/imgui/window_imgui_viewport.hpp"
#   include "erhe/imgui/windows/log_window.hpp"
#   include "erhe/imgui/windows/performance_window.hpp"
#   include "erhe/imgui/windows/pipelines.hpp"
#endif
#include "erhe/log/log.hpp"
#include "erhe/net/net_log.hpp"
#include "erhe/physics/physics_log.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/raytrace/raytrace_log.hpp"
#include "erhe/renderer/pipeline_renderpass.hpp"
#include "erhe/renderer/renderer_log.hpp"
#include "erhe/renderer/text_renderer.hpp"
#include "erhe/rendergraph/rendergraph.hpp"
#include "erhe/rendergraph/rendergraph_log.hpp"
#include "erhe/scene/scene_log.hpp"
#include "erhe/scene_renderer/scene_renderer_log.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/renderdoc_capture.hpp"
#include "erhe/toolkit/toolkit_log.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/window.hpp"
#include "erhe/toolkit/window.hpp"
#include "erhe/toolkit/window_event_handler.hpp"
#include "erhe/ui/ui_log.hpp"

namespace editor {

class Editor
    : public erhe::toolkit::Window_event_handler
{
public:

    [[nodiscard]] auto create_window() -> erhe::toolkit::Context_window
    {
        {
            auto ini = erhe::configuration::get_ini("erhe.ini", "headset");
            ini->get("openxr", m_openxr);
        }

        erhe::toolkit::Window_configuration configuration{
            .gl_major          = 4,
            .gl_minor          = 6,
            .width             = 1920,
            .height            = 1080,
            .msaa_sample_count = 0,
            .title             = erhe::toolkit::format_window_title("erhe editor by Timo Suoranta")
        };

        bool show             = true;
        bool fullscreen       = false;
        bool use_transparency = false;
        auto ini = erhe::configuration::get_ini("erhe.ini", "window");
        ini->get("show",              show);
        ini->get("fullscreen",        fullscreen);
        ini->get("fuse_transparency", use_transparency);
        ini->get("gl_major",          configuration.gl_major);
        ini->get("gl_minor",          configuration.gl_minor);
        ini->get("width",             configuration.width);
        ini->get("height",            configuration.height);
        ini->get("swap_interval",     configuration.swap_interval);

        return erhe::toolkit::Context_window{configuration};
    }

    Editor()
        : m_editor_settings   {}
        , m_commands          {}
        , m_scene_message_bus {}
        , m_editor_message_bus{}
        , m_input_state       {}
        , m_time              {}
        , m_editor_context    {}

        , m_context_window{create_window()}
        , m_graphics_instance     {m_context_window}
        , m_imgui_renderer        {m_graphics_instance}
        , m_image_transfer        {m_graphics_instance}
        , m_line_renderer_set     {m_graphics_instance}
        , m_program_interface     {m_graphics_instance}
        , m_rendergraph           {m_graphics_instance}
        , m_text_renderer         {m_graphics_instance}

        , m_programs              {m_graphics_instance, m_program_interface}
        , m_forward_renderer      {m_graphics_instance, m_program_interface}
        , m_shadow_renderer       {m_graphics_instance, m_program_interface}
        , m_mesh_memory           {m_graphics_instance, m_program_interface}

        , m_imgui_windows         {m_imgui_renderer,    &m_context_window,   m_rendergraph}
        , m_editor_scenes         {m_editor_context,    m_time}
        , m_content_library_window{m_imgui_renderer,    m_imgui_windows,     m_editor_scenes}
        , m_icon_set              {m_graphics_instance, m_imgui_renderer,    m_programs}
        , m_post_processing       {m_graphics_instance, m_editor_context,    m_programs}
        , m_id_renderer           {m_graphics_instance, m_program_interface, m_mesh_memory,     m_programs}
        , m_settings_window       {m_imgui_renderer,    m_imgui_windows,     m_shadow_renderer, m_editor_context}
        , m_viewport_windows      {m_commands,          m_editor_context,    m_editor_message_bus}
        , m_editor_rendering      {m_commands,          m_graphics_instance, m_editor_context, m_editor_message_bus, m_mesh_memory, m_programs}
        , m_selection             {m_commands,          m_editor_context,    m_editor_message_bus}
        , m_operation_stack       {m_commands,          m_imgui_renderer,    m_imgui_windows, m_editor_context}
        , m_scene_commands        {m_commands,          m_editor_context}
        , m_animation_window      {m_imgui_renderer, m_imgui_windows, m_editor_context, m_editor_message_bus}
        , m_commands_window       {m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_layers_window         {m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_network_window        {m_imgui_renderer, m_imgui_windows, m_editor_context, m_time}
        , m_node_tree_window      {m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_operations            {m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_physics_window        {m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_post_processing_window{m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_properties            {m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_rendergraph_window    {m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_tool_properties_window{m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_viewport_config_window{m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_log_window            {m_commands, m_imgui_renderer, m_imgui_windows}
        , m_performance_window    {m_imgui_renderer, m_imgui_windows}
        , m_pipelines             {m_imgui_renderer, m_imgui_windows}

        , m_tools{
            m_graphics_instance, m_scene_message_bus, m_editor_context, m_editor_rendering,
            m_editor_scenes,     m_mesh_memory,       m_programs
        }
        , m_scene_builder{
            m_graphics_instance,
            m_imgui_renderer,
            m_imgui_windows,
            m_rendergraph,
            m_scene_message_bus,
            m_shadow_renderer,
            m_editor_context,
            m_editor_rendering,
            m_editor_scenes,
            m_mesh_memory,
            m_settings_window,
            m_tools,
            m_viewport_config_window,
            m_viewport_windows
        }
        , m_headset_view{
            m_graphics_instance,
            m_rendergraph,
            m_shadow_renderer,
            m_context_window,
            m_editor_context,
            m_editor_rendering,
            m_mesh_memory,
            m_scene_builder
        }
#if defined(ERHE_XR_LIBRARY_OPENXR)
        , m_hand_tracker{m_editor_context, m_editor_rendering}
#endif
        , m_move_tool             {m_editor_context,    m_icon_set}
        , m_rotate_tool           {m_editor_context,    m_icon_set}
        , m_scale_tool            {m_editor_context,    m_icon_set}
        , m_transform_tool{
            m_commands,           m_imgui_renderer,  m_imgui_windows, m_editor_context,
            m_editor_message_bus, m_headset_view,    m_mesh_memory,   m_tools
        }
        , m_hud{
            m_commands,       m_graphics_instance,  m_imgui_renderer, m_imgui_windows, m_rendergraph,
            m_editor_context, m_editor_message_bus, m_headset_view,   m_mesh_memory,   m_scene_builder,
            m_tools
        }
        , m_hotbar{
            m_commands,    m_graphics_instance, m_imgui_renderer,     m_imgui_windows,
            m_rendergraph, m_editor_context,    m_editor_message_bus, m_headset_view,
            m_icon_set,    m_mesh_memory,       m_scene_builder,      m_tools
        }
        , m_hover_tool       {m_imgui_renderer, m_imgui_windows, m_editor_context, m_editor_message_bus, m_tools}

        , m_brdf_slice_window{
            m_imgui_renderer,   m_imgui_windows,          m_rendergraph,
            m_forward_renderer, m_content_library_window, m_programs
        }
        , m_debug_draw            {m_editor_context}
        , m_debug_view_window     {
            m_imgui_renderer, m_imgui_windows,    m_rendergraph, m_forward_renderer,
            m_editor_context, m_editor_rendering, m_mesh_memory, m_programs
        }
        , m_debug_visualizations  {m_imgui_renderer, m_imgui_windows, m_editor_context, m_editor_message_bus, m_editor_rendering}
        , m_material_preview      {m_graphics_instance, m_scene_message_bus, m_editor_context, m_mesh_memory, m_programs}

#if defined(ERHE_XR_LIBRARY_OPENXR)
        , m_theremin              {m_imgui_renderer, m_imgui_windows,  m_hand_tracker,       m_editor_context}
#endif
        , m_brush_tool            {m_commands,       m_editor_context, m_editor_message_bus, m_headset_view, m_icon_set, m_tools}
        , m_create                {m_imgui_renderer, m_imgui_windows,  m_editor_context,     m_tools}
        , m_fly_camera_tool       {m_commands,       m_imgui_renderer, m_imgui_windows,      m_editor_context, m_editor_message_bus, m_time, m_tools}
        , m_grid_tool             {m_imgui_renderer, m_imgui_windows,  m_editor_context,     m_icon_set, m_tools}
        , m_material_paint_tool   {m_commands,       m_editor_context, m_headset_view,       m_icon_set, m_tools}
        , m_paint_tool{
            m_commands,           m_imgui_renderer, m_imgui_windows, m_editor_context,
            m_editor_message_bus, m_headset_view,   m_icon_set,      m_tools
        }
        , m_physics_tool  {m_commands,       m_editor_context, m_editor_message_bus, m_headset_view, m_icon_set, m_tools}
        , m_selection_tool{m_editor_context, m_icon_set, m_tools}
    {
        ERHE_PROFILE_GPU_CONTEXT

        fill_editor_context();

        m_hotbar.get_all_tools();

        gl::clip_control(gl::Clip_control_origin::lower_left, gl::Clip_control_depth::zero_to_one);
        gl::enable      (gl::Enable_cap::framebuffer_srgb);

        auto& root_event_handler = m_context_window.get_root_window_event_handler();
        root_event_handler.attach(this, 3);
#if defined(ERHE_XR_LIBRARY_OPENXR)
        if (m_headset_view.config.openxr)
        {
            // TODO Create windows directly to correct viewport?
            // Move all imgui windows that have window viewport to hud viewport
            const auto viewport        = m_hud.get_rendertarget_imgui_viewport();
            const auto window_viewport = m_imgui_windows.get_window_viewport();
            if (viewport) {
                auto& windows = m_imgui_windows.get_windows();
                for (auto window : windows) {
                    if (window->get_viewport() == window_viewport.get()) {
                        window->set_viewport(viewport.get());
                    }
                }
            }
        }
        else
#endif
        {
            root_event_handler.attach(&m_input_state, 4);
            root_event_handler.attach(&m_imgui_windows, 2);
            root_event_handler.attach(&m_commands, 1);
        }

        if (
            m_editor_settings.physics_static_enable &&
            m_editor_settings.physics_dynamic_enable
        ) {
            const auto& test_scene_root = m_scene_builder.get_scene_root();
            test_scene_root->physics_world().enable_physics_updates();
        }

        m_tools.set_priority_tool(&m_physics_tool);
    }

    void fill_editor_context()
    {
        m_editor_context.commands               = &m_commands              ;
        m_editor_context.graphics_instance      = &m_graphics_instance     ;
        m_editor_context.imgui_renderer         = &m_imgui_renderer        ;
        m_editor_context.imgui_windows          = &m_imgui_windows         ;
        m_editor_context.line_renderer_set      = &m_line_renderer_set     ;
        m_editor_context.text_renderer          = &m_text_renderer         ;
        m_editor_context.rendergraph            = &m_rendergraph           ;
        m_editor_context.scene_message_bus      = &m_scene_message_bus     ;
        m_editor_context.forward_renderer       = &m_forward_renderer      ;
        m_editor_context.shadow_renderer        = &m_shadow_renderer       ;
        m_editor_context.context_window         = &m_context_window        ;
        m_editor_context.brush_tool             = &m_brush_tool            ;
        m_editor_context.content_library_window = &m_content_library_window;
        m_editor_context.create                 = &m_create                ;
        m_editor_context.editor_message_bus     = &m_editor_message_bus    ;
        m_editor_context.editor_rendering       = &m_editor_rendering      ;
        m_editor_context.editor_scenes          = &m_editor_scenes         ;
        m_editor_context.editor_settings        = &m_editor_settings       ;
        m_editor_context.fly_camera_tool        = &m_fly_camera_tool       ;
        m_editor_context.grid_tool              = &m_grid_tool             ;
#if defined(ERHE_XR_LIBRARY_OPENXR)
        m_editor_context.hand_tracker           = &m_hand_tracker          ;
#endif
        m_editor_context.headset_view           = &m_headset_view          ;
        m_editor_context.hotbar                 = &m_hotbar                ;
        m_editor_context.hud                    = &m_hud                   ;
        m_editor_context.icon_set               = &m_icon_set              ;
        m_editor_context.id_renderer            = &m_id_renderer           ;
        m_editor_context.input_state            = &m_input_state           ;
        m_editor_context.material_paint_tool    = &m_material_paint_tool   ;
        m_editor_context.material_preview       = &m_material_preview      ;
        m_editor_context.mesh_memory            = &m_mesh_memory           ;
        m_editor_context.move_tool              = &m_move_tool             ;
        m_editor_context.operation_stack        = &m_operation_stack       ;
        m_editor_context.paint_tool             = &m_paint_tool            ;
        m_editor_context.physics_tool           = &m_physics_tool          ;
        m_editor_context.post_processing        = &m_post_processing       ;
        m_editor_context.programs               = &m_programs              ;
        m_editor_context.rotate_tool            = &m_rotate_tool           ;
        m_editor_context.scale_tool             = &m_scale_tool            ;
        m_editor_context.scene_builder          = &m_scene_builder         ;
        m_editor_context.scene_commands         = &m_scene_commands        ;
        m_editor_context.selection              = &m_selection             ;
        m_editor_context.selection_tool         = &m_selection_tool        ;
        m_editor_context.settings_window        = &m_settings_window       ;
        m_editor_context.tools                  = &m_tools                 ;
        m_editor_context.transform_tool         = &m_transform_tool        ;
        m_editor_context.viewport_config_window = &m_viewport_config_window;
        m_editor_context.viewport_windows       = &m_viewport_windows      ;
    }

    void run()
    {
        while (!m_close_requested) {
            m_context_window.poll_events();
            on_idle();
        }
    }

    auto on_idle() -> bool override
    {
        m_graphics_instance.shader_monitor.update_once_per_frame();
        m_mesh_memory.gl_buffer_transfer_queue.flush();
        m_time.update();
        m_editor_rendering.begin_frame();
        m_imgui_windows.imgui_windows();
        m_rendergraph.execute();
        m_imgui_renderer.next_frame();
        m_editor_rendering.end_frame();
        m_commands.on_idle();
        if (!m_openxr) {
            m_context_window.swap_buffers();
        }

        ERHE_PROFILE_FRAME_END

        return true;
    }

    auto on_close() -> bool override
    {
        m_close_requested = true;
        return true;
    }

    // No constructors
    Editor_settings                m_editor_settings;
    erhe::commands::Commands       m_commands;
    erhe::scene::Scene_message_bus m_scene_message_bus;
    Editor_message_bus             m_editor_message_bus;
    Input_state                    m_input_state;
    Time                           m_time;
    Editor_context                 m_editor_context;

    erhe::toolkit::Context_window           m_context_window;
    erhe::graphics::Instance                m_graphics_instance;
    erhe::imgui::Imgui_renderer             m_imgui_renderer;
    Image_transfer                          m_image_transfer;
    erhe::renderer::Line_renderer_set       m_line_renderer_set;
    erhe::scene_renderer::Program_interface m_program_interface;
    erhe::rendergraph::Rendergraph          m_rendergraph;
    erhe::renderer::Text_renderer           m_text_renderer;

    Programs                                m_programs;
    erhe::scene_renderer::Forward_renderer  m_forward_renderer;
    erhe::scene_renderer::Shadow_renderer   m_shadow_renderer;
    Mesh_memory                             m_mesh_memory;

    erhe::imgui::Imgui_windows              m_imgui_windows;
    Editor_scenes                           m_editor_scenes;

    Content_library_window                  m_content_library_window;
    Icon_set                                m_icon_set;
    Post_processing                         m_post_processing;
    Id_renderer                             m_id_renderer;
    Settings_window                         m_settings_window;
    Viewport_windows                        m_viewport_windows;
    Editor_rendering                        m_editor_rendering;
    Selection                               m_selection;
    Operation_stack                         m_operation_stack;
    Scene_commands                          m_scene_commands;
    Animation_window                        m_animation_window;
    Commands_window                         m_commands_window;
    Layers_window                           m_layers_window;
    Network_window                          m_network_window;
    Node_tree_window                        m_node_tree_window;
    Operations                              m_operations;
    Physics_window                          m_physics_window;
    Post_processing_window                  m_post_processing_window;
    Properties                              m_properties;
    Rendergraph_window                      m_rendergraph_window;
    Tool_properties_window                  m_tool_properties_window;
    Viewport_config_window                  m_viewport_config_window;
    erhe::imgui::Log_window                 m_log_window;
    erhe::imgui::Performance_window         m_performance_window;
    erhe::imgui::Pipelines                  m_pipelines;

    Tools                                   m_tools;
    Scene_builder                           m_scene_builder;
    Headset_view                            m_headset_view;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    Hand_tracker                            m_hand_tracker;
#endif
    Move_tool                               m_move_tool;
    Rotate_tool                             m_rotate_tool;
    Scale_tool                              m_scale_tool;
    Transform_tool                          m_transform_tool;
    Hud                                     m_hud;
    Hotbar                                  m_hotbar;
    Hover_tool                              m_hover_tool;
    Brdf_slice_window                       m_brdf_slice_window;
    Debug_draw                              m_debug_draw;
    Debug_view_window                       m_debug_view_window;
    Debug_visualizations                    m_debug_visualizations;
    Material_preview                        m_material_preview;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    Theremin                                m_theremin;
#endif

    Brush_tool                              m_brush_tool;
    Create                                  m_create;
    Fly_camera_tool                         m_fly_camera_tool;
    Grid_tool                               m_grid_tool;
    Material_paint_tool                     m_material_paint_tool;
    Paint_tool                              m_paint_tool;
    Physics_tool                            m_physics_tool;
    Selection_tool                          m_selection_tool;

    bool m_close_requested{false};
    bool m_openxr         {false};
};

void run_editor()
{
    erhe::log::initialize_log_sinks();
    gl::initialize_logging();
    erhe::commands::initialize_logging();
    erhe::gltf::initialize_logging();
    erhe::geometry::initialize_logging();
    erhe::graphics::initialize_logging();
    erhe::imgui::initialize_logging();
    erhe::net::initialize_logging();
    erhe::physics::initialize_logging();
    erhe::primitive::initialize_logging();
    erhe::raytrace::initialize_logging();
    erhe::renderer::initialize_logging();
    erhe::rendergraph::initialize_logging();
    erhe::scene::initialize_logging();
    erhe::scene_renderer::initialize_logging();
    erhe::toolkit::initialize_logging();
    erhe::ui::initialize_logging();
#if defined(ERHE_XR_LIBRARY_OPENXR)
    erhe::xr::initialize_logging();
#endif
    editor::initialize_logging();

    bool enable_renderdoc_capture_support{false};
    auto ini = erhe::configuration::get_ini("erhe.ini", "renderdoc");
    ini->get("capture_support", enable_renderdoc_capture_support);
    if (enable_renderdoc_capture_support) {
        erhe::toolkit::initialize_frame_capture();
    }

    Editor editor{};
    editor.run();
}

} // namespace editor
