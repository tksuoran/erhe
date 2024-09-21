#include "editor.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "editor_settings.hpp"
#include "editor_windows.hpp"
#include "input_state.hpp"
#include "time.hpp"

#include "graphics/icon_set.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "rendergraph/post_processing.hpp"
#include "rendertarget_imgui_host.hpp"
#include "scene/asset_browser.hpp"
#include "scene/debug_draw.hpp"
#include "scene/material_preview.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_commands.hpp"
#include "scene/viewport_scene_views.hpp"
#include "tools/brushes/brush.hpp"
#include "tools/clipboard.hpp"
#include "tools/tools.hpp"
#include "tools/transform/move_tool.hpp"
#include "tools/transform/rotate_tool.hpp"
#include "tools/transform/scale_tool.hpp"
#include "windows/brdf_slice.hpp"
#include "windows/clipboard_window.hpp"
#include "windows/commands_window.hpp"
#include "windows/composer_window.hpp"
#include "windows/debug_view_window.hpp"
#include "windows/layers_window.hpp"
#include "windows/network_window.hpp"
#include "windows/operations.hpp"
#include "windows/physics_window.hpp"
#include "windows/post_processing_window.hpp"
#include "windows/properties.hpp"
#include "windows/rendergraph_window.hpp"
#include "windows/selection_window.hpp"
#include "windows/settings_window.hpp"
#include "windows/tool_properties_window.hpp"
#include "windows/viewport_config_window.hpp"

#include "xr/headset_view.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/hand_tracker.hpp"
//#   include "xr/theremin.hpp"
#   include "erhe_xr/xr_log.hpp"
#endif

#include "erhe_commands/commands.hpp"
#include "erhe_commands/commands_log.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_file/file_log.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_gl/gl_log.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_gltf/gltf_log.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/renderbuffer.hpp"
#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include "erhe_imgui/imgui_log.hpp"
#   include "erhe_imgui/imgui_renderer.hpp"
#   include "erhe_imgui/imgui_windows.hpp"
#   include "erhe_imgui/window_imgui_host.hpp"
#   include "erhe_imgui/windows/log_window.hpp"
#   include "erhe_imgui/windows/performance_window.hpp"
#   include "erhe_imgui/windows/pipelines.hpp"
#endif
#include "erhe_item/item_log.hpp"
#include "erhe_log/log.hpp"
#include "erhe_math/math_log.hpp"
#include "erhe_net/net_log.hpp"
#include "erhe_physics/physics_log.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_renderer/line_renderer.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_renderer/text_renderer.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_time/sleep.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_window/renderdoc_capture.hpp"
#include "erhe_window/window_log.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_event_handler.hpp"
#include "erhe_ui/ui_log.hpp"

#if defined(ERHE_PROFILE_LIBRARY_NVTX)
#   include <nvtx3/nvToolsExt.h>
#endif

namespace editor {

class Editor : public erhe::window::Input_event_handler
{
public:
    void tick()
    {
        ERHE_PROFILE_FUNCTION();

        std::vector<erhe::window::Input_event>& input_events = m_context_window.get_input_events();

        std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();

        m_fly_camera_tool.on_frame_begin();

        // Apply logic updates
        if (!m_editor_context.OpenXR) { //if (!m_headset_view.is_active()) {
            auto* imgui_host = m_imgui_windows.get_window_imgui_host().get(); // get glfw window hosted viewport
            if (imgui_host != nullptr) {
                m_viewport_scene_views.update_hover(imgui_host); // updates what viewport window is being hovered
            }
        }
#if defined(ERHE_XR_LIBRARY_OPENXR)
        // - updates cameras
        // - updates pointer context for headset scene view from controller
        // - sends XR input events to Commands
        // - updates controller visualization nodes
        if (m_editor_context.OpenXR) {
            ERHE_PROFILE_SCOPE("OpenXR update events");
            bool headset_update_ok = m_headset_view.update_events();
            if (headset_update_ok && m_headset_view.is_active()) {
                m_viewport_config_window.set_edit_data(&m_headset_view.get_config());
            } else{
                ERHE_PROFILE_SCOPE("OpenXR sleep");
                // Throttle loop since xrWaitFrame won't be called.
                std::this_thread::sleep_for(std::chrono::milliseconds{250});
            }
        }
#endif

        // - Update all ImGui hosts. glfw window host processes input events, converting them to ImGui inputs events
        //   This may consume some input events, so that they will not get processed by m_commands.tick() below
        // - Call all ImGui code (Imgui_window)
        m_imgui_windows.imgui_windows();

        // - Apply all command bindings (OpenXR bindings were already executed above)
        m_commands.tick(timestamp, input_events);

        m_operation_stack.update();

        m_editor_scenes.before_physics_simulation_steps();

        // - Execute all fixes step updates
        // - Execute all once per frame updates
        //    - Editor_scenes (updates physics)
        //    - Fly_camera_tool
        //    - Network_window 
        m_time.update();
        m_editor_message_bus.update(); // Flushes queued messages

        // Apply physics updates
        m_editor_scenes.after_physics_simulation_steps();

        m_fly_camera_tool.on_frame_end();

        // Rendering
        m_graphics_instance.shader_monitor.update_once_per_frame();
        m_mesh_memory.gl_buffer_transfer_queue.flush();

        m_editor_rendering.begin_frame(); // tests renderdoc capture start

        // Execute rendergraph
        m_rendergraph.execute();

        m_imgui_renderer.next_frame();
        m_editor_rendering.end_frame();
        if (!m_editor_context.OpenXR) {
            gl::bind_framebuffer(gl::Framebuffer_target::framebuffer, 0);
            if (m_editor_context.use_sleep) {
                std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
                {
                    ERHE_PROFILE_SCOPE("swap_buffers");
                    m_context_window.swap_buffers();
                }
                std::chrono::steady_clock::time_point end_time = std::chrono::steady_clock::now();
                const std::chrono::duration<float> swap_duration = start_time - end_time;
                const std::chrono::duration<float> sleep_margin{m_editor_context.sleep_margin};
                if (swap_duration > sleep_margin) {
                    ERHE_PROFILE_SCOPE("sleep");
                    erhe::time::sleep_for(swap_duration - sleep_margin);
                }
            } else {
                ERHE_PROFILE_SCOPE("swap_buffers");
                m_context_window.swap_buffers();
            }
        }
    }

    [[nodiscard]] static auto get_windows_ini_path() -> std::string
    {
        bool openxr{false};
        const auto& heaset = erhe::configuration::get_ini_file_section("erhe.ini", "headset");
        heaset.get("openxr", openxr);
        return openxr ? "openxr_windows.ini" : "windows.ini";
    }

    [[nodiscard]] static auto conditionally_enable_window_imgui_host(erhe::window::Context_window* context_window)
    {
        bool openxr{false};
        const auto& heaset = erhe::configuration::get_ini_file_section("erhe.ini", "headset");
        heaset.get("openxr", openxr);
        return openxr ? nullptr : context_window;
    }

    [[nodiscard]] auto create_window() -> erhe::window::Context_window
    {
        auto& erhe_ini = erhe::configuration::get_ini_file("erhe.ini");

        const auto& headset_section = erhe_ini.get_section("headset");
        headset_section.get("openxr",        m_editor_context.OpenXR);
        headset_section.get("openxr_mirror", m_editor_context.OpenXR_mirror);

        const auto& developer_section = erhe_ini.get_section("developer");
        developer_section.get("enable", m_editor_context.developer_mode);

        const auto& renderdoc_section = erhe_ini.get_section("renderdoc");
        renderdoc_section.get("capture_support", m_editor_context.renderdoc);
        if (m_editor_context.renderdoc) {
            m_editor_context.developer_mode = true;
        }

        const auto& window_section = erhe_ini.get_section("window");
        window_section.get("use_sleep", m_editor_context.use_sleep);
        window_section.get("sleep_margin", m_editor_context.sleep_margin);

        erhe::window::Window_configuration configuration{
            .use_depth         = m_editor_context.OpenXR_mirror,
            .use_stencil       = m_editor_context.OpenXR_mirror,
            .gl_major          = 4,
            .gl_minor          = 6,
            .width             = 1920,
            .height            = 1080,
            .msaa_sample_count = m_editor_context.OpenXR_mirror ? 0 : 0,
            .title             = erhe::window::format_window_title("erhe editor by Timo Suoranta")
        };

        window_section.get("show",             configuration.show);
        window_section.get("fullscreen",       configuration.fullscreen);
        window_section.get("use_transparency", configuration.framebuffer_transparency);
        window_section.get("gl_major",         configuration.gl_major);
        window_section.get("gl_minor",         configuration.gl_minor);
        window_section.get("width",            configuration.width);
        window_section.get("height",           configuration.height);
        window_section.get("swap_interval",    configuration.swap_interval);

        return erhe::window::Context_window{configuration};
    }

#pragma region Editor()
#pragma region Initialize members
    Editor()
        : m_commands          {}
        , m_scene_message_bus {}
        , m_editor_message_bus{}
        , m_input_state       {}
        , m_time              {}
        , m_editor_context    {}

        , m_clipboard             {m_commands, m_editor_context}
        , m_context_window        {create_window()}
        , m_editor_settings       {m_editor_message_bus}
        , m_graphics_instance     {m_context_window}
        , m_imgui_renderer        {m_graphics_instance, m_editor_settings.imgui}
        , m_line_renderer_set     {m_graphics_instance}
        , m_program_interface     {m_graphics_instance}
        , m_rendergraph           {m_graphics_instance}
        , m_text_renderer         {m_graphics_instance}

        , m_programs              {m_graphics_instance, m_program_interface}
        , m_forward_renderer      {m_graphics_instance, m_program_interface}
        , m_shadow_renderer       {m_graphics_instance, m_program_interface}
        , m_mesh_memory           {m_graphics_instance, m_program_interface}

        , m_imgui_windows         {m_imgui_renderer,    conditionally_enable_window_imgui_host(&m_context_window), m_rendergraph, get_windows_ini_path()}
        , m_editor_scenes         {m_editor_context,    m_time}
        , m_editor_windows        {m_editor_context}
        , m_asset_browser         {m_imgui_renderer,    m_imgui_windows,     m_editor_context}
        , m_icon_set              {m_graphics_instance, m_imgui_renderer,    m_editor_context, m_editor_settings.icons, m_programs}
        , m_post_processing       {m_graphics_instance, m_editor_context}
        , m_id_renderer           {m_graphics_instance, m_program_interface, m_mesh_memory,     m_programs}
        , m_composer_window       {m_imgui_renderer,    m_imgui_windows,     m_editor_context}
        , m_selection_window      {m_imgui_renderer,    m_imgui_windows,     m_editor_context}
        , m_settings_window       {m_imgui_renderer,    m_imgui_windows,     m_editor_context}
        , m_viewport_scene_views  {m_commands,          m_editor_context,    m_editor_message_bus}
        , m_editor_rendering      {m_commands,          m_graphics_instance, m_editor_context,  m_editor_message_bus, m_mesh_memory, m_programs}
        , m_selection             {m_commands,          m_editor_context,    m_editor_message_bus}
        , m_operation_stack       {m_commands,          m_imgui_renderer,    m_imgui_windows,   m_editor_context}
        , m_scene_commands        {m_commands,          m_editor_context}
        , m_clipboard_window      {m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_commands_window       {m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_layers_window         {m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_network_window        {m_imgui_renderer, m_imgui_windows, m_editor_context, m_time}
        , m_operations            {m_commands,       m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_physics_window        {m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_post_processing_window{m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_properties            {m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_rendergraph_window    {m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_tool_properties_window{m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_viewport_config_window{m_imgui_renderer, m_imgui_windows, m_editor_context}
        , m_logs                  {m_commands, m_imgui_renderer}
        , m_log_settings_window   {m_imgui_renderer, m_imgui_windows, m_logs}
        , m_tail_log_window       {m_imgui_renderer, m_imgui_windows, m_logs}
        , m_frame_log_window      {m_imgui_renderer, m_imgui_windows, m_logs}
        , m_performance_window    {m_imgui_renderer, m_imgui_windows}
        , m_pipelines             {m_imgui_renderer, m_imgui_windows}

        , m_tools{
            m_imgui_renderer, m_imgui_windows,
            m_graphics_instance, m_scene_message_bus, m_editor_context,
            m_editor_rendering,  m_mesh_memory,       m_programs
        }
        , m_scene_builder{
            m_graphics_instance,
            m_imgui_renderer,
            m_imgui_windows,
            m_rendergraph,
            m_scene_message_bus,
            m_editor_context,
            m_editor_message_bus,
            m_editor_rendering,
            m_editor_scenes,
            m_editor_settings,
            m_mesh_memory,
            m_post_processing,
            m_tools,
            m_viewport_scene_views
        }
        , m_fly_camera_tool       {m_commands, m_imgui_renderer, m_imgui_windows, m_editor_context, m_editor_message_bus, m_time, m_tools}
        , m_headset_view{
            m_commands,
            m_graphics_instance,
            m_imgui_renderer,
            m_imgui_windows,
            m_rendergraph,
            m_context_window,
            m_editor_context,
            m_editor_rendering,
            m_editor_settings,
            m_mesh_memory,
            m_scene_builder,
            m_time
        }
#if defined(ERHE_XR_LIBRARY_OPENXR)
        , m_hand_tracker{m_editor_context, m_editor_rendering}
#endif
        , m_move_tool             {m_editor_context, m_icon_set, m_tools}
        , m_rotate_tool           {m_editor_context, m_icon_set, m_tools}
        , m_scale_tool            {m_editor_context, m_icon_set, m_tools}
        , m_transform_tool{
            m_commands,           m_imgui_renderer,  m_imgui_windows, m_editor_context,
            m_editor_message_bus, m_headset_view,    m_mesh_memory,   m_tools
        }
        , m_hud{
            m_commands,       m_graphics_instance,  m_imgui_renderer, m_rendergraph,
            m_editor_context, m_editor_message_bus, m_editor_windows, m_headset_view,  m_mesh_memory,
            m_scene_builder,  m_tools
        }
        , m_hotbar{
            m_commands,       m_imgui_renderer,     m_imgui_windows,
            m_editor_context, m_editor_message_bus, m_headset_view,
            m_mesh_memory,    m_scene_builder,      m_tools
        }
        , m_hover_tool       {m_imgui_renderer, m_imgui_windows, m_editor_context, m_editor_message_bus, m_tools}

        , m_brdf_slice{
            m_rendergraph, m_forward_renderer, m_editor_context, m_programs
        }
        , m_debug_draw            {m_editor_context}
        , m_debug_view_window     {
            m_imgui_renderer, m_imgui_windows,    m_rendergraph, m_forward_renderer,
            m_editor_context, m_editor_rendering, m_mesh_memory, m_programs
        }
        , m_debug_visualizations  {m_imgui_renderer, m_imgui_windows, m_editor_context, m_editor_message_bus, m_editor_rendering}
        , m_material_preview      {m_graphics_instance, m_scene_message_bus, m_editor_context, m_mesh_memory, m_programs}

#if defined(ERHE_XR_LIBRARY_OPENXR)
        ////, m_theremin              {m_imgui_renderer, m_imgui_windows,  m_hand_tracker,       m_editor_context}
#endif
        , m_brush_tool            {m_commands,       m_editor_context, m_editor_message_bus, m_headset_view, m_icon_set, m_tools}
        , m_create                {m_imgui_renderer, m_imgui_windows,  m_editor_context,     m_tools}
        , m_grid_tool             {m_imgui_renderer, m_imgui_windows,  m_editor_context,     m_icon_set, m_tools}
        , m_material_paint_tool   {m_commands,       m_editor_context, m_headset_view,       m_icon_set, m_tools}
        , m_paint_tool{
            m_commands,           m_imgui_renderer, m_imgui_windows, m_editor_context,
            m_editor_message_bus, m_headset_view,   m_icon_set,      m_tools
        }
        , m_physics_tool  {m_commands,       m_editor_context, m_editor_message_bus, m_headset_view, m_icon_set, m_tools}
        , m_selection_tool{m_editor_context, m_icon_set, m_tools}
    {
#pragma endregion Initialize members
        ERHE_PROFILE_FUNCTION();
        ERHE_PROFILE_GPU_CONTEXT

#if defined(ERHE_XR_LIBRARY_OPENXR)
        if (m_editor_context.OpenXR) {
            m_selection.setup_xr_bindings(m_commands, m_headset_view);
        }
#endif

        fill_editor_context();

        const auto& physics_section = erhe::configuration::get_ini_file_section("erhe.ini", "physics");
        physics_section.get("static_enable",  m_editor_settings.physics.static_enable);
        physics_section.get("dynamic_enable", m_editor_settings.physics.dynamic_enable);
        if (!m_editor_settings.physics.static_enable) {
            m_editor_settings.physics.dynamic_enable = false;
        }

        {
            m_clipboard_window.set_developer();
            m_commands_window.set_developer();
            m_composer_window.set_developer();
            m_debug_view_window.set_developer();
            m_hover_tool.set_developer();
            m_frame_log_window.set_developer();
            m_log_settings_window.set_developer();
            m_performance_window.set_developer();
            m_pipelines.set_developer();
#if defined(ERHE_XR_LIBRARY_OPENXR)
            ////m_theremin.set_developer();
#endif
            m_layers_window.set_developer();
            m_network_window.set_developer();
            m_post_processing_window.set_developer();
            m_rendergraph_window.set_developer();
            m_selection_window.set_developer();
            m_tail_log_window.set_developer();
            m_operation_stack.set_developer();
        }

        m_hotbar.get_all_tools();

        gl::clip_control(gl::Clip_control_origin::lower_left, gl::Clip_control_depth::zero_to_one);
        gl::enable      (gl::Enable_cap::framebuffer_srgb);

        const auto window_viewport = m_imgui_windows.get_window_imgui_host();
        if (!m_editor_context.OpenXR) {
            window_viewport->set_begin_callback(
                [this](erhe::imgui::Imgui_host& imgui_host) {
                    m_editor_windows.viewport_menu(imgui_host);
                }
            );
        }

#if defined(ERHE_XR_LIBRARY_OPENXR)
        if (m_editor_context.OpenXR) {
            // TODO Create windows directly to correct viewport?
            // Move all imgui windows that have window viewport to hud viewport
            const auto viewport = m_hud.get_rendertarget_imgui_viewport();
            if (viewport) {
                auto& windows = m_imgui_windows.get_windows();
                for (auto window : windows) {
                    if (window->get_imgui_host() == window_viewport.get()) {
                        window->set_imgui_host(viewport.get());
                    }
                }
            }
        }
#endif
        m_tools.set_priority_tool(&m_physics_tool);
    }
#pragma endregion Editor()

    void fill_editor_context()
    {
        ERHE_PROFILE_FUNCTION();

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
        m_editor_context.brdf_slice             = &m_brdf_slice            ;
        m_editor_context.brush_tool             = &m_brush_tool            ;
        m_editor_context.clipboard              = &m_clipboard             ;
        m_editor_context.clipboard_window       = &m_clipboard_window      ;
        m_editor_context.create                 = &m_create                ;
        m_editor_context.editor_message_bus     = &m_editor_message_bus    ;
        m_editor_context.editor_rendering       = &m_editor_rendering      ;
        m_editor_context.editor_scenes          = &m_editor_scenes         ;
        m_editor_context.editor_settings        = &m_editor_settings       ;
        m_editor_context.editor_windows         = &m_editor_windows        ;
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
        m_editor_context.time                   = &m_time                  ;
        m_editor_context.tools                  = &m_tools                 ;
        m_editor_context.transform_tool         = &m_transform_tool        ;
        m_editor_context.viewport_config_window = &m_viewport_config_window;
        m_editor_context.scene_views            = &m_viewport_scene_views  ;
    }

    auto on_key_event(const erhe::window::Input_event& input_event) -> bool override
    {
        m_input_state.shift   = erhe::bit::test_all_rhs_bits_set(input_event.u.key_event.modifier_mask, erhe::window::Key_modifier_bit_shift);
        m_input_state.control = erhe::bit::test_all_rhs_bits_set(input_event.u.key_event.modifier_mask, erhe::window::Key_modifier_bit_ctrl);
        m_input_state.alt     = erhe::bit::test_all_rhs_bits_set(input_event.u.key_event.modifier_mask, erhe::window::Key_modifier_bit_menu);
        return false;
    }
    auto on_window_close_event(const erhe::window::Input_event&) -> bool override
    {
        m_close_requested = true;
        return true;
    }
    auto on_window_refresh_event(const erhe::window::Input_event&) -> bool override
    {
        // TODO
        return true;
    }

    void run()
    {
        ERHE_PROFILE_FUNCTION();

        m_run_started = true;
        float wait_time = m_editor_context.use_sleep ? m_editor_context.sleep_margin : 0.0f;
        // TODO: https://registry.khronos.org/OpenGL/extensions/NV/GLX_NV_delay_before_swap.txt
        // Also:
        //  - Measure time since first swapbuffers
        //  - Count number of swapbuffers
        //  - Wait to avoid presenting frames faster than display refreshrate
        while (!m_close_requested) {
            m_context_window.poll_events(wait_time);
            {
                ERHE_PROFILE_SCOPE("dispatch events");
                auto& input_events = m_context_window.get_input_events();
                for (erhe::window::Input_event& input_event : input_events) {
                    dispatch_input_event(input_event);
                }
            }
            tick();

            ERHE_PROFILE_FRAME_END
        }
        m_run_stopped = true;
    }

#pragma region Members
    bool m_close_requested{false};

    // No dependencies (constructors)
    erhe::commands::Commands                m_commands;
    erhe::scene::Scene_message_bus          m_scene_message_bus;
    Editor_message_bus                      m_editor_message_bus;
    Input_state                             m_input_state;
    Time                                    m_time;
    Editor_context                          m_editor_context;
    bool                                    m_run_started{false};
    bool                                    m_run_stopped{false};

    Clipboard                               m_clipboard;
    erhe::window::Context_window            m_context_window;
    Editor_settings                         m_editor_settings;
    erhe::graphics::Instance                m_graphics_instance;
    erhe::imgui::Imgui_renderer             m_imgui_renderer;
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
    Editor_windows                          m_editor_windows;

    Asset_browser                           m_asset_browser;
    Icon_set                                m_icon_set;
    Post_processing                         m_post_processing;
    Id_renderer                             m_id_renderer;
    Composer_window                         m_composer_window;
    Selection_window                        m_selection_window;
    Settings_window                         m_settings_window;
    Scene_views                             m_viewport_scene_views;
    Editor_rendering                        m_editor_rendering;
    Selection                               m_selection;
    Operation_stack                         m_operation_stack;
    Scene_commands                          m_scene_commands;
    Clipboard_window                        m_clipboard_window;
    Commands_window                         m_commands_window;
    Layers_window                           m_layers_window;
    Network_window                          m_network_window;
    Operations                              m_operations;
    Physics_window                          m_physics_window;
    Post_processing_window                  m_post_processing_window;
    Properties                              m_properties;
    Rendergraph_window                      m_rendergraph_window;
    Tool_properties_window                  m_tool_properties_window;
    Viewport_config_window                  m_viewport_config_window;
    erhe::imgui::Logs                       m_logs;
    erhe::imgui::Log_settings_window        m_log_settings_window;
    erhe::imgui::Tail_log_window            m_tail_log_window;
    erhe::imgui::Frame_log_window           m_frame_log_window;
    erhe::imgui::Performance_window         m_performance_window;
    erhe::imgui::Pipelines                  m_pipelines;

    Tools                                   m_tools;
    Scene_builder                           m_scene_builder;
    Fly_camera_tool                         m_fly_camera_tool;
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
    Brdf_slice                              m_brdf_slice;
    Debug_draw                              m_debug_draw;
    Debug_view_window                       m_debug_view_window;
    Debug_visualizations                    m_debug_visualizations;
    Material_preview                        m_material_preview;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    ////Theremin                                m_theremin;
#endif

    Brush_tool                              m_brush_tool;
    Create                                  m_create;
    Grid_tool                               m_grid_tool;
    Material_paint_tool                     m_material_paint_tool;
    Paint_tool                              m_paint_tool;
    Physics_tool                            m_physics_tool;
    Selection_tool                          m_selection_tool;
#pragma endregion Members
};

void run_editor()
{
    ERHE_PROFILE_FUNCTION();

#if defined(ERHE_PROFILE_LIBRARY_NVTX)
    nvtxInitialize(nullptr);
#endif
    {
        ERHE_PROFILE_SCOPE("erhe::log::initialize_log_sinks()");
        erhe::log::initialize_log_sinks();
    }
    {
        ERHE_PROFILE_SCOPE("initialize logging");
        gl::initialize_logging();
        erhe::commands::initialize_logging();
        erhe::file::initialize_logging();
        erhe::gltf::initialize_logging();
        erhe::geometry::initialize_logging();
        erhe::graphics::initialize_logging();
        erhe::imgui::initialize_logging();
        erhe::item::initialize_logging();
        erhe::math::initialize_logging();
        erhe::net::initialize_logging();
        erhe::physics::initialize_logging();
        erhe::primitive::initialize_logging();
        erhe::raytrace::initialize_logging();
        erhe::renderer::initialize_logging();
        erhe::rendergraph::initialize_logging();
        erhe::scene::initialize_logging();
        erhe::scene_renderer::initialize_logging();
        erhe::window::initialize_logging();
        erhe::ui::initialize_logging();
#if defined(ERHE_XR_LIBRARY_OPENXR)
        erhe::xr::initialize_logging();
#endif
        editor::initialize_logging();
    }

    erhe::time::sleep_initialize();

    {
        ERHE_PROFILE_SCOPE("init renderdoc");
        bool enable_renderdoc_capture_support{false};
        const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "renderdoc");
        ini.get("capture_support", enable_renderdoc_capture_support);
        if (enable_renderdoc_capture_support) {
            erhe::window::initialize_frame_capture();
        }
    }

    {
        ERHE_PROFILE_SCOPE("Construct and run Editor");
        Editor editor{};
        editor.run();
    }
}

} // namespace editor
