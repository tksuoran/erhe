// Using llvm pipe appears to have broken GL context sharing at least with what I do here.
#define ERHE_SERIAL_INIT 1

#if !defined(ERHE_SERIAL_INIT)
# define ERHE_PARALLEL_INIT 1
#endif

#include "editor.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "app_message_bus.hpp"
#include "app_rendering.hpp"
#include "app_scenes.hpp"
#include "app_settings.hpp"
#include "app_windows.hpp"
#include "content_library/material_library.hpp"
#include "input_state.hpp"
#include "time.hpp"

#include "animation/timeline_window.hpp"
#include "asset_browser/asset_browser.hpp"
#include "brushes/brush.hpp"
#include "content_library/brdf_slice.hpp"
#include "developer/clipboard_window.hpp"
#include "developer/commands_window.hpp"
#include "developer/composer_window.hpp"
#include "developer/depth_visualization_window.hpp"
#include "developer/icon_browser.hpp"
#include "developer/layers_window.hpp"
#include "developer/post_processing_window.hpp"
#include "developer/rendergraph_window.hpp"
#include "developer/selection_window.hpp"
#include "developer/tool_properties_window.hpp"
#include "experiments/gradient_editor.hpp"
#include "experiments/network_window.hpp"
#include "experiments/sheet_window.hpp"
#include "graph/graph_window.hpp"
#include "graph/node_properties.hpp"
#include "graphics/icon_set.hpp"
#include "graphics/thumbnails.hpp"
#include "operations/operation_stack.hpp"
#include "operations/operations_window.hpp"
#include "physics/physics_window.hpp"
#include "preview/brush_preview.hpp"
#include "preview/material_preview.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "rendergraph/post_processing.hpp"
#include "rendertarget_imgui_host.hpp"
#include "scene/debug_draw.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_views.hpp"
#include "tools/clipboard.hpp"
#include "tools/tools.hpp"
#include "transform/move_tool.hpp"
#include "transform/rotate_tool.hpp"
#include "transform/scale_tool.hpp"
#include "windows/properties.hpp"
#include "windows/settings_window.hpp"
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
#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_gl/gl_helpers.hpp"
# include "erhe_gl/gl_log.hpp"
# include "erhe_gl/wrapper_functions.hpp"
#endif
#include "erhe_gltf/gltf_log.hpp"
#include "erhe_graph/graph_log.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_imgui/imgui_log.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/window_imgui_host.hpp"
#include "erhe_imgui/windows/log_window.hpp"
#include "erhe_imgui/windows/performance_window.hpp"
#include "erhe_imgui/windows/pipelines.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_log/log.hpp"
#include "erhe_math/math_log.hpp"
#include "erhe_net/net_log.hpp"
#include "erhe_physics/physics_log.hpp"
#include "erhe_physics/iworld.hpp"
#if defined(ERHE_PHYSICS_LIBRARY_JOLT) && defined(JPH_DEBUG_RENDERER)
#   include "erhe_renderer/jolt_debug_renderer.hpp"
#endif
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_renderer/text_renderer.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_scene/scene_message_bus.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_scene_renderer/texel_renderer.hpp"
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

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

#if defined(ERHE_OS_LINUX)
#   include <unistd.h>
#   include <limits.h>
#endif

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
    std::mutex m_mutex;
    void tick()
    {
        std::lock_guard<std::mutex> lock{m_mutex};

        ERHE_PROFILE_FUNCTION();
        m_frame_log_window->on_frame_begin();

        erhe::graphics::Frame_state frame_state{};
        const bool wait_ok = m_graphics_device->wait_frame(frame_state);
        ERHE_VERIFY(wait_ok);

        // log_input_frame->trace("----------------------- Editor::tick() -----------------------");

        std::vector<erhe::window::Input_event>& input_events = m_window->get_input_events();

        m_time->prepare_update();
        m_time->update_transform_animations(*m_app_message_bus.get());
        m_fly_camera_tool->on_frame_begin();

        // Updating pointer is probably sufficient to be done once per frame
        if (!m_app_context.OpenXR) { //if (!m_headset_view.is_active()) {
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
        if (m_app_context.OpenXR) {
            ERHE_PROFILE_SCOPE("OpenXR update events");
            bool headset_poll_ok           = m_headset_view->poll_events();
            bool headset_begin_frame_ok    = headset_poll_ok        && m_headset_view->begin_frame();
            bool headset_update_actions_ok = headset_begin_frame_ok && m_headset_view->update_actions();
            if (headset_update_actions_ok) {
                m_viewport_config_window->set_edit_data(&m_headset_view->get_config());
            } else{
                ERHE_PROFILE_SCOPE("OpenXR sleep");
                // Throttle loop since xrWaitFrame won't be called.
                std::this_thread::sleep_for(std::chrono::milliseconds{250});
            }
        }
#endif

        m_app_scenes->before_physics_simulation_steps();

        float host_system_dt_s = 0.0f;
        int64_t host_system_time_ns = 0;
        m_time->for_each_fixed_step(
            [this, &input_events, &host_system_dt_s, &host_system_time_ns](const Time_context& time_context) {
                host_system_dt_s += time_context.host_system_dt_s;
                host_system_time_ns = time_context.host_system_time_ns;
                m_headset_view   ->update_fixed_step();
                m_fly_camera_tool->update_fixed_step(time_context);
                m_app_scenes     ->update_physics_simulation_fixed_step(time_context);
            }
        );
        m_app_scenes   ->after_physics_simulation_steps();
        m_imgui_windows->process_events(host_system_dt_s, host_system_time_ns);
        m_commands     ->tick(host_system_time_ns, input_events);

        // Once per frame updates
        m_network_window->update_network();

        // - Update all ImGui hosts. glfw window host processes input events, converting them to ImGui inputs events
        //   This may consume some input events, so that they will not get processed by m_commands.tick() below
        // - Call all ImGui code (Imgui_window)
        m_hover_tool->reset_item_tree_hover();

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
        //    - App_scenes (updates physics)
        //    - Fly_camera_tool
        //    - Network_window 
        m_app_message_bus->update(); // Flushes queued messages

        // Apply physics updates

        m_fly_camera_tool->on_frame_end();

        // Rendering
        m_graphics_device->get_shader_monitor().update_once_per_frame();
        m_mesh_memory->buffer_transfer_queue.flush();

        const erhe::graphics::Frame_begin_info frame_begin_info{
            .resize_width   = static_cast<uint32_t>(m_last_window_width),
            .resize_height  = static_cast<uint32_t>(m_last_window_height),
            .request_resize = m_request_resize_pending.load()
        };
        m_request_resize_pending.store(false);

        const bool begin_frame_ok = m_graphics_device->begin_frame(frame_begin_info);
        ERHE_VERIFY(begin_frame_ok);

        m_app_rendering->begin_frame(); // tests renderdoc capture start

        m_thumbnails->update();

        // Update scene transforms
        m_tools->update_transforms();
        m_viewport_scene_views->update_transforms();
        if (m_app_context.OpenXR) {
            m_headset_view->update_transforms();
        }

        // Execute rendergraph
        m_rendergraph->execute();

        m_imgui_renderer->next_frame();
        m_app_rendering->end_frame();

        const erhe::graphics::Frame_end_info frame_end_info{
            .requested_display_time = 0
        };
        const bool end_frame_ok = m_graphics_device->end_frame(frame_end_info);
        ERHE_VERIFY(end_frame_ok);

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
        //if (!m_app_context.OpenXR) {
        //    gl::bind_framebuffer(gl::Framebuffer_target::framebuffer, 0);
        //    if (m_app_context.use_sleep) {
        //        std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
        //        {
        //            ERHE_PROFILE_SCOPE("swap_buffers");
        //            m_window->swap_buffers();
        //        }
        //        std::chrono::steady_clock::time_point end_time = std::chrono::steady_clock::now();
        //        const std::chrono::duration<float> swap_duration = start_time - end_time;
        //        const std::chrono::duration<float> sleep_margin{m_app_context.sleep_margin};
        //        if (swap_duration > sleep_margin) {
        //            ERHE_PROFILE_SCOPE("sleep");
        //            erhe::time::sleep_for(swap_duration - sleep_margin);
        //        }
        //    } else {
        //        ERHE_PROFILE_SCOPE("swap_buffers");
        //        m_window->swap_buffers();
        //    }
        //}
        // TODO move this logic to m_graphics_device->end_of_frame();
#endif // TODO
        m_frame_log_window->on_frame_end();
    }

    [[nodiscard]] static auto get_windows_ini_path() -> std::string
    {
        bool openxr{false};
        const auto& heaset = erhe::configuration::get_ini_file_section(erhe::c_erhe_config_file_path, "headset");
        heaset.get("openxr", openxr);
        return openxr ? "openxr_windows.toml" : "windows.toml";
    }

    [[nodiscard]] static auto conditionally_enable_window_imgui_host(erhe::window::Context_window* context_window)
    {
        bool openxr{false};
        const auto& heaset = erhe::configuration::get_ini_file_section(erhe::c_erhe_config_file_path, "headset");
        heaset.get("openxr", openxr);
        return openxr ? nullptr : context_window;
    }

    [[nodiscard]] auto create_window() -> std::unique_ptr<erhe::window::Context_window>
    {
        auto& erhe_ini = erhe::configuration::get_ini_file(erhe::c_erhe_config_file_path);

        const auto& headset_section = erhe_ini.get_section("headset");
        headset_section.get("openxr",        m_app_context.OpenXR);
        headset_section.get("openxr_mirror", m_app_context.OpenXR_mirror);

        const auto& developer_section = erhe_ini.get_section("developer");
        developer_section.get("enable", m_app_context.developer_mode);

        const auto& renderdoc_section = erhe_ini.get_section("renderdoc");
        renderdoc_section.get("capture_support", m_app_context.renderdoc);
        if (m_app_context.renderdoc) {
            m_app_context.developer_mode = true;
        }

        const auto& window_section = erhe_ini.get_section("window");
        window_section.get("use_sleep",    m_app_context.use_sleep);
        window_section.get("sleep_margin", m_app_context.sleep_margin);

        if (m_app_context.OpenXR) {
            m_app_context.sleep_margin = 0.0f;
        }

        erhe::window::Window_configuration configuration{
            .use_depth         = m_app_context.OpenXR_mirror,
            .use_stencil       = m_app_context.OpenXR_mirror,
            .msaa_sample_count = m_app_context.OpenXR_mirror ? 0 : 0,
            .size              = glm::ivec2{1920, 1080},
            .title             = erhe::window::format_window_title("erhe editor by Timo Suoranta")
        };

        window_section.get("show",             configuration.show);
        window_section.get("fullscreen",       configuration.fullscreen);
        window_section.get("use_transparency", configuration.framebuffer_transparency);
#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
        window_section.get("gl_major",         configuration.gl_major);
        window_section.get("gl_minor",         configuration.gl_minor);
#endif
        window_section.get("size",             configuration.size);
        window_section.get("swap_interval",    configuration.swap_interval);
        window_section.get("enable_joystick",  configuration.enable_joystick);

        if (m_app_context.OpenXR) {
            configuration.swap_interval = 0;
        }

        const char* const env_value = std::getenv("LIBGL_ALWAYS_SOFTWARE");
        if (env_value != nullptr) {
            log_startup->info("Detected LIBGL_ALWAYS_SOFTWARE = {}", env_value);
        } else {
            log_startup->info("Detected LIBGL_ALWAYS_SOFTWARE is not set");
        }

        return std::make_unique<erhe::window::Context_window>(configuration);
    }

    Editor()
    {
        int thread_count = 1;
        auto& erhe_ini = erhe::configuration::get_ini_file(erhe::c_erhe_config_file_path);

        // Note: m_executor is also used at runtime, so it cannot be
        //       skipped even if parallel init is not used.
        const auto& threading_section = erhe_ini.get_section("threading");
        threading_section.get("thread_count", thread_count);

        m_executor = std::make_unique<tf::Executor>(thread_count);

        try {
#if defined(ERHE_PARALLEL_INIT)
            tf::Taskflow taskflow;
# if defined(ERHE_PROFILE_LIBRARY_TRACY)
            std::shared_ptr<Tracy_observer> observer = m_executor->make_observer<Tracy_observer>();
# endif
#endif

            m_commands          = std::make_unique<erhe::commands::Commands      >();
            m_scene_message_bus = std::make_unique<erhe::scene::Scene_message_bus>();
            m_app_message_bus   = std::make_unique<App_message_bus               >();
            m_app_settings      = std::make_unique<App_settings                  >();
            m_input_state       = std::make_unique<Input_state                   >();
            m_time              = std::make_unique<Time                          >();
            auto& commands        = *m_commands          .get();
            auto& app_message_bus = *m_app_message_bus.get();

#if defined(ERHE_PARALLEL_INIT)
#   define ERHE_GET_GL_CONTEXT erhe::graphics::Scoped_gl_context ctx{m_graphics_device->context_provider};
#   define ERHE_TASK_HEADER(var) auto var = taskflow.emplace([&, this]()
#   define ERHE_TASK_FOOTER(ops) ) ops
#else
#   define ERHE_GET_GL_CONTEXT
#   define ERHE_TASK_HEADER(var)
#   define ERHE_TASK_FOOTER(ops)
#endif

#if defined(ERHE_PARALLEL_INIT)
            m_executor->run(taskflow);
#endif

            // Window and graphics context creation - in main thread
            m_window = create_window();

            // Graphics context state init after window - in main thread
            m_graphics_device = std::make_unique<erhe::graphics::Device>(
                erhe::graphics::Surface_create_info{
                    .context_window            = m_window.get(),
                    .prefer_low_bandwidth      = false,
                    .prefer_high_dynamic_range = false
                }

            );

            m_app_settings->apply_limits(*m_graphics_device.get(), app_message_bus);

#if defined(ERHE_XR_LIBRARY_OPENXR)
            // Apparently it is necessary to create OpenXR session in the main thread / original OpenGL
            // context, instead of using shared context / worker thread
            if (m_app_context.OpenXR) {
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
                const auto& ini = erhe::configuration::get_ini_file_section(erhe::c_erhe_config_file_path, "headset");
                ini.get("quad_view",         configuration.quad_view);
                ini.get("debug",             configuration.debug);
                ini.get("validation",        configuration.validation);
                ini.get("api_dump",          configuration.api_dump);
                ini.get("depth",             configuration.depth);
                ini.get("visibility_mask",   configuration.visibility_mask);
                ini.get("hand_tracking",     configuration.hand_tracking);
                ini.get("composition_alpha", configuration.composition_alpha);
                ini.get("passthrough_fb",    configuration.passthrough_fb);
                configuration.mirror_mode = m_app_context.OpenXR_mirror;
                m_headset = std::make_unique<erhe::xr::Headset>(*m_window.get(), configuration);
                if (!m_headset->is_valid()) {
                    log_headset->info("Headset initialization failed. Disabling OpenXR.");
                    m_app_context.OpenXR = false;
                    m_headset.reset();
                }
            }
#endif

            // It seems to be faster to create the worker thread here instead of between
            // executor run and wait.
#if defined(ERHE_PARALLEL_INIT)
            m_graphics_device->context_provider.provide_worker_contexts(
                m_window.get(),
                8u,
                []() -> bool { return true; }
            );
#endif

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
                        { erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::normal,    1}, // wireframe bias requires smooth normal attribute
                        { erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::tangent,   0},
                        // { erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::bitangent, 0},
                        { erhe::dataformat::Format::format_32_vec2_float, erhe::dataformat::Vertex_attribute_usage::tex_coord, 0},
                        { erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::color,     0},
                        { erhe::dataformat::Format::format_8_vec2_unorm,  erhe::dataformat::Vertex_attribute_usage::custom,    erhe::dataformat::custom_attribute_aniso_control},
                        { erhe::dataformat::Format::format_16_vec2_uint,  erhe::dataformat::Vertex_attribute_usage::custom,    erhe::dataformat::custom_attribute_valency_edge_count},
                        { erhe::dataformat::Format::format_8_vec3_unorm,  erhe::dataformat::Vertex_attribute_usage::custom,    erhe::dataformat::custom_attribute_id}
                    }
                }
            };

            m_clipboard            = std::make_unique<Clipboard     >(commands, m_app_context, app_message_bus);
            m_app_scenes           = std::make_unique<App_scenes    >(m_app_context);
            m_app_windows          = std::make_unique<App_windows   >(m_app_context, commands);
            m_viewport_scene_views = std::make_unique<Scene_views   >(commands, m_app_context, app_message_bus);
            m_selection            = std::make_unique<Selection     >(commands, m_app_context, app_message_bus);
            m_scene_commands       = std::make_unique<Scene_commands>(commands, m_app_context);
            m_debug_draw           = std::make_unique<Debug_draw    >(m_app_context);
            m_program_interface    = std::make_unique<erhe::scene_renderer::Program_interface>(*m_graphics_device.get(), m_vertex_format);
            m_programs             = std::make_unique<Programs>(*m_graphics_device.get());

            ERHE_TASK_HEADER(programs_load_task)
            {
                ERHE_GET_GL_CONTEXT
                m_programs->load_programs(*m_executor.get(), *m_graphics_device.get(), *m_program_interface.get());
            }
            ERHE_TASK_FOOTER( .name("Programs (load)") );

            ERHE_TASK_HEADER(imgui_renderer_task)
            {
                ERHE_GET_GL_CONTEXT
                m_imgui_renderer = std::make_unique<erhe::imgui::Imgui_renderer>(*m_graphics_device.get(), m_app_settings->imgui);
            }
            ERHE_TASK_FOOTER( .name("Imgui_renderer") );

            ERHE_TASK_HEADER(debug_renderer_task)
            {
                ERHE_GET_GL_CONTEXT
                m_debug_renderer = std::make_unique<erhe::renderer::Debug_renderer>(*m_graphics_device.get());
            }
            ERHE_TASK_FOOTER( .name("Debug_renderer") );

            ERHE_TASK_HEADER(thumbnails_task)
            {
                ERHE_GET_GL_CONTEXT
                m_thumbnails = std::make_unique<Thumbnails>(*m_graphics_device.get(), m_app_context);
            }
            ERHE_TASK_FOOTER( .name("Thumbnails") );

            ERHE_TASK_HEADER(rendergraph_task)
            {
                ERHE_GET_GL_CONTEXT
                m_rendergraph = std::make_unique<erhe::rendergraph::Rendergraph>(*m_graphics_device.get());
            }
            ERHE_TASK_FOOTER( .name("Rendergraph") );

#if defined(ERHE_PHYSICS_LIBRARY_JOLT) && defined(JPH_DEBUG_RENDERER)
            ERHE_TASK_HEADER(jolt_debug_renderer_task)
            {
                ERHE_GET_GL_CONTEXT
                m_jolt_debug_renderer = std::make_unique<erhe::renderer::Jolt_debug_renderer>(*m_debug_renderer.get());
            }
            ERHE_TASK_FOOTER( .name("Jolt_debug_renderer").succeed(debug_renderer_task) );
#endif

            ERHE_TASK_HEADER(text_renderer_task)
            {
                ERHE_GET_GL_CONTEXT
                m_text_renderer = std::make_unique<erhe::renderer::Text_renderer>(*m_graphics_device.get());
            }
            ERHE_TASK_FOOTER( .name("Text_renderer") );

            ERHE_TASK_HEADER(forward_renderer_task)
            {
                ERHE_GET_GL_CONTEXT
                m_forward_renderer = std::make_unique<erhe::scene_renderer::Forward_renderer>(*m_graphics_device.get(), *m_program_interface.get());
            }
            ERHE_TASK_FOOTER( .name("Forward_renderer") );

            ERHE_TASK_HEADER(shadow_renderer_task)
            {
                ERHE_GET_GL_CONTEXT
                m_shadow_renderer = std::make_unique<erhe::scene_renderer::Shadow_renderer >(*m_graphics_device.get(), *m_program_interface.get());
            }
            ERHE_TASK_FOOTER( .name("Shadow_renderer") );

            ERHE_TASK_HEADER(mesh_memory_task)
            {
                ERHE_GET_GL_CONTEXT
                m_mesh_memory = std::make_unique<Mesh_memory>(*m_graphics_device.get(), m_vertex_format);
            }
            ERHE_TASK_FOOTER( .name("Mesh_memory") );

            ERHE_TASK_HEADER(imgui_windows_task)
            {
                m_imgui_windows = std::make_unique<erhe::imgui::Imgui_windows>(
                    *m_imgui_renderer.get(),
                    *m_graphics_device.get(),
                    *m_rendergraph.get(),
                    conditionally_enable_window_imgui_host(m_window.get()),
                    get_windows_ini_path()
                );
            }
            ERHE_TASK_FOOTER( .name("Imgui_windows") .succeed(imgui_renderer_task, rendergraph_task) );

            ERHE_TASK_HEADER(icon_set_task)
            {
                ERHE_GET_GL_CONTEXT
                m_icon_set = std::make_unique<Icon_set>(m_app_context, *m_imgui_renderer.get());
            }
            ERHE_TASK_FOOTER( .name("Icon_set") .succeed(imgui_renderer_task) );

            ERHE_TASK_HEADER(post_processing_task)
            {
                ERHE_GET_GL_CONTEXT
                m_post_processing = std::make_unique<Post_processing>(*m_graphics_device.get(), m_app_context);
            }
            ERHE_TASK_FOOTER( .name("Post_processing") );

            ERHE_TASK_HEADER(id_renderer_task)
            {
                ERHE_GET_GL_CONTEXT
                m_id_renderer = std::make_unique<Id_renderer>(*m_graphics_device.get(), *m_program_interface.get(), *m_mesh_memory.get(), *m_programs.get());
            }
            ERHE_TASK_FOOTER(
                .name("Id_renderer")
                .succeed(mesh_memory_task)
            );

            ERHE_TASK_HEADER(app_rendering_task)
            {
                ERHE_GET_GL_CONTEXT
                m_app_rendering = std::make_unique<App_rendering>(
                    *m_commands.get(),
                    *m_graphics_device.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_mesh_memory.get(),
                    *m_programs.get()
                );
            }
            ERHE_TASK_FOOTER(
                .name("App_rendering")
                .succeed(mesh_memory_task)
            );

            ERHE_TASK_HEADER(some_windows_task)
            {
                m_operation_stack        = std::make_unique<Operation_stack                 >(*m_executor.get(),       *m_commands.get(),       *m_imgui_renderer.get(), *m_imgui_windows.get(), m_app_context);
                m_asset_browser          = std::make_unique<Asset_browser                   >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_composer_window        = std::make_unique<Composer_window                 >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_selection_window       = std::make_unique<Selection_window                >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_settings_window        = std::make_unique<Settings_window                 >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context, *m_app_message_bus.get());
                m_clipboard_window       = std::make_unique<Clipboard_window                >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_commands_window        = std::make_unique<Commands_window                 >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_graph_window           = std::make_unique<Graph_window                    >(*m_commands.get(),       *m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context, *m_app_message_bus.get());
                m_node_properties_window = std::make_unique<Node_properties_window          >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_gradient_editor        = std::make_unique<Gradient_editor                 >(*m_imgui_renderer.get(), *m_imgui_windows.get());
                m_icon_browser           = std::make_unique<Icon_browser                    >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_sheet_window           = std::make_unique<Sheet_window                    >(*m_commands.get(),       *m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context, *m_app_message_bus.get());
                m_timeline_window        = std::make_unique<Timeline_window                 >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_layers_window          = std::make_unique<Layers_window                   >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_network_window         = std::make_unique<Network_window                  >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_operations             = std::make_unique<Operations                      >(*m_commands.get(),       *m_imgui_renderer.get(), *m_imgui_windows.get(), m_app_context, *m_app_message_bus.get());
                m_physics_window         = std::make_unique<Physics_window                  >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_post_processing_window = std::make_unique<Post_processing_window          >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_properties             = std::make_unique<Properties                      >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_rendergraph_window     = std::make_unique<Rendergraph_window              >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_tool_properties_window = std::make_unique<Tool_properties_window          >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_viewport_config_window = std::make_unique<Viewport_config_window          >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_logs                   = std::make_unique<erhe::imgui::Logs               >(*m_commands.get(),       *m_imgui_renderer.get());
                m_log_settings_window    = std::make_unique<erhe::imgui::Log_settings_window>(*m_imgui_renderer.get(), *m_imgui_windows.get(),  *m_logs.get());
                m_tail_log_window        = std::make_unique<erhe::imgui::Tail_log_window    >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  *m_logs.get());
                m_frame_log_window       = std::make_unique<erhe::imgui::Frame_log_window   >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  *m_logs.get());
                m_performance_window     = std::make_unique<erhe::imgui::Performance_window >(*m_imgui_renderer.get(), *m_imgui_windows.get());
                m_pipelines              = std::make_unique<erhe::imgui::Pipelines          >(*m_imgui_renderer.get(), *m_imgui_windows.get());
            }
            ERHE_TASK_FOOTER(
                .name("Some windows")
                .succeed(imgui_renderer_task, imgui_windows_task)
            );

            ERHE_TASK_HEADER(tools_task)
            {
                m_tools = std::make_unique<Tools>(
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    *m_scene_message_bus.get(),
                    m_app_context,
                    *m_app_rendering.get(),
                    *m_app_settings.get(),
                    *m_mesh_memory.get(),
                    *m_programs
                );
                m_fly_camera_tool = std::make_unique<Fly_camera_tool>(
                    *m_commands.get(),
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_tools.get()
                );
            }
            ERHE_TASK_FOOTER(
                .name("Tools")
                .succeed(imgui_renderer_task, imgui_windows_task, mesh_memory_task, app_rendering_task)
            );
        
            ERHE_TASK_HEADER(default_scene_task)
            {
                ERHE_GET_GL_CONTEXT
                auto content_library = std::make_shared<Content_library>();
                add_default_materials(*content_library.get());

                m_default_scene = std::make_shared<Scene_root>(
                    m_imgui_renderer.get(),
                    m_imgui_windows.get(),
                    *m_scene_message_bus.get(),
                    &m_app_context,
                    m_app_message_bus.get(),
                    m_app_scenes.get(),
                    content_library,
                    "Default Scene"
                );
                m_default_scene_browser = m_default_scene->make_browser_window(
                    *m_imgui_renderer.get(), *m_imgui_windows.get(), m_app_context, *m_app_settings.get()
                );
            }
            ERHE_TASK_FOOTER(
                .name("Default scene")
                .succeed(imgui_renderer_task, imgui_windows_task)
            );

            ERHE_TASK_HEADER(scene_builder_task)
            {
                ERHE_GET_GL_CONTEXT
                m_scene_builder = std::make_unique<Scene_builder>(
                    m_default_scene,                //std::shared_ptr<Scene_root>     scene
                    *m_executor.get(),              //tf::Executor&                   executor
                    *m_graphics_device.get(),       //erhe::graphics::Device&         graphics_device
                    *m_imgui_renderer.get(),        //erhe::imgui::Imgui_renderer&    imgui_renderer
                    *m_imgui_windows.get(),         //erhe::imgui::Imgui_windows&     imgui_windows
                    *m_rendergraph.get(),           //erhe::rendergraph::Rendergraph& rendergraph
                    m_app_context,                  //App_context&                    app_context
                    *m_app_message_bus.get(),       //App_message_bus&                app_message_bus
                    *m_app_rendering.get(),         //App_rendering&                  app_rendering
                    *m_app_settings.get(),          //App_settings&                   app_settings
                    *m_mesh_memory.get(),           //Mesh_memory&                    mesh_memory
                    *m_post_processing.get(),       //Post_processing&                post_processing
                    *m_tools.get(),                 //Tools&                          tools
                    *m_viewport_scene_views.get()   //Scene_views&                    scene_views
                );
            }
            ERHE_TASK_FOOTER(
                .name("Scene_builder")
                .succeed(
                    default_scene_task, imgui_renderer_task, imgui_windows_task, rendergraph_task,
                    app_rendering_task, mesh_memory_task, post_processing_task, tools_task
                )
            );

            ERHE_TASK_HEADER(headset_task)
            {
                ERHE_GET_GL_CONTEXT
                m_headset_view = std::make_unique<Headset_view>(
                    *m_commands.get(),
                    *m_graphics_device.get(),
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    *m_rendergraph.get(),
                    *m_window.get(),
#if defined(ERHE_XR_LIBRARY_OPENXR)
                    m_headset.get(),
#endif
                    m_app_context,
                    *m_app_rendering.get(),
                    *m_app_settings.get()
                );
#if defined(ERHE_XR_LIBRARY_OPENXR)
                if (m_app_context.OpenXR) {
                    m_hand_tracker = std::make_unique<Hand_tracker>(m_app_context, *m_app_rendering.get());
                }
#endif
            }
            ERHE_TASK_FOOTER(
                .name("Headset (init)")
                .succeed(imgui_renderer_task, imgui_windows_task, rendergraph_task, app_rendering_task)
            );

            ERHE_TASK_HEADER(headset_attach_task)
            {
#if defined(ERHE_XR_LIBRARY_OPENXR)
                if (m_app_context.OpenXR) {
                    m_headset_view->attach_to_scene(m_default_scene, *m_mesh_memory.get());
                }
#endif
            }
            ERHE_TASK_FOOTER(
                .name("Headset (attach)")
                .succeed(default_scene_task, headset_task, mesh_memory_task, scene_builder_task)
            );

            ERHE_TASK_HEADER(transform_tools_task)
            {
                ERHE_GET_GL_CONTEXT
                m_move_tool   = std::make_unique<Move_tool  >(m_app_context, *m_icon_set.get(), *m_tools.get());
                m_rotate_tool = std::make_unique<Rotate_tool>(m_app_context, *m_icon_set.get(), *m_tools.get());
                m_scale_tool  = std::make_unique<Scale_tool >(m_app_context, *m_icon_set.get(), *m_tools.get());
                m_transform_tool = std::make_unique<Transform_tool>(
                    *m_executor.get(),
                    *m_commands.get(),
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_headset_view.get(),
                    *m_mesh_memory.get(),
                    *m_tools.get()
                );
            }
            ERHE_TASK_FOOTER(
                .name("Transform tools")
                .succeed(imgui_renderer_task, imgui_windows_task, headset_task, mesh_memory_task, icon_set_task, tools_task)
            );

            ERHE_TASK_HEADER(group_1)
            {
                ERHE_GET_GL_CONTEXT
                m_hud = std::make_unique<Hud>(
                    *m_commands.get(),
                    *m_graphics_device.get(),
                    *m_imgui_renderer.get(),
                    *m_rendergraph.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_app_windows.get(),
                    *m_headset_view.get(),
                    *m_mesh_memory.get(),
                    *m_scene_builder.get(),
                    *m_tools.get()
                );
                m_hotbar = std::make_unique<Hotbar>(
                    *m_commands.get(),
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_headset_view.get(),
                    *m_mesh_memory.get(),
                    *m_scene_builder.get(),
                    *m_tools.get()
                );
                m_hover_tool = std::make_unique<Hover_tool>(
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_tools.get()
                );
                m_brdf_slice = std::make_unique<Brdf_slice>(
                    *m_rendergraph.get(),
                    *m_forward_renderer.get(),
                    m_app_context,
                    *m_programs.get()
                );
                m_debug_view_window = std::make_unique<Depth_visualization_window>(
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    *m_rendergraph.get(),
                    *m_forward_renderer.get(),
                    m_app_context,
                    *m_app_rendering.get(),
                    *m_mesh_memory.get(),
                    *m_programs.get()
                );
                m_debug_visualizations = std::make_unique<Debug_visualizations>(
                    *m_graphics_device.get(),
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    *m_program_interface.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_app_rendering.get(),
                    *m_programs.get()
                );
            }
            ERHE_TASK_FOOTER(
                .name("Group 1")
                .succeed(
                    imgui_renderer_task,
                    imgui_windows_task,
                    app_rendering_task,
                    mesh_memory_task,
                    rendergraph_task,
                    forward_renderer_task,
                    tools_task,
                    scene_builder_task,
                    headset_task
                )
            );

            ERHE_TASK_HEADER(material_preview_task)
            {
                ERHE_GET_GL_CONTEXT
                m_material_preview = std::make_unique<Material_preview>(
                    *m_graphics_device.get(),
                    *m_scene_message_bus.get(),
                    m_app_context,
                    *m_mesh_memory.get(),
                    *m_programs.get()
                );
                m_brush_preview = std::make_unique<Brush_preview>(
                    *m_graphics_device.get(),
                    *m_scene_message_bus.get(),
                    m_app_context,
                    *m_mesh_memory.get(),
                    *m_programs.get()
                );
            }
            ERHE_TASK_FOOTER(
                .name("Material_preview")
                .succeed(mesh_memory_task)
            );

            ERHE_TASK_HEADER(brush_tool_task)
            {
                ERHE_GET_GL_CONTEXT
                m_brush_tool = std::make_unique<Brush_tool>(
                    *m_commands.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_headset_view.get(),
                    *m_icon_set.get(),
                    *m_tools.get()
                );
            }
            ERHE_TASK_FOOTER(
                .name("Brush_tool")
                .succeed(headset_task, icon_set_task, tools_task)
            );

            ERHE_TASK_HEADER(group_2)
            {
                ERHE_GET_GL_CONTEXT
                m_create = std::make_unique<Create>(
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_app_context,
                    *m_tools.get()
                );
                m_grid_tool = std::make_unique<Grid_tool>(
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_app_context,
                    *m_icon_set.get(),
                    *m_tools.get()
                );
                m_material_paint_tool = std::make_unique<Material_paint_tool>(
                    *m_commands.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_headset_view.get(),
                    *m_icon_set.get(),
                    *m_tools.get()
                );
                m_paint_tool = std::make_unique<Paint_tool>(
                    *m_commands.get(),
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_headset_view.get(),
                    *m_icon_set.get(),
                    *m_tools.get()
                );
                m_physics_tool = std::make_unique<Physics_tool>(
                    *m_commands.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_headset_view.get(),
                    *m_icon_set.get(),
                    *m_tools.get()
                );
                m_selection_tool = std::make_unique<Selection_tool>(
                    m_app_context,
                    *m_icon_set.get(),
                    *m_tools.get()
                );
            }
            ERHE_TASK_FOOTER(
                .name("Group 2")
                .succeed(imgui_renderer_task, imgui_windows_task, icon_set_task, tools_task, headset_task)
            );

#if defined(ERHE_PARALLEL_INIT)
            std::string graph_dump = taskflow.dump();
            erhe::file::write_file("erhe_init_graph.dot", graph_dump);
            m_executor->run(taskflow).wait();
#endif
        } catch (std::runtime_error& e) {
            log_startup->error("exception: {}", e.what());
        }

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
        m_window->make_current();
#endif
        m_graphics_device->on_thread_enter();

        ERHE_PROFILE_FUNCTION();
#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
        ERHE_PROFILE_GPU_CONTEXT
#endif

#if defined(ERHE_XR_LIBRARY_OPENXR)
        if (m_app_context.OpenXR) {
            m_selection->setup_xr_bindings(*m_commands.get(), *m_headset_view.get());
        }
#endif

        fill_app_context();

        const auto& physics_section = erhe::configuration::get_ini_file_section(erhe::c_erhe_config_file_path, "physics");
        physics_section.get("static_enable",  m_app_settings->physics.static_enable);
        physics_section.get("dynamic_enable", m_app_settings->physics.dynamic_enable);
        if (!m_app_settings->physics.static_enable) {
            m_app_settings->physics.dynamic_enable = false;
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
            m_graph_window          ->set_developer();
            m_gradient_editor       ->set_developer();
            m_node_properties_window->set_developer();
            m_network_window        ->set_developer();
            m_headset_view          ->set_developer();
            m_sheet_window          ->set_developer();

            m_icon_browser          ->set_developer();
            m_layers_window         ->set_developer();
            m_network_window        ->set_developer();
            m_post_processing_window->set_developer();
            m_rendergraph_window    ->set_developer();
            m_selection_window      ->set_developer();
            m_tail_log_window       ->set_developer();
            m_operation_stack       ->set_developer();
        }

        m_hotbar->get_all_tools();

        // Notify ImGui renderer about current font settings
        m_imgui_renderer->on_font_config_changed(m_app_settings->imgui);

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
        gl::clip_control(gl::Clip_control_origin::lower_left, gl::Clip_control_depth::zero_to_one);
        gl::enable      (gl::Enable_cap::framebuffer_srgb);
#endif

        const std::shared_ptr<erhe::imgui::Window_imgui_host>& window_imgui_host = m_imgui_windows->get_window_imgui_host();
        if (!m_app_context.OpenXR) {
            window_imgui_host->set_begin_callback(
                [this](erhe::imgui::Imgui_host& imgui_host) {
                    m_app_windows->viewport_menu(imgui_host);
                }
            );
            window_imgui_host->set_status_bar_callback(
                [this](erhe::imgui::Window_imgui_host&) {
                    {
                        std::stringstream ss;
                        ss << fmt::format("{:.2f} ms", m_time->get_frame_time_average_ms());
                        const int running_async_ops = m_app_context.running_async_ops.load();
                        if (running_async_ops > 0) {
                            ss << " " << running_async_ops << " running operations";
                        }
                        const int pending_async_ops = m_app_context.pending_async_ops.load();
                        if (pending_async_ops > 0) {
                            if (running_async_ops > 0) {
                                ss << ", ";
                            }
                            ss << pending_async_ops << " queued operations";
                        }
                        ImGui::TextUnformatted(ss.str().c_str());
                        ImGui::SameLine();
                    }
                    struct Input_record
                    {
                        bool shift              {false};
                        bool alt                {false};
                        bool control            {false};
                        bool mouse_button_left  {false};
                        bool mouse_button_right {false};
                        bool mouse_button_middle{false};
                        bool mouse_button_x1    {false};
                        bool mouse_button_x2    {false};
                        bool mouse_drag_left    {false};
                        bool mouse_drag_right   {false};
                        bool mouse_drag_middle  {false};
                        bool mouse_drag_x1      {false};
                        bool mouse_drag_x2      {false};
                    };
                    Input_record r{
                        .shift               = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift),
                        .alt                 = ImGui::IsKeyDown(ImGuiKey_LeftAlt)   || ImGui::IsKeyDown(ImGuiKey_RightAlt),
                        .control             = ImGui::IsKeyDown(ImGuiKey_LeftCtrl)  || ImGui::IsKeyDown(ImGuiKey_RightCtrl),
                        .mouse_button_left   = ImGui::IsMouseDown(ImGuiMouseButton_Left),
                        .mouse_button_right  = ImGui::IsMouseDown(ImGuiMouseButton_Right),
                        .mouse_button_middle = ImGui::IsMouseDown(ImGuiMouseButton_Middle),
                        .mouse_button_x1     = ImGui::IsMouseDown(3),
                        .mouse_button_x2     = ImGui::IsMouseDown(4),
                        .mouse_drag_left     = ImGui::IsMouseDragging(ImGuiMouseButton_Left),
                        .mouse_drag_right    = ImGui::IsMouseDragging(ImGuiMouseButton_Right),
                        .mouse_drag_middle   = ImGui::IsMouseDragging(ImGuiMouseButton_Middle),
                        .mouse_drag_x1       = ImGui::IsMouseDragging(3),
                        .mouse_drag_x2       = ImGui::IsMouseDragging(4)
                    };
                    if (r.shift) {
                        ImGui::Button("Shift");
                        ImGui::SameLine(0.0f, 10.0f);
                    }
                    if (r.alt) {
                        ImGui::Button("Alt");
                        ImGui::SameLine(0.0f, 10.0f);
                    }
                    if (r.control) {
                        ImGui::Button("Control");
                        ImGui::SameLine(0.0f, 10.0f);
                    }
                    if (r.mouse_drag_left) {
                        m_icon_set->draw_icon(m_icon_set->icons.mouse_lmb_drag, glm::vec4{1.0f, 1.0f, 1.0f, 1.0}, m_icon_set->custom_icons);
                        ImGui::Button("Dragging Left Mouse Button");
                        ImGui::SameLine(0.0f, 10.0f);
                    } else if (r.mouse_button_left) {
                        m_icon_set->draw_icon(m_icon_set->icons.mouse_lmb, glm::vec4{1.0f, 1.0f, 1.0f, 1.0}, m_icon_set->custom_icons);
                        ImGui::Button("Pressing Left Mouse Button");
                        ImGui::SameLine(0.0f, 10.0f);
                    }

                    if (r.mouse_drag_middle) {
                        m_icon_set->draw_icon(m_icon_set->icons.mouse_mmb_drag, glm::vec4{1.0f, 1.0f, 1.0f, 1.0}, m_icon_set->custom_icons);
                        ImGui::Button("Dragging Middle Mouse Button");
                        ImGui::SameLine(0.0f, 10.0f);
                    } else if (r.mouse_button_middle) {
                        m_icon_set->draw_icon(m_icon_set->icons.mouse_mmb, glm::vec4{1.0f, 1.0f, 1.0f, 1.0}, m_icon_set->custom_icons);
                        ImGui::Button("Pressing Middle Mouse Button");
                        ImGui::SameLine(0.0f, 10.0f);
                    }

                    if (r.mouse_drag_right) {
                        m_icon_set->draw_icon(m_icon_set->icons.mouse_rmb_drag, glm::vec4{1.0f, 1.0f, 1.0f, 1.0}, m_icon_set->custom_icons);
                        ImGui::Button("Dragging Right Mouse Button");
                        ImGui::SameLine(0.0f, 10.0f);
                    } else if (r.mouse_button_right) {
                        m_icon_set->draw_icon(m_icon_set->icons.mouse_rmb, glm::vec4{1.0f, 1.0f, 1.0f, 1.0}, m_icon_set->custom_icons);
                        ImGui::Button("Pressing Right Mouse Button");
                        ImGui::SameLine(0.0f, 10.0f);
                    }
                    if (r.mouse_drag_x1) {
                        ImGui::Button("Dragging Extra-1 Mouse Button");
                        ImGui::SameLine(0.0f, 10.0f);
                    } else if (r.mouse_button_x1) {
                        ImGui::Button("Pressing Extra-1 Mouse Button");
                        ImGui::SameLine(0.0f, 10.0f);
                    }
                    if (r.mouse_drag_x2) {
                        ImGui::Button("Dragging Extra-2 Mouse Button");
                        ImGui::SameLine(0.0f, 10.0f);
                    } else if (r.mouse_button_x2) {
                        ImGui::Button("Extra-2 Mouse Button");
                        ImGui::SameLine(0.0f, 10.0f);
                    }
                }
            );
        }

#if defined(ERHE_XR_LIBRARY_OPENXR)
        if (m_app_context.OpenXR) {
            // TODO Create windows directly to correct viewport?
            // Move all imgui windows that have window viewport to hud viewport
            const auto viewport = m_hud->get_rendertarget_imgui_viewport();
            if (viewport) {
                auto& windows = m_imgui_windows->get_windows();
                for (auto window : windows) {
                    if (window->get_imgui_host() == window_imgui_host.get()) {
                        window->set_imgui_host(viewport.get());
                    }
                }
            }
        }
#endif
        m_tools->set_priority_tool(m_physics_tool.get());

        m_last_window_width  = m_window->get_width();
        m_last_window_height = m_window->get_height();

        m_window->register_redraw_callback(
            [this](){
                if (!m_run_started) {
                    return;
                }
                if (
                    (m_last_window_width  != m_window->get_width ()) ||
                    (m_last_window_height != m_window->get_height())
                ) {
                    m_request_resize_pending.store(true);
                    m_last_window_width  = m_window->get_width();
                    m_last_window_height = m_window->get_height();
                }
                // TODO throttle redraws - if last redraw was less than live resize redraw threshold limit ago, don't redraw
                tick();
            }
        );
    }

    ~Editor() noexcept
    {
        m_default_scene_browser.reset();
        m_default_scene.reset();
    }
    void fill_app_context()
    {
        ERHE_PROFILE_FUNCTION();

        m_app_context.executor               = m_executor.get();

        m_app_context.commands               = m_commands              .get();
        m_app_context.graphics_device        = m_graphics_device       .get();
        m_app_context.imgui_renderer         = m_imgui_renderer        .get();
        m_app_context.imgui_windows          = m_imgui_windows         .get();
#if defined(ERHE_PHYSICS_LIBRARY_JOLT) && defined(JPH_DEBUG_RENDERER)
        m_app_context.jolt_debug_renderer    = m_jolt_debug_renderer   .get();
#endif
        m_app_context.debug_renderer         = m_debug_renderer        .get();
        m_app_context.text_renderer          = m_text_renderer         .get();
        m_app_context.rendergraph            = m_rendergraph           .get();
        m_app_context.scene_message_bus      = m_scene_message_bus     .get();
        m_app_context.forward_renderer       = m_forward_renderer      .get();
        m_app_context.shadow_renderer        = m_shadow_renderer       .get();
        m_app_context.context_window         = m_window                .get();
        m_app_context.brdf_slice             = m_brdf_slice            .get();
        m_app_context.brush_tool             = m_brush_tool            .get();
        m_app_context.clipboard              = m_clipboard             .get();
        m_app_context.clipboard_window       = m_clipboard_window      .get();
        m_app_context.create                 = m_create                .get();
        m_app_context.app_message_bus        = m_app_message_bus       .get();
        m_app_context.app_rendering          = m_app_rendering         .get();
        m_app_context.app_scenes             = m_app_scenes            .get();
        m_app_context.app_settings           = m_app_settings          .get();
        m_app_context.app_windows            = m_app_windows           .get();
        m_app_context.fly_camera_tool        = m_fly_camera_tool       .get();
        m_app_context.grid_tool              = m_grid_tool             .get();
#if defined(ERHE_XR_LIBRARY_OPENXR)
        m_app_context.hand_tracker           = m_hand_tracker          .get();
#endif
        m_app_context.headset_view           = m_headset_view          .get();
        m_app_context.hotbar                 = m_hotbar                .get();
        m_app_context.hud                    = m_hud                   .get();
        m_app_context.icon_set               = m_icon_set              .get();
        m_app_context.id_renderer            = m_id_renderer           .get();
        m_app_context.input_state            = m_input_state           .get();
        m_app_context.material_paint_tool    = m_material_paint_tool   .get();
        m_app_context.material_preview       = m_material_preview      .get();
        m_app_context.brush_preview          = m_brush_preview         .get();
        m_app_context.mesh_memory            = m_mesh_memory           .get();
        m_app_context.move_tool              = m_move_tool             .get();
        m_app_context.operation_stack        = m_operation_stack       .get();
        m_app_context.paint_tool             = m_paint_tool            .get();
        m_app_context.physics_tool           = m_physics_tool          .get();
        m_app_context.post_processing        = m_post_processing       .get();
        m_app_context.programs               = m_programs              .get();
        m_app_context.rotate_tool            = m_rotate_tool           .get();
        m_app_context.scale_tool             = m_scale_tool            .get();
        m_app_context.scene_builder          = m_scene_builder         .get();
        m_app_context.scene_commands         = m_scene_commands        .get();
        m_app_context.selection              = m_selection             .get();
        m_app_context.selection_tool         = m_selection_tool        .get();
        m_app_context.settings_window        = m_settings_window       .get();
        m_app_context.sheet_window           = m_sheet_window          .get();
        m_app_context.thumbnails             = m_thumbnails            .get();
        m_app_context.time                   = m_time                  .get();
        m_app_context.timeline_window        = m_timeline_window       .get();
        m_app_context.tools                  = m_tools                 .get();
        m_app_context.transform_tool         = m_transform_tool        .get();
        m_app_context.viewport_config_window = m_viewport_config_window.get();
        m_app_context.scene_views            = m_viewport_scene_views  .get();
    }

    auto on_key_event(const erhe::window::Input_event& input_event) -> bool override
    {
        m_input_state->shift   = erhe::utility::test_bit_set(input_event.u.key_event.modifier_mask, erhe::window::Key_modifier_bit_shift);
        m_input_state->control = erhe::utility::test_bit_set(input_event.u.key_event.modifier_mask, erhe::window::Key_modifier_bit_ctrl);
        m_input_state->alt     = erhe::utility::test_bit_set(input_event.u.key_event.modifier_mask, erhe::window::Key_modifier_bit_menu);
        return false;
    }

    std::optional<erhe::window::Input_event> m_window_resize_event{};
    int               m_last_window_width     {0};
    int               m_last_window_height    {0};
    uint32_t          m_swapchain_width       {0};
    uint32_t          m_swapchain_height      {0};
    std::atomic<bool> m_request_resize_pending{false};

    auto on_window_resize_event(const erhe::window::Input_event& input_event) -> bool override
    {
        m_window_resize_event = input_event;
        return true;
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
        float wait_time = m_app_context.use_sleep ? m_app_context.sleep_margin : 0.0f;
        // TODO: https://registry.khronos.org/OpenGL/extensions/NV/GLX_NV_delay_before_swap.txt
        // Also:
        //  - Measure time since first swapbuffers
        //  - Count number of swapbuffers
        //  - Wait to avoid presenting frames faster than display refreshrate
        while (!m_close_requested) {
            m_window->poll_events(wait_time);
            {
                ERHE_PROFILE_SCOPE("dispatch events");
                auto& input_events = m_window->get_input_events();
                for (erhe::window::Input_event& input_event : input_events) {
                    dispatch_input_event(input_event);
                }
                if (m_window_resize_event.has_value()) {
                    m_request_resize_pending.store(true);
                    m_window_resize_event.reset();
                    m_last_window_width  = m_window->get_width();
                    m_last_window_height = m_window->get_height();
                }
            }
            tick();

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
            ERHE_PROFILE_FRAME_END
#endif // TODO
        }
        m_run_stopped = true;
    }

    bool m_close_requested{false};
    bool m_run_started    {false};
    bool m_run_stopped    {false};


    std::unique_ptr<tf::Executor>       m_executor;

    App_context                         m_app_context;

    std::shared_ptr<Scene_root>         m_default_scene;
    std::shared_ptr<Item_tree_window>   m_default_scene_browser;

    // No dependencies (constructors)
    std::unique_ptr<erhe::commands::Commands      > m_commands;
    std::unique_ptr<erhe::scene::Scene_message_bus> m_scene_message_bus;
    std::unique_ptr<App_message_bus               > m_app_message_bus;
    std::unique_ptr<Input_state                   > m_input_state;
    std::unique_ptr<Time                          > m_time;

    std::unique_ptr<Clipboard                              > m_clipboard;
    std::unique_ptr<erhe::window::Context_window           > m_window;
    std::unique_ptr<App_settings                           > m_app_settings;
    std::unique_ptr<erhe::graphics::Device                 > m_graphics_device;
    std::unique_ptr<erhe::imgui::Imgui_renderer            > m_imgui_renderer;
    std::unique_ptr<erhe::renderer::Debug_renderer         > m_debug_renderer;
    std::unique_ptr<erhe::scene_renderer::Program_interface> m_program_interface;
    std::unique_ptr<erhe::rendergraph::Rendergraph         > m_rendergraph;
    std::unique_ptr<erhe::renderer::Text_renderer          > m_text_renderer;
#if defined(ERHE_PHYSICS_LIBRARY_JOLT) && defined(JPH_DEBUG_RENDERER)
    std::unique_ptr<erhe::renderer::Jolt_debug_renderer    > m_jolt_debug_renderer;
#endif
    erhe::dataformat::Vertex_format                          m_vertex_format;

    std::unique_ptr<Programs                              >  m_programs;
    std::unique_ptr<erhe::scene_renderer::Forward_renderer>  m_forward_renderer;
    std::unique_ptr<erhe::scene_renderer::Shadow_renderer >  m_shadow_renderer;
    std::unique_ptr<Mesh_memory                           >  m_mesh_memory;

    std::unique_ptr<erhe::imgui::Imgui_windows>              m_imgui_windows;
    std::unique_ptr<App_scenes             >                 m_app_scenes;
    std::unique_ptr<App_windows            >                 m_app_windows;

    std::unique_ptr<Asset_browser                   >        m_asset_browser;
    std::unique_ptr<Icon_set                        >        m_icon_set;
    std::unique_ptr<Post_processing                 >        m_post_processing;
    std::unique_ptr<Id_renderer                     >        m_id_renderer;
    std::unique_ptr<Composer_window                 >        m_composer_window;
    std::unique_ptr<Selection_window                >        m_selection_window;
    std::unique_ptr<Settings_window                 >        m_settings_window;
    std::unique_ptr<Scene_views                     >        m_viewport_scene_views;
    std::unique_ptr<App_rendering                   >        m_app_rendering;
    std::unique_ptr<Selection                       >        m_selection;
    std::unique_ptr<Operation_stack                 >        m_operation_stack;
    std::unique_ptr<Scene_commands                  >        m_scene_commands;
    std::unique_ptr<Clipboard_window                >        m_clipboard_window;
    std::unique_ptr<Commands_window                 >        m_commands_window;
    std::unique_ptr<Graph_window                    >        m_graph_window;
    std::unique_ptr<Node_properties_window          >        m_node_properties_window;
    std::unique_ptr<Gradient_editor                 >        m_gradient_editor;
    std::unique_ptr<Icon_browser                    >        m_icon_browser;
    std::unique_ptr<Thumbnails                      >        m_thumbnails;
    std::unique_ptr<Sheet_window                    >        m_sheet_window;
    std::unique_ptr<Layers_window                   >        m_layers_window;
    std::unique_ptr<Network_window                  >        m_network_window;
    std::unique_ptr<Operations                      >        m_operations;
    std::unique_ptr<Physics_window                  >        m_physics_window;
    std::unique_ptr<Post_processing_window          >        m_post_processing_window;
    std::unique_ptr<Properties                      >        m_properties;
    std::unique_ptr<Rendergraph_window              >        m_rendergraph_window;
    std::unique_ptr<Timeline_window                 >        m_timeline_window;
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
    std::unique_ptr<Depth_visualization_window>              m_debug_view_window;
    std::unique_ptr<Debug_visualizations>                    m_debug_visualizations;
    std::unique_ptr<Material_preview    >                    m_material_preview;
    std::unique_ptr<Brush_preview       >                    m_brush_preview;
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

    // Workaround for
    // https://intellij-support.jetbrains.com/hc/en-us/community/posts/27792220824466-CMake-C-git-project-How-to-share-working-directory-in-git
    erhe::file::ensure_working_directory_contains(erhe::c_erhe_config_file_path);

    {
        ERHE_PROFILE_SCOPE("initialize logging");
#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
        gl::initialize_logging();
        gl_helpers::initialize_logging();
#endif
        erhe::commands::initialize_logging();
        erhe::dataformat::initialize_logging();
        erhe::file::initialize_logging();
        erhe::gltf::initialize_logging();
        erhe::geometry::initialize_logging();
        erhe::graph::initialize_logging();
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
        GEO::CmdLine::set_arg("sys:multithread", "true");
        // TODO
        // GEO::Logger::unregister_all_clients();
        // GEO::Logger::register_client(s_geogram_logger_client.get());
    }

    {
        ERHE_PROFILE_SCOPE("init renderdoc");
        bool enable_renderdoc_capture_support{false};
        const auto& ini = erhe::configuration::get_ini_file_section(erhe::c_erhe_config_file_path, "renderdoc");
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

}
