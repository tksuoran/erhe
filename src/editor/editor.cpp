#include "editor.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "editor_settings.hpp"
#include "editor_windows.hpp"
#include "scene/material_library.hpp"
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
#include "scene/scene_root.hpp"
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
#   include "erhe_xr/xr_instance.hpp"
#endif

#include "erhe_commands/commands.hpp"
#include "erhe_commands/commands_log.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_dataformat/dataformat_log.hpp"
#include "erhe_file/file.hpp"
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
#include "erhe_physics/iworld.hpp"
#if defined(ERHE_PHYSICS_LIBRARY_JOLT)
#   include "erhe_renderer/debug_renderer.hpp"
#endif
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

#include <taskflow/taskflow.hpp>

#if defined(ERHE_PROFILE_LIBRARY_NVTX)
#   include <nvtx3/nvToolsExt.h>
#endif

#if defined(ERHE_PROFILE_LIBRARY_TRACY)
#   include <tracy/TracyC.h>
#endif

#include <geogram/basic/common.h>
#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/basic/logger.h>

namespace editor {

#if defined(ERHE_PROFILE_LIBRARY_TRACY)
class Tracy_observer : public tf::ObserverInterface {
public:
    void set_up(std::size_t) override final {};

    void on_entry(tf::WorkerView, tf::TaskView tv) override final
    {
        const std::string& name = tv.name();
#if TRACY_ENABLE
        const std::size_t hash = tv.hash_value();
        const uint32_t color = 0x804020ffu;
        uint64_t srcloc = ___tracy_alloc_srcloc_name(1, "", 0, "", 0, name.c_str(), name.length(), color);
        TracyCZoneCtx zone = ___tracy_emit_zone_begin_alloc(srcloc, 1);
        // log_startup->trace("enter zone = {} worker = {} task = {} hash = {:x}", zone.id, w.id(), name, hash);
        st_entries.emplace_back(hash, zone);
#endif
    }

    void on_exit(tf::WorkerView, tf::TaskView tv) override final
    {
        //const std::string& name = tv.name();
#if TRACY_ENABLE
        const std::size_t hash = tv.hash_value();
        for (Entry& entry : st_entries) {
            if (entry.hash == hash && entry.zone.active != 0) {
                // log_startup->trace("leave zone = {} worker = {} task = {} hash = {:x}", entry.zone.id, w.id(), name, hash);
                ___tracy_emit_zone_end(entry.zone);
                entry.zone.active = 0;
                return;
            }
        }
        ERHE_FATAL("zone not found");
#endif
    }

private:
    struct Entry
    {
        std::size_t   hash;
        TracyCZoneCtx zone;
    };
    static thread_local std::vector<Entry> st_entries;
};

thread_local std::vector<Tracy_observer::Entry> Tracy_observer::st_entries;
#endif


class Editor : public erhe::window::Input_event_handler
{
public:
    void tick()
    {
        ERHE_PROFILE_FUNCTION();
        m_frame_log_window->on_frame_begin();
        // log_input_frame->trace("----------------------- Editor::tick() -----------------------");

        std::vector<erhe::window::Input_event>& input_events = m_context_window->get_input_events();

        m_time->prepare_update();
        m_fly_camera_tool->on_frame_begin();

        // Updating pointer is probably sufficient to be done once per frame
        if (!m_editor_context.OpenXR) { //if (!m_headset_view.is_active()) {
            auto* imgui_host = m_imgui_windows->get_window_imgui_host().get(); // get glfw window hosted viewport
            if (imgui_host != nullptr) {
                m_viewport_scene_views->update_pointer(imgui_host); // updates what viewport window is being hovered
            }
        }

#if defined(ERHE_XR_LIBRARY_OPENXR)
        // - updates cameras
        // - updates pointer context for headset scene view from controller
        // - sends XR input events to Commands
        // - updates controller visualization nodes
        if (m_editor_context.OpenXR) {
            ERHE_PROFILE_SCOPE("OpenXR update events");
            bool headset_update_ok = m_headset_view->update_events();
            if (headset_update_ok && m_headset_view->is_active()) {
                m_viewport_config_window->set_edit_data(&m_headset_view->get_config());
            } else{
                ERHE_PROFILE_SCOPE("OpenXR sleep");
                // Throttle loop since xrWaitFrame won't be called.
                std::this_thread::sleep_for(std::chrono::milliseconds{250});
            }
        }
#endif

        m_editor_scenes->before_physics_simulation_steps();

        float host_system_dt_s = 0.0f;
        int64_t host_system_time_ns = 0;
        m_time->for_each_fixed_step(
            [this, &input_events, &host_system_dt_s, &host_system_time_ns](const Time_context& time_context) {
                host_system_dt_s += time_context.host_system_dt_s;
                host_system_time_ns = time_context.host_system_time_ns;
                m_headset_view   ->update_fixed_step();
                m_fly_camera_tool->update_fixed_step(time_context);
                m_editor_scenes  ->update_physics_simulation_fixed_step(time_context);
            }
        );
        m_editor_scenes->after_physics_simulation_steps();
        m_imgui_windows->process_events(host_system_dt_s, host_system_time_ns);
        m_commands     ->tick(host_system_time_ns, input_events);

        // Once per frame updates
        m_network_window->update_network();

        // - Update all ImGui hosts. glfw window host processes input events, converting them to ImGui inputs events
        //   This may consume some input events, so that they will not get processed by m_commands.tick() below
        // - Call all ImGui code (Imgui_window)
        m_imgui_windows->begin_frame();
        m_imgui_windows->draw_imgui_windows();
        m_imgui_windows->end_frame();

        // - Apply all command bindings (OpenXR bindings were already executed above)

        //m_fly_camera_tool->update_once_per_frame(timestamp);

        auto* imgui_host = m_imgui_windows->get_window_imgui_host().get(); // get window hosted viewport
        if (imgui_host != nullptr) {
            m_viewport_scene_views->update_hover_info(imgui_host); // updates what viewport window is being hovered
        }

        m_operation_stack->update();

        // - Execute all fixes step updates
        // - Execute all once per frame updates
        //    - Editor_scenes (updates physics)
        //    - Fly_camera_tool
        //    - Network_window 
        m_editor_message_bus->update(); // Flushes queued messages

        // Apply physics updates

        m_fly_camera_tool->on_frame_end();

        // Rendering
        m_graphics_instance->shader_monitor.update_once_per_frame();
        m_mesh_memory->gl_buffer_transfer_queue.flush();

        m_editor_rendering->begin_frame(); // tests renderdoc capture start

        // Execute rendergraph
        m_rendergraph->execute();

        m_imgui_renderer->next_frame();
        m_editor_rendering->end_frame();
        if (!m_editor_context.OpenXR) {
            gl::bind_framebuffer(gl::Framebuffer_target::framebuffer, 0);
            if (m_editor_context.use_sleep) {
                std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
                {
                    ERHE_PROFILE_SCOPE("swap_buffers");
                    m_context_window->swap_buffers();
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
                m_context_window->swap_buffers();
            }
        }
        m_frame_log_window->on_frame_end();
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

    [[nodiscard]] auto create_window() -> std::unique_ptr<erhe::window::Context_window>
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
        window_section.get("enable_joystick",  configuration.enable_joystick);

        return std::make_unique<erhe::window::Context_window>(configuration);
    }

    Editor()
    {
        int init_thread_count = 1;
        auto& erhe_ini = erhe::configuration::get_ini_file("erhe.ini");
        const auto& threading_section = erhe_ini.get_section("threading");
        threading_section.get("init_thread_count", init_thread_count);

        m_executor = std::make_unique<tf::Executor>(init_thread_count);

        try {
            tf::Taskflow taskflow;
#if defined(ERHE_PROFILE_LIBRARY_TRACY)
            std::shared_ptr<Tracy_observer> observer = m_executor->make_observer<Tracy_observer>();
#endif

            m_commands           = std::make_unique<erhe::commands::Commands      >();
            m_scene_message_bus  = std::make_unique<erhe::scene::Scene_message_bus>();
            m_editor_message_bus = std::make_unique<Editor_message_bus            >();
            m_editor_settings    = std::make_unique<Editor_settings               >();
            m_input_state        = std::make_unique<Input_state                   >();
            m_time               = std::make_unique<Time                          >();
            auto& commands           = *m_commands          .get();
            auto& editor_message_bus = *m_editor_message_bus.get();

            // Icon rasterization is slow task that can be run in parallel with
            // GL context creation and Joystick scanning which are two other slow tasks
            Icons icons;
            Icon_loader icon_loader{m_editor_settings->icon_settings};
            // TODO compare with Icon_set::load_icons()
            icons.queue_load_icons(icon_loader);
            auto icon_rasterization_task = taskflow.emplace([this, &icon_loader](){
                icon_loader.execute_rasterization_queue();
            })  .name("Icon rasterization");

            m_executor->run(taskflow);

            // Window and graphics context creation - in main thread
            m_context_window = create_window();
            m_editor_settings->apply_limits(editor_message_bus);

            // Graphics context state init after window - in main thread
            m_graphics_instance = std::make_unique<erhe::graphics::Instance>(*m_context_window.get());

#if defined(ERHE_XR_LIBRARY_OPENXR)
            // Apparently it is necessary to create OpenXR session in the main thread / original OpenGL
            // context, instead of using shared context / worker thread
            if (m_editor_context.OpenXR) {
                ERHE_PROFILE_SCOPE("make xr::Headset");
                erhe::xr::Xr_configuration configuration{
                    .debug             = false,
                    .quad_view         = false,
                    .depth             = false,
                    .visibility_mask   = false,
                    .hand_tracking     = false,
                    .composition_alpha = false,
                    .mirror_mode       = false
                };
                const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "headset");
                ini.get("quad_view",         configuration.quad_view);
                ini.get("debug",             configuration.debug);
                ini.get("depth",             configuration.depth);
                ini.get("visibility_mask",   configuration.visibility_mask);
                ini.get("hand_tracking",     configuration.hand_tracking);
                ini.get("composition_alpha", configuration.composition_alpha);
                ini.get("mirror_mode",       configuration.mirror_mode);
                m_headset = std::make_unique<erhe::xr::Headset>(*m_context_window.get(), configuration);
                if (!m_headset->is_valid()) {
                    log_headset->info("Headset initialization failed. Disabling OpenXR.");
                    m_editor_context.OpenXR = false;
                    m_headset.reset();
                }
            }
#endif

            // It seems to be faster to create the worker thread here instead of between
            // executor run and wait.
            m_graphics_instance->context_provider.provide_worker_contexts(
                m_context_window.get(),
                8u,
                []() -> bool { return true; }
            );

            m_vertex_format = erhe::dataformat::Vertex_format{
                {
                    0,
                    {
                        { erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::position,      0},
                        { erhe::dataformat::Format::format_32_vec4_uint,  erhe::dataformat::Vertex_attribute_usage::joint_indices, 0},
                        { erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::joint_weights, 0}
                    }
                },
                {
                    1,
                    {
                        { erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::normal,    0},
                        { erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::normal,    1}, // editor wireframe bias requires smooth normal attribute
                        { erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::tangent,   0},
                        { erhe::dataformat::Format::format_32_vec2_float, erhe::dataformat::Vertex_attribute_usage::tex_coord, 0},
                        { erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::color,     0},
                        { erhe::dataformat::Format::format_8_vec2_unorm,  erhe::dataformat::Vertex_attribute_usage::custom,    erhe::dataformat::custom_attribute_aniso_control},
                        { erhe::dataformat::Format::format_16_vec2_uint,  erhe::dataformat::Vertex_attribute_usage::custom,    erhe::dataformat::custom_attribute_valency_edge_count}
                    }
                }
            };

            m_clipboard            = std::make_unique<Clipboard     >(commands, m_editor_context, editor_message_bus);
            m_editor_scenes        = std::make_unique<Editor_scenes >(m_editor_context);
            m_editor_windows       = std::make_unique<Editor_windows>(m_editor_context, commands);
            m_viewport_scene_views = std::make_unique<Scene_views   >(commands, m_editor_context, editor_message_bus);
            m_selection            = std::make_unique<Selection     >(commands, m_editor_context, editor_message_bus);
            m_scene_commands       = std::make_unique<Scene_commands>(commands, m_editor_context);
            m_debug_draw           = std::make_unique<Debug_draw    >(m_editor_context);
            m_program_interface    = std::make_unique<erhe::scene_renderer::Program_interface>(*m_graphics_instance.get(), m_vertex_format);
            m_programs             = std::make_unique<Programs>(*m_graphics_instance.get());

            auto programs_load_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_programs->load_programs(*m_executor.get(), *m_graphics_instance.get(), *m_program_interface.get());
            })  .name("Programs (load)");

            auto imgui_renderer_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_imgui_renderer = std::make_unique<erhe::imgui::Imgui_renderer>(*m_graphics_instance.get(), m_editor_settings->imgui);
            })  .name("Imgui_renderer");

            auto line_renderer_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_line_renderer = std::make_unique<erhe::renderer::Line_renderer>(*m_graphics_instance.get());
            })  .name("Line_renderer");

            auto rendergraph_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_rendergraph = std::make_unique<erhe::rendergraph::Rendergraph>(*m_graphics_instance.get());
            })  .name("Rendergraph");

#if defined(ERHE_PHYSICS_LIBRARY_JOLT)
            auto debug_renderer_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_debug_renderer = std::make_unique<erhe::renderer::Debug_renderer>(*m_line_renderer.get());
            }).name("Debug_renderer").succeed(line_renderer_task);
#endif

            auto text_renderer_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_text_renderer = std::make_unique<erhe::renderer::Text_renderer>(*m_graphics_instance.get());
            }).name("Text_renderer");

            auto forward_renderer_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_forward_renderer = std::make_unique<erhe::scene_renderer::Forward_renderer>(*m_graphics_instance.get(), *m_program_interface.get());
            })  .name("Forward_renderer");

            auto shadow_renderer_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_shadow_renderer = std::make_unique<erhe::scene_renderer::Shadow_renderer >(*m_graphics_instance.get(), *m_program_interface.get());
            })  .name("Shadow_renderer");

            auto mesh_memory_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_mesh_memory = std::make_unique<Mesh_memory>(*m_graphics_instance.get(), m_vertex_format);
            })  .name("Mesh_memory");

            auto imgui_windows_task = taskflow.emplace([this]()
            {
                m_imgui_windows = std::make_unique<erhe::imgui::Imgui_windows>(
                    *m_imgui_renderer.get(),
                    conditionally_enable_window_imgui_host(m_context_window.get()),
                    *m_rendergraph.get(),
                    get_windows_ini_path()
                );
            })  .name("Imgui_windows")
                .succeed(imgui_renderer_task, rendergraph_task);

            auto icon_set_task = taskflow.emplace([this, &icons, &icon_loader]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_icon_set = std::make_unique<Icon_set>(
                    m_editor_context,
                    *m_graphics_instance.get(),
                    m_editor_settings->icon_settings,
                    icons,
                    icon_loader
                );
            })  .name("Icon_set")
                .succeed(icon_rasterization_task);

            auto post_processing_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_post_processing = std::make_unique<Post_processing>(*m_graphics_instance.get(), m_editor_context);
            })  .name("Post_processing");

            auto id_renderer_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_id_renderer = std::make_unique<Id_renderer>(*m_graphics_instance.get(), *m_program_interface.get(), *m_mesh_memory.get(), *m_programs.get());
            }
            )  .name("Id_renderer")
                .succeed(mesh_memory_task);

            auto editor_rendering_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_editor_rendering = std::make_unique<Editor_rendering>(
                    *m_commands.get(),
                    *m_graphics_instance.get(),
                    m_editor_context,
                    *m_editor_message_bus.get(),
                    *m_mesh_memory.get(),
                    *m_programs.get()
                );
            })  .name("Editor_rendering")
                .succeed(mesh_memory_task);

            auto some_windows_task = taskflow.emplace([this]()
            {
                m_operation_stack        = std::make_unique<Operation_stack                 >(*m_executor.get(),       *m_commands.get(),       *m_imgui_renderer.get(), *m_imgui_windows.get(), m_editor_context);
                m_asset_browser          = std::make_unique<Asset_browser                   >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_editor_context);
                m_composer_window        = std::make_unique<Composer_window                 >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_editor_context);
                m_selection_window       = std::make_unique<Selection_window                >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_editor_context);
                m_settings_window        = std::make_unique<Settings_window                 >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_editor_context);
                m_clipboard_window       = std::make_unique<Clipboard_window                >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_editor_context);
                m_commands_window        = std::make_unique<Commands_window                 >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_editor_context);
                m_layers_window          = std::make_unique<Layers_window                   >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_editor_context);
                m_network_window         = std::make_unique<Network_window                  >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_editor_context);
                m_operations             = std::make_unique<Operations                      >(*m_commands.get(),       *m_imgui_renderer.get(), *m_imgui_windows.get(), m_editor_context, *m_editor_message_bus.get());
                m_physics_window         = std::make_unique<Physics_window                  >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_editor_context);
                m_post_processing_window = std::make_unique<Post_processing_window          >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_editor_context);
                m_properties             = std::make_unique<Properties                      >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_editor_context);
                m_rendergraph_window     = std::make_unique<Rendergraph_window              >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_editor_context);
                m_tool_properties_window = std::make_unique<Tool_properties_window          >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_editor_context);
                m_viewport_config_window = std::make_unique<Viewport_config_window          >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_editor_context);
                m_logs                   = std::make_unique<erhe::imgui::Logs               >(*m_commands.get(),       *m_imgui_renderer.get());
                m_log_settings_window    = std::make_unique<erhe::imgui::Log_settings_window>(*m_imgui_renderer.get(), *m_imgui_windows.get(),  *m_logs.get());
                m_tail_log_window        = std::make_unique<erhe::imgui::Tail_log_window    >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  *m_logs.get());
                m_frame_log_window       = std::make_unique<erhe::imgui::Frame_log_window   >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  *m_logs.get());
                m_performance_window     = std::make_unique<erhe::imgui::Performance_window >(*m_imgui_renderer.get(), *m_imgui_windows.get());
                m_pipelines              = std::make_unique<erhe::imgui::Pipelines          >(*m_imgui_renderer.get(), *m_imgui_windows.get());
            })  .name("Some windows")
                .succeed(imgui_renderer_task, imgui_windows_task);

            auto tools_task = taskflow.emplace([this]()
            {
                m_tools = std::make_unique<Tools>(
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    *m_graphics_instance.get(),
                    *m_scene_message_bus.get(),
                    m_editor_context,
                    *m_editor_rendering.get(),
                    *m_mesh_memory.get(),
                    *m_programs
                );
                m_fly_camera_tool = std::make_unique<Fly_camera_tool>(
                    *m_commands.get(),
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_editor_context,
                    *m_editor_message_bus.get(),
                    *m_tools.get()
                );
            })  .name("Tools")
                .succeed(imgui_renderer_task, imgui_windows_task, mesh_memory_task, editor_rendering_task);
        
            auto default_scene_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                auto content_library = std::make_shared<Content_library>();
                add_default_materials(*content_library.get());

                m_default_scene = std::make_shared<Scene_root>(
                    m_imgui_renderer.get(),
                    m_imgui_windows.get(),
                    *m_scene_message_bus.get(),
                    &m_editor_context,
                    m_editor_message_bus.get(),
                    m_editor_scenes.get(),
                    content_library,
                    "Default Scene"
                );
                m_default_scene_browser = m_default_scene->make_browser_window(
                    *m_imgui_renderer.get(), *m_imgui_windows.get(), m_editor_context, *m_editor_settings.get()
                );
            })  .name("Default scene")
                .succeed(imgui_renderer_task, imgui_windows_task);

            auto scene_builder_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_scene_builder = std::make_unique<Scene_builder>(
                    m_default_scene,                //std::shared_ptr<Scene_root>     scene
                    *m_executor.get(),              //tf::Executor&                   executor
                    *m_graphics_instance.get(),     //erhe::graphics::Instance&       graphics_instance
                    *m_imgui_renderer.get(),        //erhe::imgui::Imgui_renderer&    imgui_renderer
                    *m_imgui_windows.get(),         //erhe::imgui::Imgui_windows&     imgui_windows
                    *m_rendergraph.get(),           //erhe::rendergraph::Rendergraph& rendergraph
                    m_editor_context,               //Editor_context&                 editor_context
                    *m_editor_rendering.get(),      //Editor_rendering&               editor_rendering
                    *m_editor_settings.get(),       //Editor_settings&                editor_settings
                    *m_mesh_memory.get(),           //Mesh_memory&                    mesh_memory
                    *m_post_processing.get(),       //Post_processing&                post_processing
                    *m_tools.get(),                 //Tools&                          tools
                    *m_viewport_scene_views.get()   //Scene_views&                    scene_views
                );
            })  .name("Scene_builder")
                .succeed(default_scene_task, imgui_renderer_task, imgui_windows_task, rendergraph_task, editor_rendering_task, mesh_memory_task, post_processing_task, tools_task);

            auto headset_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_headset_view = std::make_unique<Headset_view>(
                    *m_commands.get(),
                    *m_graphics_instance.get(),
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    *m_rendergraph.get(),
                    *m_context_window.get(),
#if defined(ERHE_XR_LIBRARY_OPENXR)
                    m_headset.get(),
#endif
                    m_editor_context,
                    *m_editor_rendering.get(),
                    *m_editor_settings.get()
                );
#if defined(ERHE_XR_LIBRARY_OPENXR)
                if (m_editor_context.OpenXR) {
                    m_hand_tracker = std::make_unique<Hand_tracker>(m_editor_context, *m_editor_rendering.get());
                }
#endif
            })  .name("Headset (init)")
                .succeed(imgui_renderer_task, imgui_windows_task, rendergraph_task, editor_rendering_task);

            auto headset_attach_task = taskflow.emplace([this]()
            {
#if defined(ERHE_XR_LIBRARY_OPENXR)
                if (m_editor_context.OpenXR) {
                    m_headset_view->attach_to_scene(m_default_scene, *m_mesh_memory.get());
                }
#endif
            })  .name("Headset (attach)")
                .succeed(default_scene_task, headset_task, mesh_memory_task, scene_builder_task);

            auto transform_tools_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_move_tool   = std::make_unique<Move_tool  >(m_editor_context, *m_icon_set.get(), *m_tools.get());
                m_rotate_tool = std::make_unique<Rotate_tool>(m_editor_context, *m_icon_set.get(), *m_tools.get());
                m_scale_tool  = std::make_unique<Scale_tool >(m_editor_context, *m_icon_set.get(), *m_tools.get());
                m_transform_tool = std::make_unique<Transform_tool>(
                    *m_executor.get(),
                    *m_commands.get(),
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_editor_context,
                    *m_editor_message_bus.get(),
                    *m_headset_view.get(),
                    *m_mesh_memory.get(),
                    *m_tools.get()
                );
            })  .name("Transform tools")
                .succeed(imgui_renderer_task, imgui_windows_task, headset_task, mesh_memory_task, icon_set_task, tools_task);

            auto group_1 = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_hud = std::make_unique<Hud>(
                    *m_commands.get(),
                    *m_graphics_instance.get(),
                    *m_imgui_renderer.get(),
                    *m_rendergraph.get(),
                    m_editor_context,
                    *m_editor_message_bus.get(),
                    *m_editor_windows.get(),
                    *m_headset_view.get(),
                    *m_mesh_memory.get(),
                    *m_scene_builder.get(),
                    *m_tools.get()
                );
                m_hotbar = std::make_unique<Hotbar>(
                    *m_commands.get(),
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_editor_context,
                    *m_editor_message_bus.get(),
                    *m_headset_view.get(),
                    *m_mesh_memory.get(),
                    *m_scene_builder.get(),
                    *m_tools.get()
                );
                m_hover_tool = std::make_unique<Hover_tool>(
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_editor_context,
                    *m_editor_message_bus.get(),
                    *m_tools.get()
                );
                m_brdf_slice = std::make_unique<Brdf_slice>(
                    *m_rendergraph.get(),
                    *m_forward_renderer.get(),
                    m_editor_context,
                    *m_programs.get()
                );
                m_debug_view_window = std::make_unique<Debug_view_window>(
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    *m_rendergraph.get(),
                    *m_forward_renderer.get(),
                    m_editor_context,
                    *m_editor_rendering.get(),
                    *m_mesh_memory.get(),
                    *m_programs.get()
                );
                m_debug_visualizations = std::make_unique<Debug_visualizations>(
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_editor_context,
                    *m_editor_message_bus.get(),
                    *m_editor_rendering.get()
                );
            })  .name("Group 1")
                .succeed(
                    imgui_renderer_task,
                    imgui_windows_task,
                    editor_rendering_task,
                    mesh_memory_task,
                    rendergraph_task,
                    forward_renderer_task,
                    tools_task,
                    scene_builder_task,
                    headset_task
                );

            auto material_preview_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_material_preview = std::make_unique<Material_preview>(
                    *m_graphics_instance.get(),
                    *m_scene_message_bus.get(),
                    m_editor_context,
                    *m_mesh_memory.get(),
                    *m_programs.get()
                );
            })  .name("Material_preview")
                .succeed(mesh_memory_task);

            auto brush_tool_task = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_brush_tool = std::make_unique<Brush_tool>(
                    *m_commands.get(),
                    m_editor_context,
                    *m_editor_message_bus.get(),
                    *m_headset_view.get(),
                    *m_icon_set.get(),
                    *m_tools.get()
                );
            })  .name("Brush_tool")
                .succeed(headset_task, icon_set_task, tools_task);

            auto group_2 = taskflow.emplace([this]()
            {
                erhe::graphics::Scoped_gl_context ctx{m_graphics_instance->context_provider};
                m_create = std::make_unique<Create>(
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_editor_context,
                    *m_tools.get()
                );
                m_grid_tool = std::make_unique<Grid_tool>(
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_editor_context,
                    *m_icon_set.get(),
                    *m_tools.get()
                );
                m_material_paint_tool = std::make_unique<Material_paint_tool>(
                    *m_commands.get(),
                    m_editor_context,
                    *m_headset_view.get(),
                    *m_icon_set.get(),
                    *m_tools.get()
                );
                m_paint_tool = std::make_unique<Paint_tool>(
                    *m_commands.get(),
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_editor_context,
                    *m_editor_message_bus.get(),
                    *m_headset_view.get(),
                    *m_icon_set.get(),
                    *m_tools.get()
                );
                m_physics_tool = std::make_unique<Physics_tool>(
                    *m_commands.get(),
                    m_editor_context,
                    *m_editor_message_bus.get(),
                    *m_headset_view.get(),
                    *m_icon_set.get(),
                    *m_tools.get()
                );
                m_selection_tool = std::make_unique<Selection_tool>(
                    m_editor_context,
                    *m_icon_set.get(),
                    *m_tools.get()
                );
            })  .name("Group 2")
                .succeed(imgui_renderer_task, imgui_windows_task, icon_set_task, tools_task, headset_task);

            std::string graph_dump = taskflow.dump();
            erhe::file::write_file("erhe_init_graph.dot", graph_dump);
            m_executor->run(taskflow).wait();
        } catch (std::runtime_error& e) {
            log_startup->error("exception: {}", e.what());
        }

        m_context_window->make_current();
        m_graphics_instance->opengl_state_tracker.on_thread_enter();

        ERHE_PROFILE_FUNCTION();
        ERHE_PROFILE_GPU_CONTEXT

#if defined(ERHE_XR_LIBRARY_OPENXR)
        if (m_editor_context.OpenXR) {
            m_selection->setup_xr_bindings(*m_commands.get(), *m_headset_view.get());
        }
#endif

        fill_editor_context();

        const auto& physics_section = erhe::configuration::get_ini_file_section("erhe.ini", "physics");
        physics_section.get("static_enable",  m_editor_settings->physics.static_enable);
        physics_section.get("dynamic_enable", m_editor_settings->physics.dynamic_enable);
        if (!m_editor_settings->physics.static_enable) {
            m_editor_settings->physics.dynamic_enable = false;
        }

        {
            m_clipboard_window   ->set_developer();
            m_commands_window    ->set_developer();
            m_composer_window    ->set_developer();
            m_debug_view_window  ->set_developer();
            m_hover_tool         ->set_developer();
            m_frame_log_window   ->set_developer();
            m_log_settings_window->set_developer();
            m_performance_window ->set_developer();
            m_pipelines          ->set_developer();
#if defined(ERHE_XR_LIBRARY_OPENXR)
            ////m_theremin.set_developer();
#endif
            m_layers_window         ->set_developer();
            m_network_window        ->set_developer();
            m_post_processing_window->set_developer();
            m_rendergraph_window    ->set_developer();
            m_selection_window      ->set_developer();
            m_tail_log_window       ->set_developer();
            m_operation_stack       ->set_developer();
        }

        m_hotbar->get_all_tools();

        gl::clip_control(gl::Clip_control_origin::lower_left, gl::Clip_control_depth::zero_to_one);
        gl::enable      (gl::Enable_cap::framebuffer_srgb);

        const auto window_viewport = m_imgui_windows->get_window_imgui_host();
        if (!m_editor_context.OpenXR) {
            window_viewport->set_begin_callback(
                [this](erhe::imgui::Imgui_host& imgui_host) {
                    m_editor_windows->viewport_menu(imgui_host);
                }
            );
        }

#if defined(ERHE_XR_LIBRARY_OPENXR)
        if (m_editor_context.OpenXR) {
            // TODO Create windows directly to correct viewport?
            // Move all imgui windows that have window viewport to hud viewport
            const auto viewport = m_hud->get_rendertarget_imgui_viewport();
            if (viewport) {
                auto& windows = m_imgui_windows->get_windows();
                for (auto window : windows) {
                    if (window->get_imgui_host() == window_viewport.get()) {
                        window->set_imgui_host(viewport.get());
                    }
                }
            }
        }
#endif
        m_tools->set_priority_tool(m_physics_tool.get());
    }

    ~Editor()
    {
        m_default_scene_browser.reset();
        m_default_scene.reset();
    }
    void fill_editor_context()
    {
        ERHE_PROFILE_FUNCTION();

        m_editor_context.commands               = m_commands              .get();
        m_editor_context.graphics_instance      = m_graphics_instance     .get();
        m_editor_context.imgui_renderer         = m_imgui_renderer        .get();
        m_editor_context.imgui_windows          = m_imgui_windows         .get();
#if defined(ERHE_PHYSICS_LIBRARY_JOLT)
        m_editor_context.debug_renderer         = m_debug_renderer        .get();
#endif
        m_editor_context.line_renderer          = m_line_renderer         .get();
        m_editor_context.text_renderer          = m_text_renderer         .get();
        m_editor_context.rendergraph            = m_rendergraph           .get();
        m_editor_context.scene_message_bus      = m_scene_message_bus     .get();
        m_editor_context.forward_renderer       = m_forward_renderer      .get();
        m_editor_context.shadow_renderer        = m_shadow_renderer       .get();
        m_editor_context.context_window         = m_context_window        .get();
        m_editor_context.brdf_slice             = m_brdf_slice            .get();
        m_editor_context.brush_tool             = m_brush_tool            .get();
        m_editor_context.clipboard              = m_clipboard             .get();
        m_editor_context.clipboard_window       = m_clipboard_window      .get();
        m_editor_context.create                 = m_create                .get();
        m_editor_context.editor_message_bus     = m_editor_message_bus    .get();
        m_editor_context.editor_rendering       = m_editor_rendering      .get();
        m_editor_context.editor_scenes          = m_editor_scenes         .get();
        m_editor_context.editor_settings        = m_editor_settings       .get();
        m_editor_context.editor_windows         = m_editor_windows        .get();
        m_editor_context.fly_camera_tool        = m_fly_camera_tool       .get();
        m_editor_context.grid_tool              = m_grid_tool             .get();
#if defined(ERHE_XR_LIBRARY_OPENXR)
        m_editor_context.hand_tracker           = m_hand_tracker          .get();
#endif
        m_editor_context.headset_view           = m_headset_view          .get();
        m_editor_context.hotbar                 = m_hotbar                .get();
        m_editor_context.hud                    = m_hud                   .get();
        m_editor_context.icon_set               = m_icon_set              .get();
        m_editor_context.id_renderer            = m_id_renderer           .get();
        m_editor_context.input_state            = m_input_state           .get();
        m_editor_context.material_paint_tool    = m_material_paint_tool   .get();
        m_editor_context.material_preview       = m_material_preview      .get();
        m_editor_context.mesh_memory            = m_mesh_memory           .get();
        m_editor_context.move_tool              = m_move_tool             .get();
        m_editor_context.operation_stack        = m_operation_stack       .get();
        m_editor_context.paint_tool             = m_paint_tool            .get();
        m_editor_context.physics_tool           = m_physics_tool          .get();
        m_editor_context.post_processing        = m_post_processing       .get();
        m_editor_context.programs               = m_programs              .get();
        m_editor_context.rotate_tool            = m_rotate_tool           .get();
        m_editor_context.scale_tool             = m_scale_tool            .get();
        m_editor_context.scene_builder          = m_scene_builder         .get();
        m_editor_context.scene_commands         = m_scene_commands        .get();
        m_editor_context.selection              = m_selection             .get();
        m_editor_context.selection_tool         = m_selection_tool        .get();
        m_editor_context.settings_window        = m_settings_window       .get();
        m_editor_context.time                   = m_time                  .get();
        m_editor_context.tools                  = m_tools                 .get();
        m_editor_context.transform_tool         = m_transform_tool        .get();
        m_editor_context.viewport_config_window = m_viewport_config_window.get();
        m_editor_context.scene_views            = m_viewport_scene_views  .get();
    }

    auto on_key_event(const erhe::window::Input_event& input_event) -> bool override
    {
        m_input_state->shift   = erhe::bit::test_all_rhs_bits_set(input_event.u.key_event.modifier_mask, erhe::window::Key_modifier_bit_shift);
        m_input_state->control = erhe::bit::test_all_rhs_bits_set(input_event.u.key_event.modifier_mask, erhe::window::Key_modifier_bit_ctrl);
        m_input_state->alt     = erhe::bit::test_all_rhs_bits_set(input_event.u.key_event.modifier_mask, erhe::window::Key_modifier_bit_menu);
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
            m_context_window->poll_events(wait_time);
            {
                ERHE_PROFILE_SCOPE("dispatch events");
                auto& input_events = m_context_window->get_input_events();
                for (erhe::window::Input_event& input_event : input_events) {
                    dispatch_input_event(input_event);
                }
            }
            tick();

            ERHE_PROFILE_FRAME_END
        }
        m_run_stopped = true;
    }

    bool m_close_requested{false};
    bool m_run_started    {false};
    bool m_run_stopped    {false};


    std::unique_ptr<tf::Executor>       m_executor;

    Editor_context                      m_editor_context;

    std::shared_ptr<Scene_root>         m_default_scene;
    std::shared_ptr<Item_tree_window>   m_default_scene_browser;

    // No dependencies (constructors)
    std::unique_ptr<erhe::commands::Commands      > m_commands;
    std::unique_ptr<erhe::scene::Scene_message_bus> m_scene_message_bus;
    std::unique_ptr<Editor_message_bus            > m_editor_message_bus;
    std::unique_ptr<Input_state                   > m_input_state;
    std::unique_ptr<Time                          > m_time;

    std::unique_ptr<Clipboard                              > m_clipboard;
    std::unique_ptr<erhe::window::Context_window           > m_context_window;
    std::unique_ptr<Editor_settings                        > m_editor_settings;
    std::unique_ptr<erhe::graphics::Instance               > m_graphics_instance;
    std::unique_ptr<erhe::imgui::Imgui_renderer            > m_imgui_renderer;
    std::unique_ptr<erhe::renderer::Line_renderer          > m_line_renderer;
    std::unique_ptr<erhe::scene_renderer::Program_interface> m_program_interface;
    std::unique_ptr<erhe::rendergraph::Rendergraph         > m_rendergraph;
    std::unique_ptr<erhe::renderer::Text_renderer          > m_text_renderer;
#if defined(ERHE_PHYSICS_LIBRARY_JOLT)
    std::unique_ptr<erhe::renderer::Debug_renderer         > m_debug_renderer;
#endif
    erhe::dataformat::Vertex_format                          m_vertex_format;

    std::unique_ptr<Programs                              >  m_programs;
    std::unique_ptr<erhe::scene_renderer::Forward_renderer>  m_forward_renderer;
    std::unique_ptr<erhe::scene_renderer::Shadow_renderer >  m_shadow_renderer;
    std::unique_ptr<Mesh_memory                           >  m_mesh_memory;

    std::unique_ptr<erhe::imgui::Imgui_windows>              m_imgui_windows;
    std::unique_ptr<Editor_scenes             >              m_editor_scenes;
    std::unique_ptr<Editor_windows            >              m_editor_windows;

    std::unique_ptr<Asset_browser                   >        m_asset_browser;
    std::unique_ptr<Icon_set                        >        m_icon_set;
    std::unique_ptr<Post_processing                 >        m_post_processing;
    std::unique_ptr<Id_renderer                     >        m_id_renderer;
    std::unique_ptr<Composer_window                 >        m_composer_window;
    std::unique_ptr<Selection_window                >        m_selection_window;
    std::unique_ptr<Settings_window                 >        m_settings_window;
    std::unique_ptr<Scene_views                     >        m_viewport_scene_views;
    std::unique_ptr<Editor_rendering                >        m_editor_rendering;
    std::unique_ptr<Selection                       >        m_selection;
    std::unique_ptr<Operation_stack                 >        m_operation_stack;
    std::unique_ptr<Scene_commands                  >        m_scene_commands;
    std::unique_ptr<Clipboard_window                >        m_clipboard_window;
    std::unique_ptr<Commands_window                 >        m_commands_window;
    std::unique_ptr<Layers_window                   >        m_layers_window;
    std::unique_ptr<Network_window                  >        m_network_window;
    std::unique_ptr<Operations                      >        m_operations;
    std::unique_ptr<Physics_window                  >        m_physics_window;
    std::unique_ptr<Post_processing_window          >        m_post_processing_window;
    std::unique_ptr<Properties                      >        m_properties;
    std::unique_ptr<Rendergraph_window              >        m_rendergraph_window;
    std::unique_ptr<Tool_properties_window          >        m_tool_properties_window;
    std::unique_ptr<Viewport_config_window          >        m_viewport_config_window;
    std::unique_ptr<erhe::imgui::Logs               >        m_logs;
    std::unique_ptr<erhe::imgui::Log_settings_window>        m_log_settings_window;
    std::unique_ptr<erhe::imgui::Tail_log_window    >        m_tail_log_window;
    std::unique_ptr<erhe::imgui::Frame_log_window   >        m_frame_log_window;
    std::unique_ptr<erhe::imgui::Performance_window >        m_performance_window;
    std::unique_ptr<erhe::imgui::Pipelines          >        m_pipelines;

    std::unique_ptr<Tools            >                       m_tools;
    std::unique_ptr<Scene_builder    >                       m_scene_builder;
    std::unique_ptr<Fly_camera_tool  >                       m_fly_camera_tool;
    std::unique_ptr<Headset_view     >                       m_headset_view;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    std::unique_ptr<erhe::xr::Headset>                       m_headset;
    std::unique_ptr<Hand_tracker     >                       m_hand_tracker;
#endif
    std::unique_ptr<Move_tool           >                    m_move_tool;
    std::unique_ptr<Rotate_tool         >                    m_rotate_tool;
    std::unique_ptr<Scale_tool          >                    m_scale_tool;
    std::unique_ptr<Transform_tool      >                    m_transform_tool;
    std::unique_ptr<Hud                 >                    m_hud;
    std::unique_ptr<Hotbar              >                    m_hotbar;
    std::unique_ptr<Hover_tool          >                    m_hover_tool;
    std::unique_ptr<Brdf_slice          >                    m_brdf_slice;
    std::unique_ptr<Debug_draw          >                    m_debug_draw;
    std::unique_ptr<Debug_view_window   >                    m_debug_view_window;
    std::unique_ptr<Debug_visualizations>                    m_debug_visualizations;
    std::unique_ptr<Material_preview    >                    m_material_preview;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    ////Theremin                                m_theremin;
#endif

    std::unique_ptr<Brush_tool         >                     m_brush_tool;
    std::unique_ptr<Create             >                     m_create;
    std::unique_ptr<Grid_tool          >                     m_grid_tool;
    std::unique_ptr<Material_paint_tool>                     m_material_paint_tool;
    std::unique_ptr<Paint_tool         >                     m_paint_tool;
    std::unique_ptr<Physics_tool       >                     m_physics_tool;
    std::unique_ptr<Selection_tool     >                     m_selection_tool;
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
        erhe::dataformat::initialize_logging();
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
    erhe::physics::initialize_physics_system();

    {
        ERHE_PROFILE_SCOPE("initialize geogram");
        GEO::initialize(GEO::GEOGRAM_INSTALL_NONE);
        GEO::CmdLine::import_arg_group("algo");
        // TODO
        // GEO::Logger::unregister_all_clients();
        // GEO::Logger::register_client(s_geogram_logger_client.get());
    }

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

        //for (std::size_t i = 0; i < 20; ++i) {
        //    log_startup->info("Stress test iteration {}", i);
        //    Editor editor{};
        //    editor.tick();
        //}

        Editor editor{};
        editor.run();
    }
}

} // namespace editor
