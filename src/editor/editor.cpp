#include "editor.hpp"

#include "ai_driver.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_version/version.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/display_refresh_resolver.hpp"
#include "app_context.hpp"
#include "benchmark_driver.hpp"
#include "config/generated/add_cameras_args.hpp"
#include "config/generated/add_cameras_args_serialization.hpp"
#include "config/generated/add_lights_args.hpp"
#include "config/generated/add_lights_args_serialization.hpp"
#include "config/generated/add_room_args.hpp"
#include "config/generated/add_room_args_serialization.hpp"
#include "config/generated/make_mesh_args.hpp"
#include "config/generated/make_mesh_args_serialization.hpp"
#include "config/generated/ddgi_config.hpp"
#include "config/generated/indirect_diffuse_source.hpp"
#include "config/generated/radiance_cascades_config.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/editor_settings_config_serialization.hpp"
#include "crash_handler.hpp"
#include "erhe_frame_pacing/frame_pacing_observer.hpp"
#include "erhe_frame_pacing/slop_servo_pacer.hpp"
#include "erhe_scene_renderer/generated/mesh_memory_config.hpp"
#include "erhe_scene_renderer/generated/mesh_memory_config_serialization.hpp"
#include "config/generated/renderer_config.hpp"
#include "config/generated/renderer_config_serialization.hpp"
#include "config/generated/text_renderer_config.hpp"
#include "config/generated/text_renderer_config_serialization.hpp"
#include "config/generated/window_config.hpp"
#include "config/generated/window_config_serialization.hpp"
#include "erhe_codegen/config_io.hpp"
#include "erhe_codegen/migration.hpp"
#include "erhe_graphics/generated/graphics_config.hpp"
#include "erhe_graphics/generated/graphics_config_serialization.hpp"
#include "items.hpp"
#include "editor_default_layout.hpp"
#include "editor_log.hpp"
#include "render_thread.hpp"
#include "editor_settings_store.hpp"
#include "input_bindings_store.hpp"
#include "app_message_bus.hpp"
#include "app_rendering.hpp"
#include "app_scenes.hpp"
#include "app_settings.hpp"
#include "app_windows.hpp"
#include "content_library/content_library.hpp"
#include "content_library/material_library.hpp"
#include "init_status_display.hpp"
#include "input_state.hpp"
#include "task_guard.hpp"
#include "time.hpp"

#include "animation/animation_player.hpp"
#include "animation/animation_window.hpp"
#include "asset_browser/asset_browser.hpp"
#include "assets/asset_load_tick_context.hpp"
#include "assets/asset_manager.hpp"
#include "brushes/brush.hpp"
#include "content_library/brdf_slice.hpp"
#include "developer/clipboard_window.hpp"
#include "developer/commands_window.hpp"
#include "windows/input_bindings_window.hpp"
#include "developer/composer_window.hpp"
#include "developer/depth_visualization_window.hpp"
#include "developer/icon_browser.hpp"
#include "developer/layers_window.hpp"
#include "developer/post_processing_window.hpp"
#include "developer/ddgi_window.hpp"
#include "developer/radiance_cascades_window.hpp"
#include "developer/ray_trace_window.hpp"
#include "developer/rendergraph_window.hpp"
#include "developer/selection_window.hpp"
#include "windows/geometry_spreadsheet_window.hpp"
#include "developer/tool_properties_window.hpp"
#include "experiments/gradient_editor.hpp"
#include "experiments/network_window.hpp"
#include "experiments/sheet_window.hpp"
#include "geometry_graph/geometry_graph_window.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "graph/graph_window.hpp"
#include "graph_editor/graph_editor_palette_window.hpp"
#include "graph/node_properties.hpp"
#include "graphics/icon_set.hpp"
#include "graphics/texture_file_loader.hpp"
#include "graphics/thumbnails.hpp"
#include "operations/operation_stack.hpp"
#include "operations/property_set_operation.hpp"
#include "scene/item_lookup.hpp"
#include "operations/operations_window.hpp"
#include "physics/physics_window.hpp"
#include "preview/brush_preview.hpp"
#include "preview/material_preview.hpp"
#include "renderers/id_renderer.hpp"
#include "erhe_scene_renderer/draw_list_renderer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "renderers/prewarm.hpp"
#include "renderers/render_settings.hpp"
#include "renderers/material_set_factory.hpp"
#include "renderers/programs.hpp"
#include "renderers/lightmap_baker.hpp"
#include "renderers/lightmap_report.hpp"
#include "renderers/lightmap_partitioner.hpp"
#include "renderers/lightmap_streamer.hpp"
#include "renderers/lightmap_tile_io.hpp"
#include "renderers/ddgi_renderer.hpp"
#include "renderers/indirect_diffuse.hpp"
#include "renderers/radiance_cascades_renderer.hpp"
#include "renderers/ray_trace_renderer.hpp"
#include "renderers/sky_renderer.hpp"
#include "rendergraph/post_processing.hpp"
#include "rendertarget_imgui_host.hpp"
#include "scene/draw_mode_renderer.hpp"
#include "prefabs/prefab_library.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_commit_queue.hpp"
#include "scene/scene_payload_release.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_settings_resolve.hpp"
#include "config/generated/sky_config.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "texture_graph/graph_texture.hpp"
#include "texture_graph/texture_graph_properties.hpp"
#include "texture_graph/texture_graph_window.hpp"
#include "tools/bone_visualization.hpp"
#include "tools/weight_display.hpp"
#include "tools/weight_paint_tool.hpp"
#include "tools/clipboard.hpp"
#include "tools/debug_visualizations.hpp"
#include "tools/mesh_component_selection.hpp"
#include "tools/lattice_tool.hpp"
#include "tools/mesh_component_selection_tool.hpp"
#include "tools/navigation_gizmo_tool.hpp"
#include "tools/tools.hpp"
#include "transform/move_tool.hpp"
#include "transform/rotate_tool.hpp"
#include "transform/scale_tool.hpp"
#include "windows/editor_windows.hpp"
#include "windows/controller_inputs_window.hpp"
#include "windows/frame_pacing_window.hpp"
#include "windows/lightmap_texture_window.hpp"
#include "windows/lightmap_window.hpp"
#include "windows/inventory_window.hpp"
#include "windows/properties.hpp"
#include "windows/settings_window.hpp"
#include "windows/transform_update_stats.hpp"
#include "windows/viewport_config_window.hpp"
#include "windows/scene_view_config_window.hpp"

#include "mcp/mcp_server.hpp"
#include "xr/headset_view.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/hand_tracker.hpp"
//#   include "xr/theremin.hpp"
#   include "erhe_xr/headset.hpp"
#   include "erhe_xr/xr_session.hpp"
#   include "erhe_xr/xr_log.hpp"
#   include "erhe_xr/xr_instance.hpp"
#   if defined(ERHE_GRAPHICS_API_VULKAN)
#       include "erhe_graphics/vulkan_external_creators.hpp"
#   endif
#endif

#include "erhe_commands/commands.hpp"
#include "erhe_commands/commands_log.hpp"
#include "erhe_dataformat/dataformat_log.hpp"
#include "erhe_file/file.hpp"
#include "erhe_file/file_log.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_geometry/geometry_serialization.hpp"
#include "erhe_geometry/geometry_progress.hpp"
#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_gl/gl_helpers.hpp"
# include "erhe_gl/gl_log.hpp"
# include "erhe_gl/wrapper_functions.hpp"
#endif
#include "erhe_gltf/gltf_log.hpp"
#include "erhe_graph/graph_log.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/present_calibration.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_imgui/imgui_log.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/window_imgui_host.hpp"
#include "erhe_imgui/windows/log_window.hpp"
#include "erhe_imgui/windows/performance_window.hpp"
#include "erhe_imgui/windows/pipelines.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/expression.hpp"
#include "erhe_property/property_string.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_property/property_log.hpp"
#include "erhe_log/log.hpp"
#include "erhe_math/math_log.hpp"
#include "erhe_net/net_log.hpp"
#include "erhe_physics/physics_log.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_raytrace/raytrace_executor.hpp"
#include "erhe_task/task.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_scene_renderer/content_wide_line_interface.hpp"
#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_renderer/text_renderer.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/cull_frame_registry.hpp"
#include "erhe_scene_renderer/gpu_cull_classifier.hpp"
#include "erhe_scene_renderer/light_cluster_gpu_binner.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_scene_renderer/shadow_renderer.hpp"
#include "erhe_scene_renderer/shader_variant_cache.hpp"
#include "erhe_scene_renderer/texel_renderer.hpp"
#include "erhe_time/sleep.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_window/renderdoc_capture.hpp"
#include "erhe_window/window_log.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_event_handler.hpp"
#include "erhe_ui/glyph_outlines.hpp"
#include "erhe_ui/ui_log.hpp"
#if defined(ERHE_USD_LIBRARY_LIGHTUSD)
#include "erhe_usd/usd_log.hpp"
#endif
#include "erhe_window/clipboard.hpp"
#include "erhe_utility/thread_roles.hpp"

#if defined(ERHE_WINDOW_LIBRARY_SDL)
#   include <SDL3/SDL.h>
#endif
#include <taskflow/taskflow.hpp>

#if defined(ERHE_PROFILE_LIBRARY_NVTX)
#   include <nvtx3/nvToolsExt.h>
#endif

#if defined(ERHE_PROFILE_LIBRARY_TRACY)
#   include <tracy/TracyC.h>
#endif

#include <geogram/basic/assert.h>
#include <geogram/basic/attributes.h>
#include <geogram/basic/common.h>
#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/basic/geometry.h>
#include <geogram/basic/logger.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <set>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#if defined(ERHE_OS_LINUX)
#   include <unistd.h>
#   include <limits.h>
#endif

namespace editor {

namespace {

// Appends a report to the given file; the file is truncated on the first
// write of each run so an agent always reads the current run's errors.
void write_ai_error_report(const char* const path, const std::string& title, const std::string& content)
{
    static std::set<std::string> s_truncated_paths;
    const bool first_write = s_truncated_paths.insert(path).second;
    std::ofstream stream{path, first_write ? std::ios::trunc : std::ios::app};
    if (!stream.is_open()) {
        return;
    }
    stream << "=== " << title << " ===\n" << content << "\n";
}

// Environment overrides of the loaded erhe_graphics.json, for automated runs
// that need a different graphics configuration without rewriting the config
// file (the verify scripts run forward-Z through this).
// ERHE_FORCE_DISABLE_REVERSE_DEPTH=1|0 overrides force_disable_reverse_depth.
void apply_graphics_config_environment_overrides(Graphics_config& graphics_config)
{
    const char* const value = std::getenv("ERHE_FORCE_DISABLE_REVERSE_DEPTH");
    if (value == nullptr) {
        return;
    }
    const std::string_view text{value};
    if ((text != "1") && (text != "0")) {
        log_startup->warn("ERHE_FORCE_DISABLE_REVERSE_DEPTH = '{}' ignored (expected 1 or 0)", text);
        return;
    }
    const bool force_disable = (text == "1");
    log_startup->info(
        "ERHE_FORCE_DISABLE_REVERSE_DEPTH = {}: force_disable_reverse_depth {} -> {} (erhe_graphics.json not modified)",
        text, graphics_config.force_disable_reverse_depth, force_disable
    );
    graphics_config.force_disable_reverse_depth = force_disable;
}

// Command buffer thread slots of the frame phases (doc/editor/render_thread.md
// section 2.3). The main thread records the prepare phase (M2) uploads from
// its own slot, the render phase (R) records the frame from another, so the
// two never share a per-thread command pool once R runs on its own thread.
// Slots 6 (lightmap bake) and 7 (readback / export) are taken by the
// synchronous helper command buffers; slot 0 is the default the init path,
// image transfers and the OpenXR view command buffers use.
static constexpr unsigned int s_main_upload_thread_slot = 1;
static constexpr unsigned int s_render_thread_slot      = 2;

// The render thread mode of the session (doc/editor/editor.md "Frame phases
// and the render thread"): threading.render_thread from the editor
// settings, overridden by ERHE_RENDER_THREAD=0|1 (0 = inline on the main
// thread, 1 = dedicated thread) so scripts can pick a mode without
// rewriting the config file. The OpenGL and null backends always run
// inline: OpenGL records on the thread its context is current on (the main
// thread; doc/editor/render_thread.md section 4), and the null backend
// has no frame work worth a thread. Logged once.
[[nodiscard]] auto select_render_thread_mode(const Render_thread_mode configured_mode) -> Render_thread_mode
{
    Render_thread_mode mode   = configured_mode;
    const char*        source = "editor settings threading.render_thread";
    const char* const  value  = std::getenv("ERHE_RENDER_THREAD");
    if (value != nullptr) {
        const std::string_view text{value};
        if (text == "0") {
            mode   = Render_thread_mode::inline_main_thread;
            source = "ERHE_RENDER_THREAD=0";
        } else if (text == "1") {
            mode   = Render_thread_mode::dedicated_thread;
            source = "ERHE_RENDER_THREAD=1";
        } else {
            log_startup->warn("ERHE_RENDER_THREAD = '{}' ignored (expected 0 or 1)", text);
        }
    }
#if defined(ERHE_GRAPHICS_API_OPENGL) || defined(ERHE_GRAPHICS_API_NONE)
    if (mode != Render_thread_mode::inline_main_thread) {
# if defined(ERHE_GRAPHICS_API_OPENGL)
        const char* const backend = "OpenGL";
# else
        const char* const backend = "null";
# endif
        log_startup->info("Render thread mode: {} from {} overridden to inline_main_thread ({} graphics backend)", to_string(mode), source, backend);
        mode   = Render_thread_mode::inline_main_thread;
        source = "graphics backend";
    }
#endif
    log_startup->info("Render thread mode: {} (from {})", to_string(mode), source);
    return mode;
}

// The ImGui overlap of the session (doc/editor/render_thread.md section 2.5):
// threading.imgui_overlap from the editor settings, overridden by
// ERHE_IMGUI_OVERLAP=0|1; only a dedicated render thread can overlap.
[[nodiscard]] auto select_imgui_overlap(const Render_thread_mode render_thread_mode, const bool configured_overlap) -> bool
{
    bool        overlap = configured_overlap;
    const char* source  = "editor settings threading.imgui_overlap";
    const char* const value = std::getenv("ERHE_IMGUI_OVERLAP");
    if (value != nullptr) {
        const std::string_view text{value};
        if (text == "0") {
            overlap = false;
            source  = "ERHE_IMGUI_OVERLAP=0";
        } else if (text == "1") {
            overlap = true;
            source  = "ERHE_IMGUI_OVERLAP=1";
        } else {
            log_startup->warn("ERHE_IMGUI_OVERLAP = '{}' ignored (expected 0 or 1)", text);
        }
    }
    if (overlap && (render_thread_mode != Render_thread_mode::dedicated_thread)) {
        log_startup->info("ImGui overlap: on from {} overridden to off (render thread mode {})", source, to_string(render_thread_mode));
        overlap = false;
        source  = "render thread mode";
    }
    log_startup->info("ImGui overlap: {} (from {})", overlap ? "on" : "off", source);
    return overlap;
}

// The frame pipelining of the session (doc/editor/editor.md "Frame phases
// and the render thread"): threading.frame_pipelining from the editor
// settings, overridden by ERHE_FRAME_PIPELINING=0|1. Only a dedicated render
// thread can render a frame while the main thread prepares the next one,
// and the OpenXR headset path still prepares and renders its views in the
// render phase from live state (doc/editor/render_thread.md section 2.2), so it
// keeps the serial schedule. Logged once.
[[nodiscard]] auto select_frame_pipelining(const Render_thread_mode render_thread_mode, const bool openxr, const bool configured_pipelining) -> bool
{
    bool        pipelining = configured_pipelining;
    const char* source     = "editor settings threading.frame_pipelining";
    const char* const value = std::getenv("ERHE_FRAME_PIPELINING");
    if (value != nullptr) {
        const std::string_view text{value};
        if (text == "0") {
            pipelining = false;
            source     = "ERHE_FRAME_PIPELINING=0";
        } else if (text == "1") {
            pipelining = true;
            source     = "ERHE_FRAME_PIPELINING=1";
        } else {
            log_startup->warn("ERHE_FRAME_PIPELINING = '{}' ignored (expected 0 or 1)", text);
        }
    }
    if (pipelining && (render_thread_mode != Render_thread_mode::dedicated_thread)) {
        log_startup->info("Frame pipelining: on from {} overridden to off (render thread mode {})", source, to_string(render_thread_mode));
        pipelining = false;
        source     = "render thread mode";
    }
    if (pipelining && openxr) {
        log_startup->info("Frame pipelining: on from {} overridden to off (OpenXR headset)", source);
        pipelining = false;
        source     = "OpenXR headset";
    }
    log_startup->info("Frame pipelining: {} (from {})", pipelining ? "on" : "off", source);
    return pipelining;
}

// config/editor/renderer.json with its v1 -> v2 migration: v1's
// max_light_count sized a light uniform block (default 32); v2's is the
// capacity of the light storage array that clustered light shading fills
// with every active light (default 1024), so a v1 value below the v2
// default is raised to it.
[[nodiscard]] auto load_renderer_config() -> Renderer_config
{
    static std::once_flag s_once;
    std::call_once(
        s_once,
        []() {
            erhe::codegen::register_migration<Renderer_config>(
                [](Renderer_config& config, const uint32_t old_version, const uint32_t new_version) -> bool
                {
                    static_cast<void>(new_version);
                    if (old_version < 2) {
                        const Renderer_config v2_defaults{};
                        config.max_light_count = std::max(config.max_light_count, v2_defaults.max_light_count);
                    }
                    return true;
                }
            );
        }
    );
    return erhe::codegen::load_config<Renderer_config>("config/editor/renderer.json");
}

} // anonymous namespace

class Editor : public erhe::window::Input_event_handler
{
public:
    std::mutex m_mutex;
    // Coarse wall-clock accumulators for the frame phases. Printed at info
    // every 60 frames so a long-running frame time regression is visible
    // without enabling per-frame trace logs.
    //
    // m1_us / m2_us / render_us bracket frame_overlap_phase(),
    // frame_prepare_phase() and frame_render_phase()
    // (doc/editor/render_thread.md section 2.1); run()'s frame start and
    // poll_events wait are outside frame() entirely. full_tick_us runs from
    // the frame start to the end of frame(), so it is the loop period.
    struct Phase_timing {
        double frame_start_wait_us {0.0}; // Device::wait_for_frame_start + frame pacing waits (start_frame(), before run()'s poll)
        double slot_acquire_us     {0.0}; // Device::acquire_frame_slot alone (part of M2)
        double m1_us               {0.0}; // M1 overlap (frame_start_wait_us is outside it unless frame() made its own frame start)
        double m2_us               {0.0}; // M2 prepare (includes slot_acquire_us); the sum of the m2_*_us terms below
        double m2_start_us         {0.0}; // input dispatch, acquire_frame_slot
        double m2_upload_us        {0.0}; // scene commit flush, asset and texture file load ticks
        double m2_sim_us           {0.0}; // animations, pointer, OpenXR poll + actions, node fixed steps, physics write-back
        double m2_imgui_us         {0.0}; // ImGui events, commands, MCP, network
        double m3_us               {0.0}; // M3 ImGui phase: the ImGui windows and the settings autosave (main thread; after the kick with the ImGui overlap)
        double draw_data_wait_us   {0.0}; // R: the ImGui host nodes' wait for M3's draw data (part of R)
        double kick_latency_us     {0.0}; // handoff: kick() to R's first instruction (dedicated thread)
        double join_latency_us     {0.0}; // handoff: R's end to join()'s return (dedicated thread)
        double m2_update_us        {0.0}; // hover, operations, graphs, message bus, lightmap commit
        double m2_scene_us         {0.0}; // mesh memory flush, layout, transforms, hotbar, draw list flush, material sets, lightmap prepare
        double render_us           {0.0}; // R render
        double swapchain_us        {0.0}; // wait_for_swapchain + begin_swapchain (both part of R)
        double join_wait_us        {0.0}; // main thread blocked joining the previous frame's R (Render_thread::join() in M1, or in Render_thread::kick() with frame pipelining); R minus join_wait = overlapped R
        double full_tick_us        {0.0}; // start_frame() begin to frame() end: frame start wait + poll + frame()
        unsigned int frame_count   {0};
    };
    Phase_timing m_phase_timing{};

    [[nodiscard]] static auto elapsed_us(
        const std::chrono::steady_clock::time_point begin,
        const std::chrono::steady_clock::time_point end
    ) -> double
    {
        return std::chrono::duration<double, std::micro>{end - begin}.count();
    }

    void log_phase_timing(const std::chrono::steady_clock::time_point tick_begin)
    {
        const std::chrono::steady_clock::time_point tick_end = std::chrono::steady_clock::now();
        m_phase_timing.full_tick_us += elapsed_us(tick_begin, tick_end);
        ++m_phase_timing.frame_count;
        if (m_benchmark_driver) {
            hand_off_benchmark_frame(tick_end);
        }
        constexpr unsigned int phase_timing_log_interval = 60;
        if (m_phase_timing.frame_count < phase_timing_log_interval) {
            return;
        }
        const double frame_count = static_cast<double>(m_phase_timing.frame_count);
        const double m1_us       = m_phase_timing.m1_us        / frame_count;
        const double m2_us       = m_phase_timing.m2_us        / frame_count;
        const double render_us   = m_phase_timing.render_us    / frame_count;
        const double full_us     = m_phase_timing.full_tick_us / frame_count;
        log_frame->info(
            "Phase timing (us/frame, avg of {} frames): M1 {:.1f} (frame_start_wait {:.1f}) M2 {:.1f} (slot_acquire {:.1f}; start {:.1f} upload {:.1f} sim {:.1f} imgui {:.1f} update {:.1f} scene {:.1f}) M3 {:.1f} R {:.1f} (swapchain {:.1f} draw_data_wait {:.1f}) join_wait {:.1f} handoff (kick {:.1f} join {:.1f}) full tick {:.1f}",
            m_phase_timing.frame_count,
            m1_us,
            m_phase_timing.frame_start_wait_us / frame_count,
            m2_us,
            m_phase_timing.slot_acquire_us / frame_count,
            m_phase_timing.m2_start_us  / frame_count,
            m_phase_timing.m2_upload_us / frame_count,
            m_phase_timing.m2_sim_us    / frame_count,
            m_phase_timing.m2_imgui_us  / frame_count,
            m_phase_timing.m2_update_us / frame_count,
            m_phase_timing.m2_scene_us  / frame_count,
            m_phase_timing.m3_us        / frame_count,
            render_us,
            m_phase_timing.swapchain_us / frame_count,
            m_phase_timing.draw_data_wait_us / frame_count,
            m_phase_timing.join_wait_us / frame_count,
            m_phase_timing.kick_latency_us / frame_count,
            m_phase_timing.join_latency_us / frame_count,
            full_us
        );
        m_phase_timing           = Phase_timing{};
        m_benchmark_phase_timing = Phase_timing{};
    }

    // Benchmark mode, end of frame(): this frame's phase terms are the
    // accumulator deltas since the previous frame's end (R: the frame joined
    // during this tick), handed to the driver with the settle state.
    Phase_timing                          m_benchmark_phase_timing{}; // m_phase_timing at the previous frame's end
    std::chrono::steady_clock::time_point m_benchmark_clock_origin{std::chrono::steady_clock::now()};

    void hand_off_benchmark_frame(const std::chrono::steady_clock::time_point tick_end)
    {
        const Phase_timing& now  = m_phase_timing;
        const Phase_timing& prev = m_benchmark_phase_timing;
        Benchmark_frame_inputs inputs{
            .full_tick_us       = now.full_tick_us - prev.full_tick_us,
            .m1_us              = now.m1_us        - prev.m1_us,
            .m2_us              = now.m2_us        - prev.m2_us,
            .m3_us              = now.m3_us        - prev.m3_us,
            .r_us               = now.render_us    - prev.render_us,
            .wall_clock_s       = std::chrono::duration<double>{tick_end - m_benchmark_clock_origin}.count(),
            .frame_rendered                = true,
            .refresh_rate_failure          = nullptr,
            .scene_settled                 = false,
            .refresh_rate_settled          = false,
            .display_refresh_hz            = 0.0,
            .applied_cpu_performance_level = -2,
            .applied_gpu_performance_level = -2,
            .thermal_step_down_count       = 0
        };
        // Whether this frame could render: under OpenXR the frame was
        // waited, the runtime asked for rendering and the actions updated
        // (the headset view renders only then); without OpenXR every frame
        // renders. A session that never runs (e.g. the headset off-head
        // without the proximity override) never settles, and a frame that
        // does not render fails the measured window.
#if defined(ERHE_XR_LIBRARY_OPENXR)
        if (m_app_context.OpenXR && m_headset_view) {
            const erhe::xr::Frame_timing& timing = m_frame_packet.headset_frame_timing;
            inputs.frame_rendered               = timing.wait_ok && timing.should_render && m_headset_view->is_update_actions_ok();
            inputs.refresh_rate_failure         = &m_headset_view->get_benchmark_display_refresh_rate_failure();
        }
#endif
        // The settle conditions have no change notification to drive them
        // (async workers, the asset manager and the brush queue count down
        // on their own), so they are polled, and only while the driver waits
        // for them (and on the last measured frame, for the refresh rate at
        // the end). A requested display refresh rate takes effect at a frame
        // boundary of the runtime's choice, so it is part of settling: the
        // achieved rate must be within 0.5 Hz of the requested one.
        if (m_benchmark_driver->needs_settle_inputs()) {
            inputs.scene_settled =
                !m_app_context.is_scene_load_in_flight() &&
                (!m_scene_builder || m_scene_builder->is_brush_geometry_idle());
            inputs.display_refresh_hz = get_benchmark_display_refresh_hz();
            const std::optional<float> requested_refresh_hz = m_benchmark_driver->get_display_refresh_rate_hz();
            inputs.refresh_rate_settled =
                !requested_refresh_hz.has_value() ||
                (std::abs(inputs.display_refresh_hz - static_cast<double>(requested_refresh_hz.value())) <= 0.5);
#if defined(ERHE_XR_LIBRARY_OPENXR)
            if (m_app_context.OpenXR && m_headset_view) {
                inputs.applied_cpu_performance_level = m_headset_view->get_applied_cpu_performance_level();
                inputs.applied_gpu_performance_level = m_headset_view->get_applied_gpu_performance_level();
                inputs.thermal_step_down_count       = m_headset_view->get_thermal_step_down_count();
            }
#endif
        }
        m_benchmark_driver->on_frame_end(inputs);
        m_benchmark_phase_timing = m_phase_timing;
    }

    // The achieved display refresh rate: the XR runtime's
    // (XR_FB_display_refresh_rate), else the window's display.
    [[nodiscard]] auto get_benchmark_display_refresh_hz() const -> double
    {
#if defined(ERHE_XR_LIBRARY_OPENXR)
        if (m_app_context.OpenXR && m_headset_view && (m_headset_view->get_headset() != nullptr)) {
            erhe::xr::Xr_session* session = m_headset_view->get_headset()->get_xr_session();
            const float xr_refresh_hz = (session != nullptr) ? session->get_display_refresh_rate() : 0.0f;
            if (xr_refresh_hz > 0.0f) {
                return static_cast<double>(xr_refresh_hz);
            }
        }
#endif
        const double refresh_duration_s = m_graphics_device->get_display_refresh_duration_seconds();
        return (refresh_duration_s > 0.0) ? (1.0 / refresh_duration_s) : 0.0;
    }

    // Benchmark mode, M2 after the XR actions: the XR_META_performance_metrics
    // counters the render phase of the previous frame queried
    // (Headset::begin_frame()); R is idle in M2 under the serial schedule
    // OpenXR runs.
    void sample_benchmark_xr_counters()
    {
#if defined(ERHE_XR_LIBRARY_OPENXR)
        erhe::xr::Headset*    headset = m_headset_view->get_headset();
        erhe::xr::Xr_session* session = (headset != nullptr) ? headset->get_xr_session() : nullptr;
        if (session == nullptr) {
            return;
        }
        const std::vector<erhe::xr::Xr_perf_counter>& counters = session->get_perf_counters();
        for (std::size_t i = 0, end = counters.size(); i < end; ++i) {
            const XrPerformanceMetricsCounterMETA& last = counters[i].last;
            const bool float_valid = (last.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_FLOAT_VALUE_VALID_BIT_META) != 0;
            const bool uint_valid  = (last.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_UINT_VALUE_VALID_BIT_META ) != 0;
            const double value =
                float_valid ? static_cast<double>(last.floatValue) :
                uint_valid  ? static_cast<double>(last.uintValue)  :
                              0.0;
            m_benchmark_driver->set_counter(i, counters[i].display_name, value);
        }
#endif
    }

    // run(), before the frame loop: the run identity and the headset's
    // benchmark mode.
    void begin_benchmark()
    {
        m_benchmark_driver->set_run_identity(
            Benchmark_run_identity{
                .commands_path         = m_resolved_startup_commands_path,
                .cpu_performance_level = std::string{to_string(m_editor_settings.headset.cpu_performance_level)},
                .gpu_performance_level = std::string{to_string(m_editor_settings.headset.gpu_performance_level)}
            }
        );
#if defined(ERHE_XR_LIBRARY_OPENXR)
        if (m_headset_view) {
            m_headset_view->enable_benchmark_mode(m_benchmark_driver->get_display_refresh_rate_hz(), m_benchmark_driver->get_passthrough());
        }
#endif
    }

    // One frame is three phases (doc/editor/render_thread.md section 2.1):
    // frame_overlap_phase() (M1) and frame_prepare_phase() (M2) on the main
    // thread, then frame_render_phase() (R) through Render_thread::kick(),
    // inline on this thread or on the dedicated render thread, and the
    // ImGui phase (M3) before or, with the ImGui overlap, after the kick.
    // Where R of the previous frame is joined depends on the schedule
    // (doc/editor/editor.md "Frame phases and the render thread"):
    //
    // - serial (threading.frame_pipelining off, or no dedicated thread):
    //   at the end of M1. R(N) runs while run() polls window events and
    //   M1(N+1) advances the clock, waits for the OpenXR frame and steps
    //   physics; M2 and R never overlap.
    // - pipelined: in Render_thread::kick() of the next frame. M1, M2 (and
    //   M3) of frame N+1 run while R(N) renders; kick(N+1) waits for R(N),
    //   then hands over N+1. What the join publishes (the last rendered
    //   frame, the sky LUT readiness, the frame log entries, the phase
    //   timings) reaches the main thread one frame later than in the
    //   serial schedule.
    //
    // m_frame_packet carries the per-frame data from M1 / M2 to R; R works
    // from Render_thread's copy, so the next frame rewrites it while R(N)
    // runs.
    //
    // m_in_tick is true while this function runs: from M1 start to the kick
    // (and M3 after it).
    void frame()
    {
        m_in_tick.store(true);
        std::lock_guard<std::mutex> lock{m_mutex};
        Frame_packet& packet = m_frame_packet;

        ERHE_PROFILE_FUNCTION();

        // Record which threads run the frame phases so the watchdog can
        // attribute the newest breadcrumb of each (worker threads also set
        // breadcrumbs). The roles are published before the first frame
        // (run(), Render_thread); set once, the lock is taken only on the
        // first frame.
        if (!m_watchdog_threads_known) {
            const std::size_t main_thread_hash   = std::hash<std::thread::id>{}(erhe::utility::Thread_roles::get_main_thread());
            const std::size_t render_thread_hash = std::hash<std::thread::id>{}(erhe::utility::Thread_roles::get_render_thread());
            std::lock_guard<std::mutex> watchdog_lock{m_watchdog_mutex};
            m_main_thread_hash       = main_thread_hash;
            m_render_thread_hash     = render_thread_hash;
            m_watchdog_threads_known = true;
        }

        frame_overlap_phase(packet);

        // Serial schedule: M2 writes what R reads, so R of the previous
        // frame ends first. The join wait lies inside this frame's CPU slot
        // (opened by wait_for_frame_start in M1) and is involuntary: it is
        // stamped as the record's join_wait pair, which cpu_service_time()
        // subtracts (doc/frame_pacing/inputs.md section 3.2).
        if (!m_frame_pipelining && m_frame_outstanding) {
            erhe::log::set_breadcrumb("tick: join previous frame");
            const Frame_result joined_result = join_outstanding_frame();
            record_join_wait(packet.device_frame_id, joined_result);
            if (joined_result.no_swapchain) {
                sleep_after_no_swapchain();
            }
        }

        frame_prepare_phase(packet);

        // The main thread leaves M2 before the kick; frame_render_phase()
        // sets the render role's phase itself, on whichever thread runs it
        // (erhe::utility::Thread_roles: in inline mode the main thread
        // holds both roles). With the ImGui overlap (dedicated render
        // thread, threading.imgui_overlap) the ImGui phase (M3) draws the
        // editor windows after the kick, while R renders this frame's
        // scene; R's ImGui host nodes wait for the draw data it publishes.
        // Without it M3 runs before the kick (doc/editor/render_thread.md
        // section 2.5).
        set_frame_phase(erhe::utility::Frame_phase::none);
        if (m_imgui_overlap) {
            kick_frame(packet);
            frame_imgui_phase(packet);
            m_render_thread->publish_draw_data(packet.frame_number);
        } else {
            frame_imgui_phase(packet);
            m_render_thread->publish_draw_data(packet.frame_number);
            kick_frame(packet);
        }

        log_phase_timing(m_frame_start_begin);
        m_in_tick.store(false);
    }

    // Main thread: hands the packet to the render phase. With frame
    // pipelining the kick first joins the frame kicked by the previous
    // frame() (Render_thread::kick() waits for its R), so that frame's
    // result is accounted here, one frame later than the serial schedule's
    // join at the end of M1; the wait lies inside this frame's CPU slot and
    // is stamped as its join_wait pair.
    void kick_frame(const Frame_packet& packet)
    {
        erhe::log::set_breadcrumb("tick: kick");
        const std::optional<Frame_result> joined = m_render_thread->kick(packet);
        if (joined.has_value()) {
            // The serial schedule joined in M1 (nothing left to join here).
            ERHE_VERIFY(m_frame_pipelining);
            ERHE_VERIFY(m_frame_outstanding);
            m_frame_outstanding = false;
            on_frame_joined(joined.value());
            record_join_wait(packet.device_frame_id, joined.value());
        }
        ERHE_VERIFY(!m_frame_outstanding);
        m_frame_outstanding = true;
        if (joined.has_value() && joined.value().no_swapchain) {
            sleep_after_no_swapchain();
        }
    }

    // Main thread: the join wait of a frame as the join_wait record pair of
    // the frame whose CPU slot it lies in (frame_id: the frame being
    // prepared).
    void record_join_wait(const uint64_t frame_id, const Frame_result& joined_result)
    {
        // The render thread times its waits with steady_clock; the recorder
        // has its own clock (mach_absolute_time on Apple, which stops during
        // system sleep while steady_clock does not). Map each time point
        // through now in both clocks: exact to the nanoseconds between the
        // two reads, whatever the recorder's clock is.
        const double                                recorder_now = erhe::frame_pacing::Frame_time_recorder::now();
        const std::chrono::steady_clock::time_point steady_now   = std::chrono::steady_clock::now();
        const auto to_recorder_time = [recorder_now, steady_now](const std::chrono::steady_clock::time_point time_point) -> double {
            return recorder_now - std::chrono::duration<double>{steady_now - time_point}.count();
        };
        erhe::frame_pacing::Frame_time_recorder& recorder = m_graphics_device->get_frame_time_recorder();
        recorder.set(static_cast<std::int64_t>(frame_id), &erhe::frame_pacing::Frame_time_record::join_wait_begin, to_recorder_time(joined_result.join_wait_begin));
        recorder.set(static_cast<std::int64_t>(frame_id), &erhe::frame_pacing::Frame_time_record::join_wait_end,   to_recorder_time(joined_result.join_wait_end));
    }

    // The joined frame found no swapchain image -- typical on Android while
    // the surface is between background and foreground. Its render phase
    // ended the device frame without presenting; sleep a short interval so
    // the loop does not burn CPU spinning on a not-yet-ready surface
    // (doc/editor/render_thread.md section 2.4).
    void sleep_after_no_swapchain()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }

    // M3, the ImGui phase (main thread, Frame_phase::m3_imgui): the ImGui
    // windows of every host draw, then the editor settings autosave. It
    // reads the live editor state the windows own and the render settings
    // copies, records no GPU work (current_command_buffer is null;
    // Thread_roles::assert_command_recording_allowed refuses the main
    // thread in this phase) and defers its render-side work
    // (doc/editor/coding_rules.md "ImGui-time work"). The draw data it
    // leaves behind is published to R by the caller.
    void frame_imgui_phase(const Frame_packet& packet)
    {
        ERHE_PROFILE_FUNCTION();
        const std::chrono::steady_clock::time_point m3_begin = std::chrono::steady_clock::now();
        set_frame_phase(erhe::utility::Frame_phase::m3_imgui);
        ERHE_VERIFY(m_app_context.current_command_buffer == nullptr);

        m_hover_tool->reset_item_tree_hover();

        erhe::log::set_breadcrumb("tick: draw_imgui_windows");
        m_imgui_windows->begin_frame(packet.device_frame_id);
        m_imgui_windows->draw_imgui_windows();
        m_imgui_windows->end_frame();

        // Autosave editor settings when an edit site marked them dirty
        // (Editor_settings_store::touch() / Graphics_settings::
        // mark_presets_dirty()); a bool test per file otherwise. Defer while
        // a mouse button is held so dragging a slider results in a single
        // write when the drag ends.
        {
            bool any_mouse_button_down = false;
            for (const bool button_down : m_input_state->mouse_button) {
                any_mouse_button_down = any_mouse_button_down || button_down;
            }
            m_app_settings.update(!any_mouse_button_down);
        }

        set_frame_phase(erhe::utility::Frame_phase::none);
        m_phase_timing.m3_us += elapsed_us(m3_begin, std::chrono::steady_clock::now());
    }

    // Main thread: waits for the render phase kicked by the previous frame
    // (nothing when that frame kicked none: the first frame) and accounts
    // its Frame_result (on_frame_joined()). Returns the joined result (a
    // default one when nothing was outstanding). Called at the end of M1 in
    // the serial schedule, after the frame loop ends (run()) and by ~Editor
    // before the render thread is stopped.
    auto join_outstanding_frame() -> Frame_result
    {
        if (!m_frame_outstanding) {
            return Frame_result{};
        }
        const Frame_result result = m_render_thread->join();
        m_frame_outstanding = false;
        on_frame_joined(result);
        return result;
    }

    // App_context::join_render_phase: with frame pipelining, joins the frame
    // still in flight from the prepare phase (or outside a frame), for the
    // standalone submits that must follow its recording and submits. Not
    // from the ImGui phase: with the ImGui overlap it runs after the kick,
    // and the frame in flight waits for its draw data.
    void join_render_phase()
    {
        if (!m_frame_pipelining || !m_frame_outstanding) {
            return;
        }
        erhe::utility::Thread_roles::assert_main_thread();
        const erhe::utility::Frame_phase phase = erhe::utility::Thread_roles::get_frame_phase();
        ERHE_VERIFY((phase == erhe::utility::Frame_phase::m2_prepare) || (phase == erhe::utility::Frame_phase::none));
        erhe::log::set_breadcrumb("join render phase (standalone submit)");
        static_cast<void>(join_outstanding_frame());
    }

    // Main thread, at the join of a frame (join_outstanding_frame(), or the
    // kick that joined it with frame pipelining): adds its Frame_result
    // into the phase timing and publishes what its render phase left for
    // the main thread.
    void on_frame_joined(const Frame_result& result)
    {
        m_phase_timing.join_wait_us += elapsed_us(result.join_wait_begin, result.join_wait_end);

        // What the joined render phase wrote for the main thread is read as
        // the result of its frame from here on (Render_frame_results), and
        // the sky LUT readiness it saw gates the lightmap bake's sky term.
        m_app_context.last_rendered_frame_index = result.device_frame_id;
        m_sky_luts_ready                        = result.sky_luts_ready;

        m_phase_timing.render_us         += result.render_us;
        m_phase_timing.swapchain_us      += result.wait_for_swapchain_us + result.begin_swapchain_us;
        m_phase_timing.draw_data_wait_us += result.draw_data_wait_us;
        m_phase_timing.kick_latency_us   += result.kick_latency_us;
        m_phase_timing.join_latency_us   += result.join_latency_us;

        // The frame's log entries (both threads logged into the frame store)
        // become the Frame log window's entries here, on the main thread,
        // once the render phase that logged last has ended; the window
        // reads them in its imgui() (doc/editor/render_thread.md section 2.6).
        m_frame_log_window->on_frame_end();
    }

    // Main thread: writes the main role's frame phase to
    // erhe::utility::Thread_roles and mirrors it for the watchdog thread
    // under m_watchdog_mutex, since Thread_roles is unsynchronized by
    // design. In inline mode the main thread holds the render role too and
    // Thread_roles writes both phases, so both mirrors follow.
    void set_frame_phase(const erhe::utility::Frame_phase phase)
    {
        erhe::utility::Thread_roles::set_frame_phase(phase);
        std::lock_guard<std::mutex> watchdog_lock{m_watchdog_mutex};
        m_watchdog_main_frame_phase = phase;
        if (m_render_thread_mode == Render_thread_mode::inline_main_thread) {
            m_watchdog_render_frame_phase = phase;
        }
    }

    // The thread running frame_render_phase() (the render thread, or the
    // main thread in inline mode): writes the render role's frame phase
    // and mirrors it for the watchdog, as set_frame_phase() does for the
    // main role.
    void set_render_frame_phase(const erhe::utility::Frame_phase phase)
    {
        erhe::utility::Thread_roles::set_frame_phase(phase);
        std::lock_guard<std::mutex> watchdog_lock{m_watchdog_mutex};
        m_watchdog_render_frame_phase = phase;
        if (m_render_thread_mode == Render_thread_mode::inline_main_thread) {
            m_watchdog_main_frame_phase = phase;
        }
    }

    // Whether this frame enforces the pacer decision of the given tier:
    // graphics config frame_pacing_enforce=false falls back to pure
    // observer mode (the kill switch); skipped when nothing is being
    // presented (hidden window, OpenXR without desktop swapchain): presents
    // stop, so the waits could only time out.
    [[nodiscard]] auto is_frame_pacing_enforced(const erhe::graphics::Frame_pacing_tier tier) const -> bool
    {
        return
            m_graphics_device->get_graphics_config().frame_pacing_enforce &&
            !m_app_context.OpenXR &&
            (m_frame_activity != Frame_activity::hidden) &&
            (m_graphics_device->get_frame_pacing_tier() == tier);
    }

    // The frame pacer's presentation setup for the window display's
    // presentation (claim C30, doc/plans/frame_pacing_adaptive_sync.md).
    // A presentation path that honors target present times shows its
    // deadline offset in the feedback (Metal tier M, Vulkan tier W through
    // VK_GOOGLE_display_timing), starting from the
    // deadline the device's present calibration measured, with the estimate
    // ceiling half a period above it; elsewhere the pacer keeps its fixed
    // initial value. On a variable refresh rate display driven fullscreen
    // the pacer tracks no grid (set_display), and the calibration - measured
    // on the windowed compositor, about one refresh period - is not its
    // deadline: there the image only has to be complete about 1 ms before
    // its target, and the estimator never decays below its seed, so the
    // seed is the pacer's uncalibrated default
    // (Frame_pacer_tunables::deadline_offset_initial), raised by evidence.
    void apply_display_presentation(const erhe::graphics::Display_presentation& presentation)
    {
        const bool variable_refresh =
            (presentation.kind == erhe::frame_pacing::Present_display_kind::variable_refresh) &&
            (presentation.maximum_refresh_interval > 0.0);
        erhe::frame_pacing::Deadline_offset_setup deadline_offset{};
        if (m_graphics_device->is_present_target_time_honored()) {
            deadline_offset.source        = erhe::frame_pacing::Deadline_offset_source::estimated;
            deadline_offset.present_queue = erhe::frame_pacing::Present_queue_kind::fifo;
            const erhe::graphics::Present_calibration calibration = m_graphics_device->get_present_calibration();
            if (calibration.valid && !variable_refresh) {
                deadline_offset.calibrated_period = calibration.period;
                deadline_offset.initial           = calibration.deadline_offset;
                deadline_offset.maximum           = calibration.deadline_offset + (0.5 * calibration.period);
            }
            // The GPU pre-warm fill starts here and the pacer learns the
            // one that raises the clock (claim C26); 2.3 ms raised it on an
            // M4 Pro. The kernel rate is measured even when the rest of the
            // calibration is not valid.
            deadline_offset.upshift_prewarm = (calibration.prewarm_seconds_per_iteration > 0.0) ? 0.002 : 0.0;
        }
        m_frame_pacing_observer.set_deadline_offset(deadline_offset);
        if (variable_refresh) {
            m_frame_pacing_observer.set_display(
                erhe::frame_pacing::Present_display_kind::variable_refresh,
                presentation.maximum_refresh_interval,
                presentation.update_granularity
            );
        } else {
            m_frame_pacing_observer.set_display(erhe::frame_pacing::Present_display_kind::fixed_grid, 0.0, 0.0);
        }
        // C32: the pacer ignores a granularity that is no tick grid finer than
        // the period (a fixed-rate mode reports its refresh interval).
        erhe::frame_pacing::Frame_pacer_tunables granularity_check{};
        granularity_check.refresh_period      = presentation.minimum_refresh_interval;
        granularity_check.display             = variable_refresh ? erhe::frame_pacing::Present_display_kind::variable_refresh : erhe::frame_pacing::Present_display_kind::fixed_grid;
        granularity_check.refresh_granularity = presentation.update_granularity;
        const bool granularity_ignored =
            variable_refresh &&
            (presentation.update_granularity > 0.0) &&
            (erhe::frame_pacing::Frame_pacer::get_usable_refresh_granularity(granularity_check) == 0.0);
        const std::string granularity_note = granularity_ignored
            ? fmt::format(" ignored: not below {:.2f} of the minimum refresh interval", granularity_check.refresh_granularity_max_frac)
            : std::string{};
        log_frame_pacing->info(
            "frame pacing: display {} (max refresh interval {:.3f} ms, update granularity {:.3f} ms{}), deadline offset seed {}",
            variable_refresh ? "variable refresh" : "fixed grid",
            presentation.maximum_refresh_interval * 1000.0,
            variable_refresh ? (presentation.update_granularity * 1000.0) : 0.0,
            granularity_note,
            (deadline_offset.initial > 0.0) ? "present calibration" : "pacer default"
        );
    }

    // M1: the pacing half of the device frame start, the analogue of
    // xrWaitFrame (doc/editor/render_thread.md section 2.3). Runs right after
    // Device::wait_for_frame_start() opened this frame's time record and
    // before Time::prepare_update, while the render phase of the previous
    // frame may still submit and present: the frame time recorder is
    // locked, and what R needs from the decision (the target present time,
    // the holdback deadline and the present-wait id) travels in the packet.
    // Returns the FR4 display
    // advance for this frame's prepare_update; < 0 = wall-clock fallback.
    [[nodiscard]] auto frame_start_pacing(Frame_packet& packet) -> int64_t
    {
        ERHE_PROFILE_SCOPE("frame pacing");

        packet.present_target_time = 0.0;
        packet.present_hold_until  = 0.0;
        packet.present_prewarm     = 0.0;
        packet.present_wait_id     = -1;
        int64_t display_advance_ns = -1;

        // Frame pacing observer (P2.1): feed the pacer with the frame time
        // records and log its decisions. The decision is for the frame just
        // started (the recorder's latest frame id).
        erhe::frame_pacing::Frame_time_recorder& recorder = m_graphics_device->get_frame_time_recorder();
        m_frame_pacing_observer.tick(
            recorder,
            erhe::frame_pacing::Frame_time_recorder::now(),
            m_graphics_device->get_display_refresh_duration_seconds()
        );
        if (log_frame_pacing->level() <= spdlog::level::debug) {
            std::string pacing_log_line;
            while (m_frame_pacing_observer.consume_log_line(pacing_log_line)) {
                log_frame_pacing->debug("{}", pacing_log_line);
            }
        }

        const std::int64_t frame_id = static_cast<std::int64_t>(packet.device_frame_id);
        // The actuators stay idle while the device's display refresh init
        // calibration runs (the first presents): a cadence they imposed
        // would be measured as the display's
        // (erhe::graphics::Device::is_display_refresh_calibrating).
        if (is_frame_pacing_enforced(erhe::graphics::Frame_pacing_tier::full) && !m_graphics_device->is_display_refresh_calibrating()) {
            // Frame pacing actuation (steps P2.2 + P2.3), before per-frame work:
            // 1. release gating (FR2): high-resolution timer wait until the
            //    pacer's release time (P0.5: the waitable timer is the only wait
            //    mode with usable wake error; its p99 0.59 ms sits inside the
            //    pacer's 1 ms guard), stamped as the pacer_wait record pair (an
            //    involuntary wait, subtracted from cpu_service_time);
            // 2. target present time (FR3) and holdback deadline: carried in
            //    the packet to the render phase, which hands them to the
            //    backend right before its submits;
            // 3. FR4 display advance (below);
            // 4. present-wait clamp (FR5): the frame id to wait for travels
            //    in the packet; the render phase waits before its swapchain
            //    wait (present_wait_clamp).
            // m_frame_activity, which decides the enforcement, is
            // main-thread state, so the render phase reads only the packet.
            const erhe::frame_pacing::Schedule_decision& pacing_decision = m_frame_pacing_observer.get_last_decision();
            double release_wait_s = pacing_decision.release_time - erhe::frame_pacing::Frame_time_recorder::now();
            if (release_wait_s > 0.0) {
                // A sane release sits at most one latency budget ahead (C14
                // measured max ~15 ms); a wait this long means the schedule
                // lost contact with reality - cap it so the app never stalls
                // on a broken schedule (mirrors the clamp's 100 ms bound).
                constexpr double s_max_release_wait_s = 0.1;
                if (release_wait_s > s_max_release_wait_s) {
                    log_frame_pacing->warn(
                        "release gate capped: frame {} release {:.1f} ms ahead",
                        pacing_decision.frame_id, release_wait_s * 1000.0
                    );
                    release_wait_s = s_max_release_wait_s;
                }
                recorder.set(pacing_decision.frame_id, &erhe::frame_pacing::Frame_time_record::pacer_wait_begin, erhe::frame_pacing::Frame_time_recorder::now());
                m_pacer_release_timer.wait_for(release_wait_s);
                recorder.set(pacing_decision.frame_id, &erhe::frame_pacing::Frame_time_record::pacer_wait_end, erhe::frame_pacing::Frame_time_recorder::now());
            }
            if ((pacing_decision.target_time > 0.0) && (pacing_decision.frame_id == frame_id)) {
                packet.present_target_time = pacing_decision.target_time;
                packet.present_hold_until  = pacing_decision.hold_until;
                packet.present_prewarm     = pacing_decision.prewarm_seconds;
            }
            if ((pacing_decision.wait_id >= 0) && (pacing_decision.frame_id == frame_id)) {
                packet.present_wait_id = pacing_decision.wait_id;
            }
            // FR4 routing (P2.4): advance the simulation clock by the delta
            // between successive predicted display times, so the state
            // rendered into this frame is sampled at the time the frame will
            // be SHOWN, not the (jittery) time it is produced. The decision
            // is for this frame, so the advance applies to the frame it was
            // computed for. Predicted display slots are strictly increasing;
            // a delta outside (0, 0.25 s) means the schedule re-anchored
            // across a gap (cadence change, swapchain recreation) - fall back
            // to wall clock for that frame. FR4 prediction accuracy is
            // tracked by the observer summary (achieved vs predicted, median
            // 23 us steady state).
            if ((pacing_decision.predicted_display > 0.0) && (pacing_decision.frame_id == frame_id)) {
                if (m_last_predicted_display_time > 0.0) {
                    const double display_delta_s = pacing_decision.predicted_display - m_last_predicted_display_time;
                    if ((display_delta_s > 0.0) && (display_delta_s < 0.25)) {
                        display_advance_ns = static_cast<int64_t>(std::llround(display_delta_s * 1e9));
                    }
                }
                m_last_predicted_display_time = pacing_decision.predicted_display;
            } else {
                m_last_predicted_display_time = 0.0;
            }
        } else if (is_frame_pacing_enforced(erhe::graphics::Frame_pacing_tier::slop_servo) && !m_graphics_device->is_display_refresh_calibrating()) {
            // Tier S (P4.1/P4.2): slop-servo fallback. No display-time
            // sensing, no grid, no cadence - measure the involuntary
            // blocking ("slop") the loop experienced since the previous
            // tick, servo a sleep before the simulation, and let the
            // plain-FIFO queue do the rest. Plain FIFO backpressure blocks
            // inside vkQueuePresentKHR / vkAcquireNextImageKHR. The previous
            // frame's render phase may still run here, so the slop comes
            // from records the main thread has seen complete: the previous
            // frame's slot fence wait (its M2) and the swapchain acquire and
            // present-call blocking of the frame before it (joined). The
            // Frame_pacer observer still ticks above for records/telemetry,
            // but its decisions are not enforced in this tier.
            const double now_seconds = erhe::frame_pacing::Frame_time_recorder::now();
            const double refresh_period = m_graphics_device->get_display_refresh_duration_seconds();
            if (refresh_period > 0.0) {
                m_slop_servo.set_refresh_period(refresh_period);
            }
            erhe::frame_pacing::Frame_time_record previous_record{};
            erhe::frame_pacing::Frame_time_record joined_record{};
            double slop = 0.0;
            if (recorder.get(frame_id - 1, previous_record)) {
                slop += previous_record.fence_wait_duration;
            }
            if (recorder.get(frame_id - 2, joined_record)) {
                slop += std::max(0.0, joined_record.acquire_end - joined_record.acquire_begin);
                slop += std::max(0.0, joined_record.present_return_time - joined_record.present_request_time);
            }
            if (m_slop_servo_last_tick_time > 0.0) {
                m_slop_servo.update(slop, now_seconds - m_slop_servo_last_tick_time);
            }
            m_slop_servo_last_tick_time = now_seconds;
            const double sleep_seconds = m_slop_servo.get_sleep();
            if (sleep_seconds > 0.0) {
                recorder.set(frame_id, &erhe::frame_pacing::Frame_time_record::pacer_wait_begin, erhe::frame_pacing::Frame_time_recorder::now());
                m_pacer_release_timer.wait_for(sleep_seconds);
                recorder.set(frame_id, &erhe::frame_pacing::Frame_time_record::pacer_wait_end, erhe::frame_pacing::Frame_time_recorder::now());
            }
            m_last_predicted_display_time = 0.0;
        } else {
            m_slop_servo_last_tick_time   = 0.0;
            m_last_predicted_display_time = 0.0;
        }
        if (m_frame_pacing_observer.get_pacer() != nullptr) {
            // With a pacer the observer decided for the frame just started:
            // the decision's frame id is the frame this advance applies to.
            log_frame_pacing->debug(
                "FR4 display advance {} ns computed for frame {}, applied to prepare_update of frame {}",
                display_advance_ns, m_frame_pacing_observer.get_last_decision().frame_id, frame_id
            );
        }
        return display_advance_ns;
    }

    // M1, after the pacing waits: frame pacing verification UI
    // (doc/frame_pacing/user_interface.md). Collects this frame's decision
    // and records into the window's history, then runs the simulated
    // workload knob. The workload runs here so it lands inside the measured
    // CPU slot after the pacer waits - the records count it as CPU service
    // time and the pacer sees it as real load (U2).
    void frame_pacing_window_update()
    {
        m_frame_pacing_window->collect(
            m_graphics_device->get_frame_time_recorder(),
            m_frame_pacing_observer,
            m_graphics_device->get_display_refresh_duration_seconds(),
            erhe::frame_pacing::Frame_time_recorder::now()
        );
        m_frame_pacing_window->run_simulated_workload();
    }

    // R: the FR5 present-wait clamp (step P2.2) with the decision M1 made
    // for this frame (packet.present_wait_id, -1 when frame pacing is not
    // enforced or the decision has no wait): block until that frame has been
    // displayed, bounding pending presented images to Q* + F. Stamped as the
    // present_wait record pair (an involuntary wait; the recorder is
    // thread-safe). Runs in R, before the swapchain wait, because
    // vkWaitForPresentKHR is externally synchronized on the swapchain R
    // acquires and presents (doc/editor/render_thread.md section 2.4).
    void present_wait_clamp(const Frame_packet& packet)
    {
        if (packet.present_wait_id < 0) {
            return;
        }
        ERHE_PROFILE_SCOPE("present-wait clamp");
        const std::int64_t frame_id = static_cast<std::int64_t>(packet.device_frame_id);
        erhe::frame_pacing::Frame_time_recorder& recorder = m_graphics_device->get_frame_time_recorder();
        recorder.set(frame_id, &erhe::frame_pacing::Frame_time_record::present_wait_begin, erhe::frame_pacing::Frame_time_recorder::now());
        const erhe::graphics::Present_wait_result wait_result = m_graphics_device->wait_for_displayed_frame(
            packet.present_wait_id,
            100'000'000 // 100 ms bound; a timeout is a logged no-op, not an error
        );
        recorder.set(frame_id, &erhe::frame_pacing::Frame_time_record::present_wait_end, erhe::frame_pacing::Frame_time_recorder::now());
        if (wait_result == erhe::graphics::Present_wait_result::timeout) {
            log_frame_pacing->warn(
                "present-wait clamp timed out: frame {} wait_id {}",
                frame_id, packet.present_wait_id
            );
        }
    }

    // M1 overlap: the device frame start and frame pacing, the simulation
    // clock, xrWaitFrame, and the physics
    // simulation fixed steps. Runs while R of the previous frame may still
    // run on the render thread, so it writes nothing R reads: what R needs
    // from here travels in the packet (frame number, host time, the
    // Frame_timing R begins the OpenXR frame with), and the node flag edits
    // of the body activation callbacks are queued for M2. M2 iterates the
    // same per-frame step list again for the node-writing fixed-step
    // updates.
    // The frame start: Device::wait_for_frame_start(), in the position of
    // xrWaitFrame (doc/editor/render_thread.md section 2.3), opens the
    // frame's time record and returns its device frame id, then the frame
    // pacer decides the frame's schedule and waits for its release time
    // (frame_start_pacing(); the FR4 display advance it computes feeds
    // Time::prepare_update). run() runs it before its input poll, so the
    // poll is the last thing before the frame's body and the events that
    // arrive during the pacing wait reach M2 without waiting a frame;
    // m_frame_started marks the started frame awaiting its frame(). A
    // frame() that finds no started frame makes its own (the live-resize
    // redraw callback's frames, run from inside the poll: the first of them
    // takes the one run() started). The resource half (acquire_frame_slot)
    // is in M2, after the join.
    void start_frame()
    {
        if (m_frame_started) {
            return;
        }
        ERHE_PROFILE_SCOPE("frame start");
        Frame_packet& packet = m_frame_packet;
        erhe::log::set_breadcrumb("tick: wait_for_frame_start");
        m_frame_start_begin = std::chrono::steady_clock::now();
        packet.device_frame_id    = m_graphics_device->wait_for_frame_start();
        packet.display_advance_ns = frame_start_pacing(packet);
        // Benchmark mode: the simulation advances by exactly the benchmark's
        // frame_dt per measured frame (0 before), whatever the wall clock
        // and the pacer say, so every run simulates the same timeline. The
        // display advance is the one source of the frame's simulation
        // advance (Time::prepare_update and the animation player).
        if (m_benchmark_driver) {
            packet.display_advance_ns = m_benchmark_driver->get_frame_advance_ns();
        }
        m_phase_timing.frame_start_wait_us += elapsed_us(m_frame_start_begin, std::chrono::steady_clock::now());
        m_frame_started = true;
    }

    void frame_overlap_phase(Frame_packet& packet)
    {
        ERHE_PROFILE_FUNCTION();
        set_frame_phase(erhe::utility::Frame_phase::m1_overlap);
        const std::chrono::steady_clock::time_point m1_begin = std::chrono::steady_clock::now();

        // No command buffer is published during M1: recording here would
        // race the render phase once it runs on its own thread
        // (doc/editor/render_thread.md section 2.3).
        ERHE_VERIFY(m_app_context.current_command_buffer == nullptr);

        // The frame start run() made before its poll, or this frame's own.
        start_frame();
        m_frame_started = false;
        frame_pacing_window_update();

        // The simulation clock advances next, so the fixed step count is
        // known here; < 0 display advance = wall-clock fallback.
        m_time->prepare_update(m_frame_activity != Frame_activity::hidden, packet.display_advance_ns);
        packet.frame_number              = m_time->get_frame_number();
        packet.frame_host_system_time_ns = m_time->get_host_system_time_ns();

        packet.headset_frame_timing = erhe::xr::Frame_timing{};
#if defined(ERHE_XR_LIBRARY_OPENXR)
        if (m_app_context.OpenXR) {
            ERHE_PROFILE_SCOPE("OpenXR wait frame");
            // xrPollEvent is M2 (doc/editor/render_thread.md section 2.2):
            // the session state it applies is read by R's frame functions.
            // wait_frame() does not wait when the previous M2's poll failed.
            erhe::log::set_breadcrumb("tick: xr wait_frame");
            // Benchmark mode: the head pose of this frame is the camera
            // path's; the frame waited next carries it to R.
            if (m_benchmark_driver) {
                const Benchmark_pose pose = m_benchmark_driver->get_head_pose();
                m_headset_view->set_benchmark_head_pose(pose.position, pose.orientation);
            }
            packet.headset_frame_timing = m_headset_view->wait_frame();
            if (m_benchmark_driver && packet.headset_frame_timing.wait_ok) {
                m_benchmark_driver->set_xr_display_period(packet.headset_frame_timing.predicted_display_pediod);
            }
            if (!packet.headset_frame_timing.wait_ok) {
                ERHE_PROFILE_SCOPE("OpenXR sleep");
                // Throttle loop since xrWaitFrame won't be called.
                std::this_thread::sleep_for(std::chrono::milliseconds{250});
            }
        }
#endif

        erhe::log::set_breadcrumb("tick: fixed_step (physics)");
        m_app_scenes->before_physics_simulation_steps();

        packet.host_system_dt_s    = 0.0f;
        packet.host_system_time_ns = 0;
        m_time->for_each_fixed_step(
            [this, &packet](const Time_context& time_context) {
                ERHE_PROFILE_SCOPE("fixed step physics simulation");
                packet.host_system_dt_s += time_context.host_system_dt_s;
                packet.host_system_time_ns = time_context.host_system_time_ns;
                m_app_scenes->update_physics_simulation_fixed_step(time_context);
            }
        );

        m_phase_timing.m1_us += elapsed_us(m1_begin, std::chrono::steady_clock::now());
    }

    // M2 prepare: everything that precedes the rendergraph and writes state
    // the render phase reads.
    void frame_prepare_phase(Frame_packet& packet)
    {
        ERHE_PROFILE_FUNCTION();
        set_frame_phase(erhe::utility::Frame_phase::m2_prepare);
        const std::chrono::steady_clock::time_point m2_begin = std::chrono::steady_clock::now();
        // The M2 sub-terms of the phase timing log: each call closes the
        // term that began at the previous call (or at m2_begin).
        std::chrono::steady_clock::time_point m2_mark = m2_begin;
        auto take_m2_term = [&m2_mark](double& accumulator) {
            const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            accumulator += elapsed_us(m2_mark, now);
            m2_mark = now;
        };

        // Input event dispatch: the events run()'s last poll_events queued.
        // Once per poll: a live-resize redraw frame, run from inside
        // poll_events, finds the previous poll's events, which were
        // dispatched already. Before Device::acquire_frame_slot, as when
        // run() dispatched them before the frame.
        erhe::log::set_breadcrumb("tick: dispatch input events");
        if (m_input_events_pending) {
            ERHE_PROFILE_SCOPE("dispatch events");
            m_input_events_pending = false;
            std::vector<erhe::window::Input_event>& input_events = m_window->get_input_events();
            for (erhe::window::Input_event& input_event : input_events) {
                dispatch_input_event(input_event);
            }
            if (m_window_resize_event.has_value()) {
                m_request_resize_pending.store(true);
                m_window_resize_event.reset();
                m_last_window_width  = m_window->get_width();
                m_last_window_height = m_window->get_height();
            }
#if defined(ERHE_OS_ANDROID)
            // Foreground / device-reset arrived while we were
            // suspended. The previous VkSurfaceKHR is bound to a
            // now-detached ANativeWindow; rebuild the surface and
            // swapchain over the freshly-bound ANativeWindow that
            // SDL has wired into Context_window. Surface_impl::
            // recreate_for_new_window keeps the Swapchain C++
            // object identity stable so cached raw Swapchain*
            // pointers in Render_pass_impl (and therefore in
            // Window_imgui_host::m_render_pass) stay valid; the
            // pipeline cache and bindless texture heap are keyed
            // on format / hold no swapchain-derived handles, so
            // they survive the rebuild without an explicit
            // invalidation pass. m_request_resize_pending is also
            // set so update_swapchain re-queries the new surface
            // extent and rebuilds the underlying VkSwapchainKHR
            // on the next frame.
            if (m_window->consume_swapchain_dirty()) {
                // The swapchain belongs to the render phase: with frame
                // pipelining the previous frame's may still present into
                // it, so it is joined first (a rare event; the kick of this
                // frame then finds nothing outstanding).
                join_render_phase();
                if (!m_graphics_device->recreate_surface_for_new_window()) {
                    log_startup->warn("Surface recreate failed; retrying next frame");
                }
                m_request_resize_pending.store(true);
                m_last_window_width  = m_window->get_width();
                m_last_window_height = m_window->get_height();
            }
#endif
        }

        // Breadcrumbs mark the current main-loop phase so the watchdog can
        // report where a spinning tick is stuck. See
        // doc/erhe/geogram.md.
        //
        // The resource half of the device frame start (doc/editor/
        // render_thread.md section 2.3): the timeline wait for the slot of
        // this frame and its command pool resets. The slot's previous user,
        // two frames back, was joined by the previous kick (with frame
        // pipelining the previous frame's render phase may still be
        // running, in the other slot); from here on the main role's device
        // frame index is the frame M1 started.
        erhe::log::set_breadcrumb("tick: acquire_frame_slot");
        const std::chrono::steady_clock::time_point slot_acquire_begin = std::chrono::steady_clock::now();
        const bool acquire_ok = m_graphics_device->acquire_frame_slot(packet.device_frame_id);
        ERHE_VERIFY(acquire_ok);
        m_phase_timing.slot_acquire_us += elapsed_us(slot_acquire_begin, std::chrono::steady_clock::now());
        // Benchmark mode: acquire_frame_slot() just stored the GPU timer
        // results of the frame that last held this slot; the driver reads
        // them keyed by their frame id.
        if (m_benchmark_driver) {
            m_benchmark_driver->sample_gpu_timers();
        }

        packet.upload_command_buffer = nullptr;

        // Power saving: when the window is not visible (minimized/occluded/
        // hidden) skip all GPU rendering this frame -- do not acquire or present
        // the swapchain and do not execute the rendergraph. The device frame is
        // still opened (acquire_frame_slot, above) and closed (end_frame, in the render
        // phase) cleanly. Pacing is handled by the poll_events wait in run(),
        // so no sleep is added here. The simulation is also paused via
        // prepare_update in the overlap phase. The swapchain and the frame
        // command buffer belong to the render phase (doc/editor/render_thread.md
        // section 2.4): M2 touches neither.
        packet.should_render = (m_frame_activity != Frame_activity::hidden);

        // The main thread's upload command buffer, published as
        // App_context::current_command_buffer for the rest of M2 (doc/editor/
        // render_thread.md section 2.3). It is recording for the WHOLE
        // prepare phase even when nothing is rendered this frame (hidden
        // window / locked session): paths outside the rendergraph record GPU
        // work - Hotbar's deferred init_hotbar (rendertarget mesh uploads),
        // MCP-queued actions, operations - and recording into a never-begun
        // cb is a driver crash (observed live: launch-while-locked, first
        // hidden tick, Hotbar::init_hotbar -> Mesh_memory::flush -> null-deref
        // inside vkCmdCopyBuffer). It ends at the end of M2; the render phase
        // submits it first, device-only, with a GPU edge to the frame cb.
        take_m2_term(m_phase_timing.m2_start_us);
        packet.upload_command_buffer = &m_graphics_device->get_command_buffer(s_main_upload_thread_slot);
        erhe::graphics::Command_buffer& command_buffer = *packet.upload_command_buffer;
        command_buffer.begin();
        m_app_context.current_command_buffer = &command_buffer;

        // The shared empty material set first: passes that carry no material
        // source bind it, the previews below among them, and a set is bound
        // only in frames its update() published it for
        // (doc/erhe/draw_list_material_set.md D9). It never changes, so this
        // only publishes.
        if (m_material_set_factory) {
            m_material_set_factory->update_empty_material_set(command_buffer);
        }

        // The operations the windows and menus deferred out of the ImGui
        // iteration (Imgui_windows::queue: window and viewport creation,
        // menu commands, imports, undo / redo buttons) run here, before
        // anything of this frame is prepared or rendered, with the upload
        // command buffer open (doc/editor/render_thread.md section 2.6).
        erhe::log::set_breadcrumb("tick: flush imgui window queue");
        m_imgui_windows->flush_queue();

        // log_input_frame->trace("----------------------- Editor::tick() -----------------------");

        std::vector<erhe::window::Input_event>& input_events = m_window->get_input_events();

        // Land worker-produced scene mutations (deferred glTF finalize:
        // raytrace / Buffer_mesh swaps and raytrace instance rebuilds) in one
        // place, before anything else in this tick reads or edits scenes.
        // Everything below - pointer / hover raytrace, physics, commands,
        // MCP, operations, transforms, draw list flush, rendering - then
        // sees scenes that only the main thread changes.
        erhe::log::set_breadcrumb("tick: scene_commit_queue flush");
        m_scene_commit_queue.flush();

        // Advance asynchronous asset loads a bounded amount
        // (doc/editor/async_asset_loading_design.md). This sits right after the commit
        // queue so that worker results land first, and it runs on hidden ticks
        // too: the command buffer is recording for the whole tick either way
        // (see the begin() above), so a load keeps streaming while the window
        // is occluded instead of stalling until it comes back.
        erhe::log::set_breadcrumb("tick: asset loads");
        {
            Frame_load_budget budget{m_app_context.editor_settings->load};
            Asset_load_tick_context asset_load_tick_context{
                .app_context                 = m_app_context,
                .graphics_device             = *m_graphics_device,
                .command_buffer              = command_buffer,
                .executor                    = *m_app_context.executor,
                .budget                      = budget,
                .max_decoded_bytes_in_flight = static_cast<std::size_t>(
                    std::max(0, m_app_context.editor_settings->load.max_decoded_bytes_in_flight)
                ),
                .max_upload_staging_bytes = static_cast<std::size_t>(
                    std::max(1, m_app_context.editor_settings->load.max_upload_staging_bytes)
                )
            };
            m_asset_manager->tick(asset_load_tick_context);
        }

        // Standalone image files (asset browser hover previews, texture
        // imports into a content library) upload the same way: the decode ran
        // on a worker, the texture is created and its copies recorded here,
        // against this tick's command buffer, before any render pass begins.
        erhe::log::set_breadcrumb("tick: texture file loads");
        m_texture_file_loader->tick(command_buffer);
        take_m2_term(m_phase_timing.m2_upload_us);

        m_time->update_transform_animations(*m_app_message_bus.get());
        // Scene animations advance on the same clock as the simulation:
        // predicted display delta when pacing is active (P2.4), wall clock
        // otherwise.
        const int64_t animation_advance_ns = (packet.display_advance_ns >= 0) ? packet.display_advance_ns : m_time->get_host_system_last_frame_duration_ns();
        m_animation_player->update(static_cast<float>(animation_advance_ns) * 1.0e-9f);
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
        // xrPollEvent first: the session state transitions it applies
        // (begin / end session, boundary visibility) are read by R's frame
        // functions, so they land here, where R is idle; M1 of the next
        // frame waits (or not) on the outcome. xrSyncActions needs a
        // waited frame, not a begun one: R runs xrBeginFrame
        // (doc/editor/render_thread.md section 2.2).
        if (m_app_context.OpenXR) {
            ERHE_PROFILE_SCOPE("OpenXR poll events");
            erhe::log::set_breadcrumb("tick: xr poll_events");
            static_cast<void>(m_headset_view->poll_events());
        }
        if (m_app_context.OpenXR && packet.headset_frame_timing.wait_ok) {
            ERHE_PROFILE_SCOPE("OpenXR update events");
            erhe::log::set_breadcrumb("tick: xr update_actions");
            const bool headset_update_actions_ok = m_headset_view->update_actions(packet.headset_frame_timing);
            if (headset_update_actions_ok && m_benchmark_driver) {
                sample_benchmark_xr_counters();
            }
            if (headset_update_actions_ok) {
                // TOOD m_viewport_config_window->set_edit_data(&m_headset_view->get_config());
                // TODO m_scene_view_config_window
            } else {
                ERHE_PROFILE_SCOPE("OpenXR sleep");
                // Throttle loop when the frame was waited but cannot be
                // used; the overlap phase throttles when xrWaitFrame
                // failed.
                std::this_thread::sleep_for(std::chrono::milliseconds{250});
            }
        }
#endif

        // The node-writing fixed-step updates for the steps the overlap
        // phase simulated, then the physics write-back.
        erhe::log::set_breadcrumb("tick: fixed_step (nodes)");
        // This frame's step list, filled by Time::prepare_update in M1.
        m_time->for_each_fixed_step(
            [this](const Time_context& time_context) {
                ERHE_PROFILE_SCOPE("fixed step update");
                m_headset_view   ->update_fixed_step();
                m_fly_camera_tool->update_fixed_step(time_context);
            }
        );
        m_app_scenes   ->after_physics_simulation_steps();
        take_m2_term(m_phase_timing.m2_sim_us);
        erhe::log::set_breadcrumb("tick: imgui process_events + commands");
        m_imgui_windows->process_events(packet.host_system_dt_s, packet.host_system_time_ns);
        m_commands     ->tick(packet.host_system_time_ns, input_events);

        // Process any requests queued by the MCP server
        if (m_mcp_server) {
            ERHE_PROFILE_SCOPE("MCP server");
            m_mcp_server->process_queued_requests();
        }

        // Once per frame updates
        m_network_window->update_network();

        // The hotbar slots the windows requested (Hotbar::set_slots) are
        // rebuilt here; the ImGui windows themselves draw in the ImGui
        // phase (frame_imgui_phase), after this prepare phase.
        m_hotbar->rebuild_if_needed();
        take_m2_term(m_phase_timing.m2_imgui_us);

        // The thumbnails the windows above requested (Thumbnails::draw) render
        // their previews here, into the upload command buffer, before the
        // ImGui hosts sample them in the render phase (the upload command
        // buffer is submitted first, with a GPU edge to the frame command
        // buffer). The Material_preview and Brush_preview objects are then
        // driven by the main thread only (doc/editor/render_thread.md
        // section 2.6). Rendered frames only: a hidden frame samples nothing.
        if (packet.should_render) {
            ERHE_PROFILE_SCOPE("Thumbnails");
            erhe::log::set_breadcrumb("tick: thumbnails update");
            m_thumbnails->update(command_buffer);
        }
        // The requests the windows made at ImGui time for render-side objects
        // (the Properties material preview and BRDF slice, the Depth
        // Visualization window's node) are served here, on the main thread,
        // before the kick (doc/editor/render_thread.md section 2.6).
        if (m_material_preview && packet.should_render) {
            erhe::log::set_breadcrumb("tick: material preview update");
            m_material_preview->update(command_buffer);
        }
        if (m_brdf_slice) {
            m_brdf_slice->update();
        }
        if (m_debug_view_window) {
            m_debug_view_window->update();
        }

        // - Apply all command bindings (OpenXR bindings were already executed above)

        //m_fly_camera_tool->update_once_per_frame(timestamp);

        erhe::log::set_breadcrumb("tick: update_hover_info");
        auto* imgui_host = m_imgui_windows->get_window_imgui_host().get(); // get window hosted viewport
        if (imgui_host != nullptr) {
            m_viewport_scene_views->update_hover_info(imgui_host); // updates what viewport window is being hovered
        }

        m_operation_stack->update();

        // Geometry graph background evaluation: apply results of a
        // completed run and launch a new one when the graph is dirty
        // (after the operation stack so this frame's edits are seen).
        m_geometry_graph_window->update_evaluation();

        // Texture graph: synchronous dirty-flag evaluation (cheap GLSL
        // composition, no background engine), run once per frame so the graph
        // stays current even when the window is hidden.
        m_texture_graph_window->update();

        // Issue #252: destroy any extra Properties / graph windows the user
        // closed this frame (outside ImGui iteration; the primary instances
        // are not managed here).
        m_editor_windows->update_once_per_frame();

        // - Execute all fixes step updates
        // - Execute all once per frame updates
        //    - App_scenes (updates physics)
        //    - Fly_camera_tool
        //    - Network_window
        // Announce content removed this frame (undo of a glTF import, node
        // deletes, scene unregistration) before the pump, so subscribers drop
        // their cached references in the same frame the removal happened. Here
        // and not at the producers: this is outside ImGui iteration and
        // outside the Content_library / Item_host mutexes the producing
        // operations hold (doc/editor/import_undo_reference_clearing.md).
        if (m_asset_manager) {
            m_asset_manager->flush_pending_removals();
        }

        m_app_message_bus->update(); // Flushes queued messages

        // Release completed per-item async task handles: a retained handle
        // keeps the task lambda's captures (scene root, mesh nodes) alive
        // past completion and would pin a closed scene's content (see
        // items.cpp).
        purge_completed_item_async_tasks();

        // Scene-close leak watchdog: arms watches for scenes closed by the
        // pump above (after EVERY close_scene subscriber has run) and checks
        // previously armed watches.
        update_scene_close_leak_watches();

        // Apply physics updates

        m_fly_camera_tool->on_frame_end();

        lightmap_commit_phase();
        take_m2_term(m_phase_timing.m2_update_us);

        // Unconditional: uploads enqueued during a hidden tick (deferred
        // hotbar init, MCP-driven edits while the session is locked) must
        // not sit in the queue until the next rendered frame.
        m_mesh_memory->flush(command_buffer);

        packet.frame_begin_info = erhe::graphics::Frame_begin_info{
            .resize_width   = static_cast<uint32_t>(m_last_window_width),
            .resize_height  = static_cast<uint32_t>(m_last_window_height),
            .request_resize = m_request_resize_pending.load()
        };
        m_request_resize_pending.store(false);
        packet.window_width  = m_window->get_width();
        packet.window_height = m_window->get_height();

        // Arrange layout-node children, then update scene transforms.
        erhe::log::set_breadcrumb("tick: update_layout_nodes");
        m_app_scenes->update_layout_nodes();
        erhe::log::set_breadcrumb("tick: update_transforms");
        m_viewport_scene_views->update_transforms();
        if (m_app_context.OpenXR) {
            m_headset_view->update_transforms();
        }
        // Place the hotbar quad in front of the hovered view's camera. Needs
        // this frame's camera world transform (above) and must land before
        // flush_draw_lists() below, or the draw list records would carry the
        // previous frame's transform and the hotbar would trail the camera by
        // one frame. It re-propagates the hovered scene's transforms itself so
        // the quad's child mesh prim picks the write up this frame (see
        // Hotbar::update_once_per_frame).
        erhe::log::set_breadcrumb("tick: hotbar update");
        m_hotbar->update_once_per_frame();

        if (m_transform_update_stats_tracker) {
            m_transform_update_stats_tracker->sample_frame(*m_app_scenes.get());
        }

        // Apply queued draw list changes (register / unregister / flags from
        // this frame's edits and async loads) after transform propagation
        // (registration samples node world transforms, R10b) and before the
        // rendergraph renders any Draw_list_scene-owning root (viewports,
        // shadow nodes, headset). Thumbnails (render phase) render preview roots,
        // which have no Draw_list_scene. Main thread only.
        // Rebuild the primitives of meshes whose Gprim.display_color changed,
        // before the flush that re-registers them: the rebuild swaps
        // primitives, which enqueues a re-register of its own.
        erhe::log::set_breadcrumb("tick: rebuild_display_colors");
        m_app_scenes->rebuild_display_colors();

        // The card proxies of the draw modes whose values or extent changed,
        // beside the display colors and for the same reason: the build
        // inserts prims, so it must not run from the change site.
        erhe::log::set_breadcrumb("tick: rebuild_draw_mode_proxies");
        m_app_scenes->rebuild_draw_mode_proxies();

        erhe::log::set_breadcrumb("tick: flush_draw_lists");
        m_app_scenes->flush_draw_lists();

        // The settings the render phase reads, copied after every window and
        // operation of this frame edited the live ones (doc/editor/
        // render_thread.md section 2.6): the editor-global copy and each
        // scene's resolved overrides.
        erhe::log::set_breadcrumb("tick: snapshot_render_settings");
        m_render_settings.snapshot(m_graphics_device->get_frame_index(), m_editor_settings, m_app_settings);
        m_app_scenes->snapshot_render_settings();
#if defined(ERHE_XR_LIBRARY_OPENXR)
        if (m_app_context.OpenXR) {
            // Every frame: the headset's Shadow_render_node runs in every
            // frame's rendergraph and reads this frame's Render_view, also
            // on frames update_actions() did not run or returned early. Taken
            // here, after the message bus pump, like the viewports' copies
            // below: a scene closed or a re-home of the headset this frame
            // (Editor::home_tools()) is already reflected, so the copy never
            // names a scene that took no render settings snapshot above.
            m_headset_view->prepare_render_view();
        }
#endif

        // The tools, renderables and debug visualizations of every viewport
        // record their lines and labels here, after this frame's transforms,
        // hover and selection are final, into one debug / text renderer
        // frame slot per view; the render phase expands and draws the slots
        // (doc/editor/render_thread.md section 2.2). Only for rendered frames:
        // a hidden frame runs no rendergraph, so nothing would draw them.
        if (packet.should_render) {
            erhe::log::set_breadcrumb("tick: prepare_render");
            m_viewport_scene_views->prepare_render(command_buffer);
            // Every shadow node copies its requested configuration (with the
            // preset's shadow technique) into this frame's slot; the render
            // phase of the frame applies it before rendering into its maps.
            m_app_rendering->prepare_shadow_render_nodes();
        }

        // Material slot spaces, after the flush that settles membership and
        // before the first pass that binds one - the DDGI tick and the
        // rendergraph (which is also where XR renders) in the render phase
        // (doc/erhe/draw_list_material_set.md D6). Each update publishes the
        // buffer copy and texture heap the render phase of this frame binds
        // (D9). The preview roots and the BRDF slice window are not in
        // m_scene_roots and run their own sync + flush + update at their
        // render entry points; the empty set was published at the start of
        // this phase.
        if (m_app_context.current_command_buffer != nullptr) {
            erhe::log::set_breadcrumb("tick: update_material_sets");
            m_app_scenes->update_material_sets(*m_app_context.current_command_buffer);
        }

        if (packet.should_render) {
            lightmap_prepare_phase();

            // The bucket passes' draws, prepared per view into the frame's
            // composer slot (doc/editor/editor.md "Frame phases and the
            // render thread"). The LAST step of M2: after the material sets
            // settled the forward set's slots and the lightmap prepare phase
            // the per-primitive lightmap uv transforms, which the records
            // carry, and after every tool and operation of the frame moved,
            // flagged or created what the passes draw.
            erhe::log::set_breadcrumb("tick: prepare_composition");
            m_app_rendering->prepare_composition(
                m_graphics_device->get_frame_index(),
                m_graphics_device->get_number_of_frames_in_flight(),
                packet.frame_host_system_time_ns
            );

            // The rendergraph nodes copy what their execute of this frame
            // reads (the post-processing parameters, the BRDF slice angles)
            // and the enabled flags, after every edit and prepare step of the
            // frame; the render phase runs the nodes enabled in this copy
            // (Rendergraph::prepare_frame()). An enable, disable or slider
            // edit made in the ImGui phase reaches the next frame.
            erhe::log::set_breadcrumb("tick: rendergraph prepare_frame");
            m_rendergraph->prepare_frame();
        }

        // The lightmap atlas the forward passes of this frame sample, as the
        // lightmap prepare phase or an MCP direct bake chose it; R binds it
        // from the packet (App_rendering::set_lightmap_texture).
        packet.lightmap_texture = m_app_rendering->get_lightmap_texture();
        // The ray trace switch the Ray Trace window / MCP set last; R hands it
        // to the renderer, which traces with this copy.
        packet.ray_trace_enabled = m_ray_trace_renderer && m_ray_trace_renderer->is_enabled();
        // The scene the indirect diffuse producers of this frame tick on;
        // null with no scene or several.
        packet.indirect_diffuse_scene_root = m_app_scenes->get_single_scene_root();

        // The ray-query consumers' view of each scene, published into the
        // frame's slot after the forward material sets settled their slots.
        // Every frame, hidden ones included: the indirect diffuse ticks run in
        // the render phase whether or not the frame renders.
        if (m_graphics_device->get_info().use_ray_query) {
            erhe::log::set_breadcrumb("tick: prepare_ray_trace_scenes");
            m_app_scenes->prepare_ray_trace_scenes();
        }

        // The uploads of this frame are recorded; the render phase submits
        // them. No command buffer is published between the phases.
        command_buffer.end();
        m_app_context.current_command_buffer = nullptr;

        take_m2_term(m_phase_timing.m2_scene_us);
        m_phase_timing.m2_us += elapsed_us(m2_begin, std::chrono::steady_clock::now());
    }

    // M2: the lightmap work that commits scene content
    // (doc/editor/lightmap_baking.md section 3a "Frame phases"). Before
    // Mesh_memory::flush, so the vertex uploads of a partition commit ride
    // this frame's upload command buffer, which the render phase submits
    // before the next prepare phase can raster a G-buffer from them (the
    // baker's one-tick G-buffer defer relies on it). After the operation
    // stack, so the commit-time validation sees this frame's edits. Runs on
    // hidden frames too.
    void lightmap_commit_phase()
    {
        if (m_lightmap_partitioner) {
            // Commit or discard a finished async prepare job; the baker's
            // piece-hash change then triggers the relayout + deferred
            // G-buffer bake in lightmap_prepare_phase().
            erhe::log::set_breadcrumb("tick: lightmap partition commit");
            m_lightmap_partitioner->update();
        }
        if (m_lightmap_window) {
            // Deferred window requests: offline tile bake, save-on-evict
            // and restore-on-activate drains (standalone readback / upload
            // submits), auto re-prepare, Reorder Charts By Bake. Outside
            // ImGui iteration, before this frame's tick is planned.
            erhe::log::set_breadcrumb("tick: lightmap window update");
            m_lightmap_window->update();
        }
        // A finished offline bake left fresh tiles on disk; make the
        // streamer reload them.
        if (m_lightmap_baker && m_lightmap_streamer) {
            const bool offline_active = m_lightmap_baker->is_offline_bake_active();
            if (m_lightmap_offline_was_active && !offline_active) {
                m_lightmap_streamer->invalidate();
            }
            m_lightmap_offline_was_active = offline_active;
        }
    }

    // M2, rendered frames: plan this frame's lightmap work against the
    // settled scene (after transforms, draw list flush and material sets):
    // paused change detection, the baker's tick decisions and region
    // mapping writes (Lightmap_baker::prepare_tick), or the disk streamer's
    // residency, tile uploads and mapping writes, and the lightmap binding
    // (App_rendering::set_lightmap_texture). The render phase records the
    // baker's tick slot of its frame (Lightmap_baker::record_tick) and binds
    // the atlas the frame packet carries.
    void lightmap_prepare_phase()
    {
        // The lightmap settings reach the renderer / baker / streamer
        // through apply_lightmap_settings() at their edit sites.
        const Lightmap_config& lightmap_config = m_app_context.editor_settings->lightmap;
        // Camera for tile residency ranking (both the interactive baker's
        // clamp and the disk streamer): the last-used viewport camera,
        // falling back to the scene's first camera; no camera at all = keep
        // current residency / bake all tiles.
        const std::shared_ptr<Scene_root> lightmap_scene_root = m_app_context.selection->get_active_scene_root();
        glm::vec3  lightmap_camera_position{0.0f};
        glm::vec3* lightmap_camera_position_ptr{nullptr};
        if (lightmap_scene_root) {
            std::shared_ptr<erhe::scene::Camera> camera{};
            if (m_viewport_scene_views) {
                const std::shared_ptr<Viewport_scene_view> scene_view = m_viewport_scene_views->last_scene_view();
                camera = scene_view ? scene_view->get_camera() : nullptr;
            }
            if (!camera) {
                const std::vector<std::shared_ptr<erhe::scene::Camera>>& cameras = lightmap_scene_root->get_scene().get_cameras();
                if (!cameras.empty()) {
                    camera = cameras.front();
                }
            }
            const erhe::scene::Node* const camera_node = camera ? camera.get() : nullptr;
            if (camera_node != nullptr) {
                lightmap_camera_position     = glm::vec3{camera_node->world_from_node()[3]};
                lightmap_camera_position_ptr = &lightmap_camera_position;
            }
        }
        // Paused change detection: scene edits made while NOT baking must
        // not keep showing pre-edit lighting (the tick's hash checks only
        // run while baking); on a change the baker white-outs its display
        // and takes the binding below.
        if (m_lightmap_baker && !m_lightmap_baker->is_baking_enabled() && lightmap_scene_root) {
            m_lightmap_baker->monitor_paused_scene(*lightmap_scene_root.get());
        }
        if (m_lightmap_baker && m_lightmap_baker->is_baking_enabled()) {
            m_lightmap_baker_owned_binding = true;
            if (lightmap_scene_root) {
                // Procedural sky lighting for the bake: same per-scene sky
                // resolution + sun direction the viewport uses, so the baked
                // environment matches the rendered background. Only
                // atmosphere mode (mode == 1) contributes; the LUTs exist
                // once a viewport has rendered the atmosphere at least once
                // (ensure_luts), and the baker's lighting hash resets
                // accumulation when this state changes.
                {
                    const Sky_config& sky_config = get_effective_sky(*m_app_context.editor_settings, *lightmap_scene_root);
                    Lightmap_baker::Sky_lighting sky{};
                    sky.enabled =
                        sky_config.enabled &&
                        (sky_config.mode == 1) &&
                        m_sky_renderer &&
                        m_sky_renderer->is_atmosphere_supported() &&
                        m_sky_luts_ready; // as of the last rendered frame (Frame_result::sky_luts_ready)
                    if (sky.enabled) {
                        sky.transmittance_lut = m_sky_renderer->get_transmittance_lut();
                        sky.multiscatter_lut  = m_sky_renderer->get_multiscatter_lut();
                        sky.sun_direction_and_intensity = glm::vec4{
                            Sky_renderer::resolve_sun_direction(sky_config, &lightmap_scene_root->get_render_settings(m_graphics_device->get_frame_index())),
                            sky_config.sun_intensity
                        };
                        sky.sky_params = glm::vec4{
                            static_cast<float>(sky_config.march_steps),
                            sky_config.observer_altitude_km,
                            0.0f,
                            0.0f
                        };
                    }
                    m_lightmap_baker->set_sky_lighting(sky);
                }
                erhe::log::set_breadcrumb("tick: lightmap bake prepare");
                m_lightmap_baker->prepare_tick(
                    *lightmap_scene_root.get(),
                    lightmap_config.uv_min_chart_texels,
                    lightmap_camera_position_ptr,
                    lightmap_config.active_tile_budget
                );
                m_app_rendering->set_lightmap_texture(m_lightmap_baker->get_lightmap_texture());
            }
        } else if (
            m_lightmap_streamer &&
            lightmap_scene_root &&
            (!m_lightmap_baker ||
             (!m_lightmap_baker->is_offline_bake_active() &&
              !m_lightmap_baker->has_unsaved_published_display() &&
              !m_lightmap_baker->is_display_content_stale()))
        ) {
            // Baked-tile streaming from <scene>.lightmap/: whenever the
            // interactive / offline baker does not own the lightmap binding,
            // the streamer keeps the N nearest tiles resident and binds its
            // atlas. A PAUSED baker with UNSAVED published content keeps the
            // binding (Stop = Pause: the viewport must keep showing the
            // in-progress bake, not swap to the disk set); once everything
            // published is saved the streamer owns again (same pixels) and
            // disk tile streaming resumes.
            erhe::log::set_breadcrumb("tick: lightmap stream");
            if (m_lightmap_baker_owned_binding) {
                // Ownership handoff baker -> streamer: the baker's
                // publish_regions overwrote the primitives' uv mappings with
                // baker display slots; re-push the streamer's (residency is
                // untouched, so this is cheap and covers the no-autosave
                // pause where no invalidate() ran).
                m_lightmap_baker_owned_binding = false;
                m_lightmap_streamer->reapply_regions(*lightmap_scene_root.get());
            }
            m_lightmap_streamer->update(*lightmap_scene_root.get(), lightmap_camera_position_ptr);
            if (m_lightmap_streamer->has_resident_tiles()) {
                m_app_rendering->set_lightmap_texture(m_lightmap_streamer->get_texture());
            }
        } else if (
            m_lightmap_baker &&
            m_lightmap_baker->is_display_content_stale() &&
            m_lightmap_baker->get_lightmap_texture()
        ) {
            // Paused-and-stale: the monitor white-cleared the baker display
            // and re-published its mappings; actively rebind it (the
            // streamer may have held the binding since the pause autosave).
            // The baker owns the binding until Start.
            m_lightmap_baker_owned_binding = true;
            m_app_rendering->set_lightmap_texture(m_lightmap_baker->get_lightmap_texture());
        }
    }

    // R render: the present-wait clamp, the swapchain wait and acquire, the
    // rendering work, the rendergraph, submit and Device::end_frame. Runs on the render role thread (the
    // main thread in inline mode) through Render_thread::kick(), which
    // sets the render role's phase to r_render for its duration. Reports
    // through out_result; writes no main-thread-only state (Phase_timing is
    // added by frame() after the join).
    void frame_render_phase(const Frame_packet& packet, Frame_result& out_result)
    {
        ERHE_PROFILE_FUNCTION();
        set_render_frame_phase(erhe::utility::Frame_phase::r_render);
        const std::chrono::steady_clock::time_point render_begin = std::chrono::steady_clock::now();
        m_render_phase_draw_data_wait_us = 0.0;
        out_result.device_frame_id = packet.device_frame_id;

#if defined(ERHE_XR_LIBRARY_OPENXR)
        // xrBeginFrame (doc/editor/render_thread.md section 2.2) with the
        // Frame_timing M1's xrWaitFrame produced. Headset_view keeps the
        // timing as render-phase state for render_headset() and end_frame;
        // a frame whose wait failed stores the failed timing and does not
        // begin.
        if (m_app_context.OpenXR) {
            erhe::log::set_breadcrumb("tick: xr begin_frame");
            m_headset_view->begin_frame(packet.headset_frame_timing);
        }
#endif

        erhe::graphics::Command_buffer& upload_command_buffer = *packet.upload_command_buffer;

        // The lightmap atlas M2 chose for this frame, and its filtering from
        // the frame's Render_settings copy: the forward passes of this render
        // phase sample it.
        m_forward_renderer->set_lightmap_texture(packet.lightmap_texture);
        m_forward_renderer->set_lightmap_bicubic(m_app_context.render_settings->lightmap_bicubic_sampling);
        if (m_ray_trace_renderer) {
            m_ray_trace_renderer->set_render_enabled(packet.ray_trace_enabled);
        }

        // This frame's command buffer, from the render thread slot. The
        // render role's device frame (Device::get_frame_index() is
        // role-keyed) is the packet's frame.
        erhe::graphics::Command_buffer& command_buffer = m_graphics_device->get_command_buffer(s_render_thread_slot);
        ERHE_VERIFY(command_buffer.get_frame_id() == packet.device_frame_id);
        erhe::graphics::Frame_state frame_state{};
        bool                        should_render = packet.should_render;

        // The swapchain belongs to R (doc/editor/render_thread.md section
        // 2.4): VkSwapchainKHR is host-synchronized in vkWaitForPresentKHR,
        // vkAcquireNextImageKHR and vkQueuePresentKHR. First the FR5
        // present-wait clamp with the frame id M1's pacer decision chose,
        // then the swapchain wait, before the frame command buffer begins
        // (Vulkan: the slot fence wait and the device-frame command pool
        // reset of Swapchain_impl::setup_frame / the emulated swapchain's
        // wait_frame). Under OpenXR the headset owns display and the
        // desktop swapchain is not engaged; the frame cb is begun with no
        // swapchain bound to it.
        erhe::log::set_breadcrumb("tick: present-wait clamp");
        present_wait_clamp(packet);
        if (should_render && !m_app_context.OpenXR) {
            ERHE_PROFILE_SCOPE("wait for swapchain");
            erhe::log::set_breadcrumb("tick: wait_for_swapchain");
            const std::chrono::steady_clock::time_point wait_swapchain_begin = std::chrono::steady_clock::now();
            const bool wait_swap_ok = command_buffer.wait_for_swapchain(frame_state);
            out_result.wait_for_swapchain_us = elapsed_us(wait_swapchain_begin, std::chrono::steady_clock::now());
            if (!wait_swap_ok) {
                // No swapchain image (doc/editor/render_thread.md section
                // 2.4): the frame continues like a hidden one -- the uploads
                // M2 recorded and the frame command buffer are submitted
                // device-only, the frame slots M2 filled are released, and
                // nothing is presented. The main thread sleeps after the
                // join that reports it.
                should_render           = false;
                out_result.no_swapchain = true;
            }
        }

        // FR3 target present time and C15 holdback deadline of this frame,
        // from the pacer decision M1 made (0 = none). Handed to the device
        // here, on the thread that submits and presents, so M1 of the next
        // frame never writes device state this phase reads.
        m_graphics_device->set_present_target_time(
            static_cast<std::int64_t>(packet.device_frame_id),
            packet.present_target_time,
            packet.present_hold_until
        );
        m_graphics_device->set_present_prewarm(static_cast<std::int64_t>(packet.device_frame_id), packet.present_prewarm);

        // Rendering
        m_app_rendering->process_start_capture();

        // Submit the prepare phase uploads first, device-only (no swapchain
        // engaged), with a GPU edge to the frame command buffer: the frame
        // cb's GPU work starts only after the uploads completed. Under
        // OpenXR the frame cb is the setup cb Xr_session::render_frame*()
        // fans its view cbs out from, so the edge covers them too
        // (doc/editor/render_thread.md section 2.3). The consumer-side wait
        // registers after begin() (Metal encodes it at that point).
        command_buffer.begin();
        upload_command_buffer.signal_gpu_completion_to(command_buffer);
        command_buffer.wait_for_gpu_completion_signal();
        {
            ERHE_PROFILE_SCOPE("submit upload command buffer");
            erhe::graphics::Command_buffer* upload_cbs[] = { &upload_command_buffer };
            m_graphics_device->submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{upload_cbs});
        }
        // R records into the frame command buffer, which it hands to its
        // recorders explicitly. App_context::current_command_buffer is the
        // main thread's (the upload command buffer during M2, null
        // otherwise): M1 of the next frame runs while R does, so R neither
        // writes nor reads it (doc/editor/render_thread.md section 2.3).
        // The render-phase time comes from the packet for the same reason:
        // M1 advances Time while R runs.
        m_app_context.render_phase_host_system_time_ns = packet.frame_host_system_time_ns;

        m_graphics_device->get_shader_monitor().update_once_per_frame();

        // Under OpenXR the headset owns display: the desktop window swapchain
        // is not rendered into, so do not acquire/present it (otherwise the
        // Vulkan Swapchain_impl::end_frame assert would fire because no
        // render pass ever ran against the acquired image).
        if (should_render && !m_app_context.OpenXR) {
            ERHE_PROFILE_SCOPE("begin_swapchain");
            const std::chrono::steady_clock::time_point begin_swapchain_begin = std::chrono::steady_clock::now();
            should_render = command_buffer.begin_swapchain(packet.frame_begin_info, frame_state);
            out_result.begin_swapchain_us = elapsed_us(begin_swapchain_begin, std::chrono::steady_clock::now());
            // The window ImGui host's swapchain render pass follows the
            // window size M2 saw, here on the thread that owns the
            // swapchain (the ImGui phase runs concurrently and reads
            // nothing of the swapchain).
            const std::shared_ptr<erhe::imgui::Window_imgui_host>& window_imgui_host = m_imgui_windows->get_window_imgui_host();
            if (window_imgui_host && should_render) {
                window_imgui_host->update_render_pass(packet.window_width, packet.window_height);
            }
        }
        m_render_phase_frame_number = packet.frame_number;

        // Indirect diffuse field producers (doc/editor/ddgi.md,
        // doc/editor/radiance_cascades.md): only the selected producer
        // (set_indirect_diffuse_source()) updates its field. DDGI ticks
        // whatever the source for pending reference irradiance queries, and
        // its tick skips the probe update unless it is selected.
        {
            // The frame's source selection (and the radiance cascades merge
            // mode) from its Render_settings copy: a producer deselected since
            // the previous frame releases its textures here, before anything
            // of this frame reads them.
            if (m_ddgi_renderer) {
                m_ddgi_renderer->apply_frame_settings();
            }
            if (m_radiance_cascades_renderer) {
                m_radiance_cascades_renderer->apply_frame_settings();
            }
            // The scene M2 chose for the frame (the packet's copy): the live
            // scene list changes in M2 of the next frame while this runs.
            const std::shared_ptr<Scene_root>& field_scene_root = packet.indirect_diffuse_scene_root;
            // Radiance cascades: refit the cascades, trace this frame's
            // budget of raw intervals, merge, and reduce merged cascade 0
            // into the probe field atlases.
            if (m_radiance_cascades_renderer && m_radiance_cascades_renderer->is_selected() && field_scene_root) {
                erhe::log::set_breadcrumb("tick: radiance_cascades");
                m_radiance_cascades_renderer->tick(command_buffer, *field_scene_root.get());
            }
            // DDGI: refit the probe volume and record this frame's probe
            // update, before the rendergraph samples the probe atlases.
            if (m_ddgi_renderer && field_scene_root) {
                erhe::log::set_breadcrumb("tick: ddgi");
                m_ddgi_renderer->tick(command_buffer, *field_scene_root.get());
            }

            // Publish (or clear) the selected producer's field for this
            // frame's forward passes - the single place that does; clearing
            // turns the USE_DDGI shader axis back off. A queued MCP
            // irradiance query (sample_indirect_diffuse) samples the same
            // field, after this frame's update wrote it.
            const Probe_field field = get_indirect_diffuse_field(m_app_context);
            if (field.is_valid()) {
                m_forward_renderer->set_ddgi(field.parameters, field.irradiance, field.distance, field.probe_data);
            } else {
                m_forward_renderer->clear_ddgi();
            }
            if (m_ddgi_renderer && field_scene_root) {
                m_ddgi_renderer->record_irradiance_query(command_buffer, *field_scene_root.get(), field);
            }
        }

        // Interactive lightmap bake (doc/editor/lightmap_baking.md section
        // 3a): record the gather slice + publish lightmap_prepare_phase()
        // planned in M2 into the frame command buffer, before the
        // rendergraph samples the published atlas. Recorded even when
        // begin_swapchain refused the frame: the bake is offscreen work and
        // the frame command buffer is still submitted, device-only.
        if (m_lightmap_baker) {
            erhe::log::set_breadcrumb("tick: lightmap record");
            m_lightmap_baker->record_tick(command_buffer);
        }

        // Execute rendergraph
        if (should_render) {
            erhe::log::set_breadcrumb("tick: rendergraph execute");
            m_rendergraph->execute(command_buffer);
        }

        // A ray trace output readback the main thread requested (MCP
        // set_ray_trace save_path): copied after every pass that writes or
        // samples the output texture this frame.
        if (m_ray_trace_renderer) {
            m_ray_trace_renderer->record_output_readback(command_buffer);
        }

        // Every debug / text renderer frame slot of this frame (recorded in
        // M2 by the viewports, in R by the headset) has been drawn, or is
        // dropped unrendered: release their ring buffer ranges.
        if (m_debug_renderer) {
            m_debug_renderer->release_frames();
        }
        if (m_text_renderer) {
            m_text_renderer->release_frames();
        }
        // Every draw-list pass of this frame (the rendergraph's shadow and
        // composition passes, the headset's) has drawn: release the shared
        // records they bound (doc/erhe/draw_list_renderer.md "Record
        // upload").
        if (m_cull_frame_registry) {
            // The frame's occlusion words (doc/plans/occlusion_culling.md),
            // written by its classification and its views' occlusion
            // passes, are read by the host once the frame retired.
            m_cull_frame_registry->record_frame_end(command_buffer);
            m_cull_frame_registry->release_frame_records();
        }

        if (should_render) {
            m_imgui_renderer->next_frame();
#if defined(ERHE_XR_LIBRARY_OPENXR)
            erhe::log::set_breadcrumb("tick: headset end_frame");
            m_headset_view->end_frame();
#endif

            m_id_renderer->next_frame();
        }
        erhe::log::set_breadcrumb("tick: submit + end_frame");

        const erhe::graphics::Frame_end_info frame_end_info{
            .requested_display_time = 0
        };
        // Under OpenXR, Xr_session::render_frame ends + submits the cb
        // before the per-view Swapchain_image destructors run
        // xrReleaseSwapchainImage (XR_KHR_vulkan_enable2 requires the
        // submit to land before release). So when OpenXR rendering ran,
        // the cb is already ended and submitted by the time we get here;
        // is_recording() detects that and lets us skip the local end +
        // submit. If render_frame did not run (headset disconnected,
        // shouldRender == false, etc.) the cb is still recording and we
        // submit here as for the non-XR case.
        if (command_buffer.is_recording()) {
            ERHE_PROFILE_SCOPE("end command buffer, swapchain, submit");

            command_buffer.end();
            if (should_render && !m_app_context.OpenXR) {
                ERHE_PROFILE_SCOPE("end_swapchain");
                command_buffer.end_swapchain(frame_end_info);
            }
            // Submit + implicit present (when a swapchain was engaged).
            // Hidden ticks arrive here with no swapchain engaged; this is
            // then a device-only submit (same shape as the init pump), so
            // work recorded during the hidden tick still executes.
            erhe::graphics::Command_buffer* cbs[] = { &command_buffer };
            m_graphics_device->submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{cbs});
        }

        // End the device frame of the packet. A failure aborts here, on the
        // thread that runs R, before the main thread goes on with the next
        // frame.
        const bool end_frame_ok = m_graphics_device->end_frame(packet.device_frame_id);
        ERHE_VERIFY(end_frame_ok);

#if defined(ERHE_GRAPHICS_API_OPENGL)
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
        m_app_rendering->process_end_capture();

        // Soak / hang-detection signal: log the first main-loop frames so a
        // batch run can positively confirm the editor reached steady-state
        // content rendering. The frame number advances only via
        // Time::prepare_update() in the overlap phase, which records it in the
        // packet; init drives the loading
        // screen through pump(), not tick(), so this counts main-loop frames
        // only. Bounded to the first frames so it never floods the log. A run
        // that hangs during the early thumbnail builds stops emitting these
        // before reaching the target, and the watchdog then names the stuck
        // phase. See doc/erhe/geogram.md.
        {
            const uint64_t main_loop_frame = packet.frame_number;
            if (main_loop_frame <= 12) {
                log_startup->info("Main loop: completed frame {}", main_loop_frame);
            }
        }

        // The LUTs, if generated, were recorded into this frame's command
        // buffer or an earlier one (Sky_renderer::ensure_luts, R only).
        out_result.sky_luts_ready    = m_sky_renderer && m_sky_renderer->are_luts_ready();
        out_result.render_us         = elapsed_us(render_begin, std::chrono::steady_clock::now());
        out_result.draw_data_wait_us = m_render_phase_draw_data_wait_us;
        set_render_frame_phase(erhe::utility::Frame_phase::none);
    }

    // Start the main-loop stall watchdog. Call once, after init completes and
    // before the redraw callback begins driving frame().
    void start_main_loop_watchdog()
    {
        m_watchdog_thread = std::thread([this]() {
            // A tick taking longer than this is treated as a stall. Normal
            // frames are sub-20 ms; the in-tick OpenXR throttle sleep is
            // 250 ms; both are far below this, so this only fires on a genuine
            // spin / deadlock.
            constexpr std::int64_t stall_threshold_ns = 5'000'000'000;  // 5 s
            constexpr std::int64_t report_interval_ns = 5'000'000'000;  // re-report cadence while stuck
            std::int64_t last_reported_ns = -1;

            for (;;) {
                {
                    std::unique_lock<std::mutex> lock{m_watchdog_mutex};
                    m_watchdog_cv.wait_for(lock, std::chrono::seconds{1}, [this]{ return m_watchdog_stop; });
                    if (m_watchdog_stop) {
                        break;
                    }
                }

                // Only a tick that has entered but not returned can be the
                // spinning one. Between frames m_in_tick is false (idle /
                // throttled) and must NOT trip the watchdog.
                if (!m_in_tick.load()) {
                    last_reported_ns = -1; // re-arm so the next real stall reports promptly
                    continue;
                }

                std::size_t                main_thread_hash   = 0;
                std::size_t                render_thread_hash = 0;
                erhe::utility::Frame_phase main_frame_phase   = erhe::utility::Frame_phase::none;
                erhe::utility::Frame_phase render_frame_phase = erhe::utility::Frame_phase::none;
                {
                    std::lock_guard<std::mutex> lock{m_watchdog_mutex};
                    main_thread_hash   = m_main_thread_hash;
                    render_thread_hash = m_render_thread_hash;
                    main_frame_phase   = m_watchdog_main_frame_phase;
                    render_frame_phase = m_watchdog_render_frame_phase;
                }

                // Find the newest breadcrumb set by each of the two frame
                // phase threads (the same thread in inline mode); the frame
                // is stuck when neither has progressed. Worker-thread
                // breadcrumbs (e.g. background geometry processing) are
                // ignored.
                const std::vector<erhe::log::Breadcrumb> recent = erhe::log::get_recent_breadcrumbs();
                const erhe::log::Breadcrumb* main_newest   = nullptr;
                const erhe::log::Breadcrumb* render_newest = nullptr;
                for (const erhe::log::Breadcrumb& b : recent) {
                    // recent is oldest -> newest, so these keep the newest match
                    if ((main_thread_hash == 0) || (b.thread_hash == main_thread_hash)) {
                        main_newest = &b;
                    }
                    if ((render_thread_hash != 0) && (b.thread_hash == render_thread_hash)) {
                        render_newest = &b;
                    }
                }
                const erhe::log::Breadcrumb* newest = main_newest;
                if ((render_newest != nullptr) && ((newest == nullptr) || (render_newest->monotonic_ns > newest->monotonic_ns))) {
                    newest = render_newest;
                }
                if (newest == nullptr) {
                    continue; // no breadcrumb from the frame threads yet
                }

                const std::int64_t now_ns   = erhe::log::breadcrumb_now_ns();
                const std::int64_t stuck_ns = now_ns - newest->monotonic_ns;
                if (stuck_ns < stall_threshold_ns) {
                    continue;
                }
                if ((last_reported_ns >= 0) && ((now_ns - last_reported_ns) < report_interval_ns)) {
                    continue; // already reported this stall recently
                }
                last_reported_ns = now_ns;

                log_watchdog->error(
                    "Main loop STALLED: frame has not progressed for {:.1f} s. Frame phase: main {}, render {}.",
                    static_cast<double>(stuck_ns) / 1e9,
                    erhe::utility::c_str(main_frame_phase),
                    erhe::utility::c_str(render_frame_phase)
                );
                const erhe::log::Breadcrumb* thread_newest[2] = { main_newest, render_newest };
                const char*                  thread_label [2] = { "main",      "render"      };
                const std::size_t            thread_hash  [2] = { main_thread_hash, render_thread_hash };
                for (std::size_t i = 0; i < 2; ++i) {
                    if (thread_newest[i] == nullptr) {
                        log_watchdog->error("  {} thread {:#x}: no breadcrumb", thread_label[i], thread_hash[i]);
                        continue;
                    }
                    log_watchdog->error(
                        "  {} thread {:#x}: stuck in phase '{}' ({:.1f} s ago)",
                        thread_label[i], thread_newest[i]->thread_hash, thread_newest[i]->text,
                        static_cast<double>(now_ns - thread_newest[i]->monotonic_ns) / 1e9
                    );
                }
                // Dump the recent breadcrumb ring (oldest -> newest) so the
                // phase sequence leading into the stall is visible in the log.
                for (const erhe::log::Breadcrumb& b : recent) {
                    log_watchdog->error(
                        "  breadcrumb t={:.3f}s thread={:#x}: {}",
                        static_cast<double>(b.monotonic_ns) / 1e9, b.thread_hash, b.text
                    );
                }
            }
        });
    }

    void stop_main_loop_watchdog()
    {
        {
            std::lock_guard<std::mutex> lock{m_watchdog_mutex};
            m_watchdog_stop = true;
        }
        m_watchdog_cv.notify_all();
        if (m_watchdog_thread.joinable()) {
            m_watchdog_thread.join();
        }
    }

    [[nodiscard]] static auto get_imgui_config_path(bool openxr) -> std::string
    {
        return openxr ? "config/editor/openxr_" : "config/editor/desktop_";
    }

    [[nodiscard]] static auto conditionally_enable_window_imgui_host(erhe::window::Context_window* context_window, bool openxr)
    {
        return openxr ? nullptr : context_window;
    }

    [[nodiscard]] auto create_window(
        const Graphics_config&        graphics_config,
        const Window_config&          window_config,
        const Editor_settings_config& editor_settings
    ) -> std::unique_ptr<erhe::window::Context_window>
    {
        m_app_context.OpenXR        = editor_settings.headset.openxr;
        m_app_context.OpenXR_mirror = editor_settings.headset.openxr_mirror;

        m_app_context.developer_mode = editor_settings.developer.enable;

        m_app_context.renderdoc = graphics_config.renderdoc_capture_support;
        if (m_app_context.renderdoc) {
            m_app_context.developer_mode = true;
        }

        m_app_context.use_sleep    = window_config.use_sleep;
        m_app_context.sleep_margin = window_config.sleep_margin;

        m_app_context.power_save    = window_config.power_save;
        m_app_context.unfocused_fps = window_config.unfocused_fps;
        m_app_context.hidden_fps    = window_config.hidden_fps;

        if (m_app_context.OpenXR) {
            m_app_context.sleep_margin = 0.0f;
        }

        // On Android the RenderDoc attach is passive - initialize_frame_capture()
        // uses dlopen(..., RTLD_NOLOAD), which resolves nothing unless RenderDoc
        // already injected its capture layer into this process. It is therefore
        // free to attempt on every run, and attempting it unconditionally is what
        // lets an on-device capture work without shipping an APK whose bundled
        // config has renderdoc_capture_support pre-enabled. That matters because
        // the flag is not merely a hint: it also suppresses the Khronos
        // validation layer (see vulkan_device_init.cpp), so leaving it off keeps
        // Vulkan validation available on Quest.
#if defined(ERHE_OS_ANDROID)
        const bool initialize_frame_capture = true;
#else
        const bool initialize_frame_capture = m_graphics_config.renderdoc_capture_support;
#endif

        erhe::window::Window_configuration configuration{
            .use_depth         = m_app_context.OpenXR_mirror,
            .use_stencil       = m_app_context.OpenXR_mirror,
            .msaa_sample_count = m_app_context.OpenXR_mirror ? 0 : 0,
            .size              = glm::ivec2{1920, 1080},
#if defined(ERHE_GRAPHICS_API_OPENGL)
            .title             = erhe::window::format_window_title("erhe editor by Timo Suoranta - OpenGL"),
#elif defined(ERHE_GRAPHICS_API_METAL4)
            .title             = erhe::window::format_window_title("erhe editor by Timo Suoranta - Metal 4"),
#elif defined(ERHE_GRAPHICS_API_METAL)
            .title             = erhe::window::format_window_title("erhe editor by Timo Suoranta - Metal"),
#elif defined(ERHE_GRAPHICS_API_VULKAN)
            .title             = erhe::window::format_window_title("erhe editor by Timo Suoranta - Vulkan"),
#else
            .title             = erhe::window::format_window_title("erhe editor by Timo Suoranta"),
#endif
            .initialize_frame_capture = initialize_frame_capture,
            .renderdoc_library_path_override = m_graphics_config.renderdoc_library_path_override_enable
                ? m_graphics_config.renderdoc_library_path_override
                : std::string{}
        };

        configuration.show                     = window_config.show;
        configuration.fullscreen               = window_config.fullscreen;
        configuration.exclusive_fullscreen     = window_config.exclusive_fullscreen;
        configuration.refreshrate              = window_config.refreshrate;
        configuration.high_pixel_density       = window_config.high_pixel_density;
        configuration.framebuffer_transparency = window_config.use_transparency;
#if defined(ERHE_GRAPHICS_API_OPENGL)
        configuration.gl_major                 = window_config.gl_major;
        configuration.gl_minor                 = window_config.gl_minor;
#endif
        configuration.size                     = window_config.size;
        configuration.swap_interval            = window_config.swap_interval;
        configuration.enable_joystick          = window_config.enable_joystick;
        configuration.color_bit_depth          = window_config.color_bit_depth;

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

    Editor(std::string startup_commands_path, std::string startup_scene_path, bool no_startup_scene, bool force_post_processing_off, bool fix_gltf_spot_lights, const std::string& benchmark_path, const Headset_culling headset_culling)
        : m_startup_commands_path{std::move(startup_commands_path)}
        , m_startup_scene_path   {std::move(startup_scene_path)}
        , m_no_startup_scene     {no_startup_scene}
        , m_graphics_config     {erhe::codegen::load_config<Graphics_config>       ("config/editor/erhe_graphics.json")}
        , m_mesh_memory_config  {erhe::codegen::load_config<Mesh_memory_config>    ("config/editor/mesh_memory.json")}
        , m_renderer_config     {load_renderer_config()}
        , m_text_renderer_config{erhe::codegen::load_config<Text_renderer_config>  ("config/editor/text_renderer.json")}
        , m_window_config       {erhe::codegen::load_config<Window_config>         ("config/editor/window.json")}
    {
        apply_graphics_config_environment_overrides(m_graphics_config);

#if defined(__APPLE__)
        // Must happen before the first Metal framework use - window creation
        // below brings up a CAMetalLayer, which is enough to load Xcode's
        // GPUToolsCapture layer, and once loaded it cannot be unloaded.
        // See Metal_config::disable_gpu_frame_capture.
        if (m_graphics_config.metal.disable_gpu_frame_capture) {
            unsetenv("METAL_CAPTURE_ENABLED");
            log_startup->info(
                "Metal: GPU frame-capture layer suppressed (metal.disable_gpu_frame_capture); "
                "Xcode GPU frame capture will NOT work for this run"
            );
        }
#endif

#if defined(ERHE_OS_ANDROID) && defined(ERHE_XR_LIBRARY_OPENXR)
        // On Android the only flavor that links OpenXR is `quest` (Meta Quest 3).
        // The flat 2D Horizon panel path is not a supported runtime target;
        // force-enable OpenXR regardless of the JSON config so the editor always
        // brings up an immersive session on the headset.
        m_editor_settings.headset.openxr = true;
#endif

        if (m_editor_settings.headset.openxr) {
            m_editor_settings.hud.enabled = true;
            // Default to camera passthrough on OpenXR: with the sky composition
            // pass disabled the scene background keeps the transparent render-pass
            // clear value, so passthrough shows through instead of a rendered sky
            // (see App_rendering::update_sky_parameters()). Still runtime-toggleable
            // via the Settings window.
            m_editor_settings.sky.enabled = false;
        }

        if (m_graphics_config.renderdoc_capture_support) {
            m_app_context.renderdoc = true;
        }

        // --no-post-processing: session-wide override; the stored
        // editor_settings.json value is left untouched so removing the flag
        // restores the user's preference.
        m_app_context.force_post_processing_off = force_post_processing_off;
        if (force_post_processing_off) {
            log_startup->info("Post processing forced OFF for this session (--no-post-processing)");
        }

        // --fix-spot-lights: import-time fixup for broken glTF spot light
        // exports; applies to every glTF asset loaded in this session.
        m_app_context.fix_gltf_spot_lights = fix_gltf_spot_lights;
        if (fix_gltf_spot_lights) {
            log_startup->info("glTF spot light fixup enabled for this session (--fix-spot-lights)");
        }

        // --benchmark <file>: a timedemo run (doc/plans/quest_performance.md
        // "Benchmark mode"). A file that does not load ends the run at once
        // (the failure is logged), so a host script waiting for the result
        // does not wait for a run that measures nothing.
        if (!benchmark_path.empty()) {
            m_benchmark_driver = Benchmark_driver::load(benchmark_path);
            if (!m_benchmark_driver) {
                m_close_requested = true;
            }
        }

        // Editor is constructed on the main thread; anything capturing an
        // owner thread id must record this one, not a worker's.
        m_app_context.main_thread_id = std::this_thread::get_id();
        // The render role is published to erhe::utility::Thread_roles only
        // after init: until then the init pump renders on the main thread
        // outside any frame phase, which the render-side asserts must not
        // see as a render phase violation. In inline mode run() publishes
        // the main thread; in dedicated mode the render thread publishes
        // itself when Render_thread is constructed at the end of this
        // constructor, which then also updates render_thread_id.
        m_app_context.render_thread_id = m_app_context.main_thread_id;
        erhe::utility::Thread_roles::set_main_thread(m_app_context.main_thread_id);

        // thread_count <= 0 means one worker per hardware thread.
        const int configured_thread_count = m_editor_settings.threading.thread_count;
        const std::size_t thread_count = (configured_thread_count > 0)
            ? static_cast<std::size_t>(configured_thread_count)
            : std::max<std::size_t>(std::thread::hardware_concurrency(), 1);

        m_executor = std::make_unique<tf::Executor>(thread_count);

        // Debug-only abort when a context-holding task parks and gets a
        // task co-run onto it - proposal B, catches what the spawn guard
        // cannot see (see task_guard.hpp).
        attach_gl_context_task_guard(*m_executor.get());

        // Scene level raytrace BVH builds run on the executor, so that they
        // never land on the frame. Injected as a spawn function routing
        // through the guarded wrapper, so the library's spawn stays covered
        // by the GL worker-context spawn guard (proposal A).
        erhe::raytrace::set_task_spawner(
            [executor = m_executor.get()](std::function<void()> work) {
                erhe::task::spawn(*executor, std::move(work));
            }
        );

        // Declared outside the try so the loading screen survives past
        // the init catch block; the post-init phase (run_startup_script,
        // prewarm_all) still drives pump() through this owner. Constructed
        // inside the try once Text_renderer is built; destroyed when
        // Editor::Editor() returns.
        std::unique_ptr<Init_status_display> init_status_display_ptr;

        try {
            m_commands          = std::make_unique<erhe::commands::Commands      >();
            m_input_bindings_store = std::make_unique<Input_bindings_store       >(*m_commands.get());
            m_app_message_bus   = std::make_unique<App_message_bus               >();
            m_app_settings.read(m_editor_settings.headset.openxr);
            // --no-headset-culling: session-only, never persisted.
            m_app_settings.headset_culling_session = (headset_culling == Headset_culling::enabled);
            if (headset_culling == Headset_culling::disabled_for_session) {
                log_startup->info("Headset culling forced OFF for this session (--no-headset-culling)");
            }
            m_input_state       = std::make_unique<Input_state                   >();
            m_time              = std::make_unique<Time                          >();
            auto& commands        = *m_commands       .get();
            auto& app_message_bus = *m_app_message_bus.get();

// One serial initialization step: a status line, a profile scope and a C++
// scope for the step's locals (doc/editor/editor.md "Initialization Order").
#define ERHE_INIT_STEP_BEGIN(label) init_status_display.set_line(1, label); init_status_display.pump(); { ERHE_PROFILE_SCOPE(label);
#define ERHE_INIT_STEP_END } init_status_display.set_line(1, ""); init_status_display.pump();

            // Window and graphics context creation - in main thread
            m_window = create_window(m_graphics_config, m_window_config, m_editor_settings);

#if defined(ERHE_XR_LIBRARY_OPENXR)
            // First-phase OpenXR initialization: build Xr_instance before the
            // graphics Device. On Vulkan the Device needs to create its
            // VkInstance / VkDevice through the OpenXR runtime hooks, which are
            // exposed by Xr_instance via make_vulkan_external_creators(). On
            // OpenGL this only builds the instance; the session is created
            // later via headset->create_session(*device) on the main thread so
            // the GL context is still current.
            if (m_app_context.OpenXR) {
                ERHE_PROFILE_SCOPE("make xr::Headset (instance)");
                m_headset = std::make_unique<erhe::xr::Headset>(*m_window.get(), m_editor_settings.headset);
                if (m_headset->get_xr_instance() == nullptr) {
                    log_headset->info("OpenXR instance not available. Disabling OpenXR.");
                    m_app_context.OpenXR = false;
                    m_headset.reset();
                }
            }
#endif

#if defined(ERHE_GRAPHICS_API_VULKAN) && defined(ERHE_XR_LIBRARY_OPENXR)
            // Fetch XR-owned Vulkan creators if an Xr_instance is available, so
            // that the Vulkan graphics device creates its instance / physical
            // device / device via the OpenXR runtime.
            erhe::graphics::Vulkan_external_creators vulkan_xr_creators;
            bool                                     have_vulkan_xr_creators = false;
            if (m_headset && (m_headset->get_xr_instance() != nullptr)) {
                vulkan_xr_creators      = m_headset->get_xr_instance()->make_vulkan_external_creators();
                have_vulkan_xr_creators = true;
            }
            const erhe::graphics::Vulkan_external_creators* vulkan_xr_creators_ptr =
                have_vulkan_xr_creators ? &vulkan_xr_creators : nullptr;
#else
            const erhe::graphics::Vulkan_external_creators* vulkan_xr_creators_ptr = nullptr;
#endif

            // Graphics context state init after window - in main thread
            {
                ERHE_PROFILE_SCOPE("erhe::graphics::Device");
                m_graphics_device = std::make_unique<erhe::graphics::Device>(
                    erhe::graphics::Surface_create_info{
                        .context_window            = m_window.get(),
                        .prefer_low_bandwidth      = false,
                        .prefer_high_dynamic_range = false
                    },
                    m_graphics_config,
                    [](erhe::graphics::Message_severity severity, const std::string& error_message, const std::string& callstack) {
                        const std::string report_text = error_message + "\n=== Callstack ===\n" + callstack;
                        if (is_ai_driver()) {
                            write_ai_error_report("logs/device_error.txt", "Device error", report_text);
                            if (severity == erhe::graphics::Message_severity::error) {
                                ERHE_FATAL("Device error (see logs/device_error.txt): %s", error_message.c_str());
                            } else {
                                log_render->warn("Device message (captured to logs/device_error.txt): {}", error_message);
                            }
                            return;
                        }
                        erhe::window::copy_to_clipboard(report_text);
                        if (severity == erhe::graphics::Message_severity::error) {
                            ERHE_FATAL("Device error (copied to clipboard): %s", error_message.c_str());
                        } else {
                            log_render->warn("Device message (copied to clipboard): %s", error_message.c_str());
                            static int counter = 0;
                            ++counter;
                        }
                    },
                    vulkan_xr_creators_ptr
                );
                log_startup->info("Reverse depth: {}", m_graphics_device->get_reverse_depth() ? "on" : "off");
            }

            // RenderDoc capture is auto-initialized by Device based on Graphics_config

            // Offer the capture UI when RenderDoc is actually attached, rather
            // than when capture support was merely configured. This has to come
            // after the graphics device: on Android the capture layer is loaded
            // by the Vulkan loader during vkCreateInstance, so the API pointer
            // only resolves once Device construction has run. It is also the
            // only thing that turns the capture path on there, since
            // renderdoc_capture_support stays off on Android (see the comment
            // where initialize_frame_capture is computed).
            if (erhe::window::get_renderdoc_api() != nullptr) {
                log_startup->info("RenderDoc is attached - enabling frame capture UI and developer mode");
                m_app_context.renderdoc      = true;
                m_app_context.developer_mode = true;
            }

            m_graphics_device->set_shader_error_callback(
                [](const std::string& error_log, const std::string& shader_source, const std::string& callstack) {
                    std::string report_text = "=== Shader Error ===\n" + error_log + "\n=== Shader Source ===\n" + shader_source + "\n=== Callstack ===\n" + callstack;
                    if (is_ai_driver()) {
                        write_ai_error_report("logs/shader_error.txt", "Shader error", report_text);
                        ERHE_FATAL("Shader compilation/linking failed (see logs/shader_error.txt)");
                    }
                    erhe::window::copy_to_clipboard(report_text);
                    ERHE_FATAL("Shader compilation/linking failed (error and source copied to clipboard)");
                }
            );
            m_graphics_device->set_trace_callback(
                [](const std::string& message) {
                    if (is_ai_driver()) {
                        log_render->info("{}", message);
                        return;
                    }
                    erhe::window::copy_to_clipboard(message);
                }
            );
            m_graphics_device->set_state_dump_callback(
                [](const std::string& state_dump) {
                    if (!is_ai_driver()) {
                        erhe::window::copy_to_clipboard(state_dump);
                    }
                    log_render->info("{}", state_dump);
                }
            );

            m_window->set_title(
                erhe::window::format_window_title(
                    "erhe editor by Timo Suoranta",
                    m_graphics_device->get_info().api_info
                )
            );

            // Init-time command buffer. The constructor body below records
            // every init-time GPU operation (texture uploads, buffer
            // transfers, dummy-texture clears, etc.) into this single cb,
            // accessed via m_app_context.current_command_buffer.
            // Init_status_display::set_line ends + submits the cb when it
            // needs to display loading progress on the desktop swapchain,
            // then opens a fresh init cb and reseats
            // m_app_context.current_command_buffer to it. The constructor
            // body's final wait_idle waits for all submitted init work to
            // complete on the GPU before the main render loop starts.
            const bool init_wait_frame_ok = m_graphics_device->wait_frame();
            ERHE_VERIFY(init_wait_frame_ok);
            m_app_context.current_command_buffer = &m_graphics_device->get_command_buffer(0);
            m_app_context.current_command_buffer->begin();

            m_app_settings.apply_limits(
                *m_graphics_device.get(),
                app_message_bus,
                m_window->get_scale_factor(),
                m_renderer_config.max_light_count
            );

#if defined(ERHE_XR_LIBRARY_OPENXR)
            // Second-phase OpenXR initialization: build the Xr_session now that
            // the graphics Device exists. Apparently it is necessary to create
            // OpenXR session in the main thread / original OpenGL context,
            // instead of using shared context / worker thread. On Vulkan the
            // session uses the Device's VkInstance / VkDevice / queue indices
            // to build the XrGraphicsBindingVulkan2KHR.
            if (m_headset) {
                ERHE_PROFILE_SCOPE("make xr::Headset (session)");
                if (!m_headset->create_session(*m_graphics_device.get())) {
                    log_headset->info("Headset session creation failed. Disabling OpenXR.");
                    m_app_context.OpenXR = false;
                    m_headset.reset();
                } else if (!m_headset->is_valid()) {
                    log_headset->info("Headset initialization failed. Disabling OpenXR.");
                    m_app_context.OpenXR = false;
                    m_headset.reset();
                }
            }
#endif

            {
                ERHE_PROFILE_SCOPE("Early parts");
                m_clipboard            = std::make_unique<Clipboard     >(commands, m_app_context, app_message_bus);
                m_prefab_library       = std::make_unique<Prefab_library>(m_app_context);
                m_texture_file_loader  = std::make_unique<Texture_file_loader>(m_app_context);
                m_animation_player     = std::make_unique<Animation_player>(m_app_context, app_message_bus);
                m_app_scenes           = std::make_unique<App_scenes    >(m_app_context);
                m_asset_manager        = std::make_unique<Asset_manager >(m_app_context, app_message_bus, *m_app_scenes.get());
                m_app_windows          = std::make_unique<App_windows   >(m_app_context, commands);
                m_viewport_scene_views = std::make_unique<Scene_views   >(m_editor_settings.viewport, commands, m_app_context, app_message_bus);
                m_selection            = std::make_unique<Selection     >(commands, m_app_context, app_message_bus);
                m_mesh_component_selection = std::make_unique<Mesh_component_selection>(app_message_bus);
                m_scene_commands       = std::make_unique<Scene_commands>(commands, m_app_context, app_message_bus);
            }
            // Drive view_count from the OpenXR session's multiview
            // capability. The session was created above (first-phase
            // OpenXR initialization).
            // When multiview is enabled, the camera UBO holds two
            // entries (one per eye); shaders compiled with
            // ERHE_MULTIVIEW pick the right one via gl_ViewIndex.
            int xr_view_count = 1;
#if defined(ERHE_XR_LIBRARY_OPENXR)
            if (m_headset && (m_headset->get_xr_session() != nullptr) && m_headset->get_xr_session()->is_multiview_enabled()) {
                xr_view_count = static_cast<int>(m_headset->get_xr_session()->get_view_count());
            }
#endif
            erhe::scene_renderer::Program_interface_config program_interface_config{
                .shader_paths = {
                    std::filesystem::path{"res"} / std::filesystem::path{"shaders"},
                    std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"},
                },
                .max_camera_count    = m_renderer_config.max_camera_count,
                .max_joint_count     = m_renderer_config.max_joint_count,
                .max_light_count     = m_renderer_config.max_light_count,
                .max_material_count  = m_renderer_config.max_material_count,
                .max_primitive_count = m_renderer_config.max_primitive_count,
                .max_draw_count      = m_renderer_config.max_draw_count,
                .light_cluster       = erhe::scene_renderer::Light_cluster_config{
                    .tile_size   = m_renderer_config.light_cluster_tile_size,
                    .slice_count = m_renderer_config.light_cluster_slice_count,
                    .capacity    = m_renderer_config.light_cluster_capacity
                },
                .view_count      = xr_view_count
            };
            {
                ERHE_PROFILE_SCOPE("Mesh_memory");
                m_mesh_memory = std::make_unique<erhe::scene_renderer::Mesh_memory>(
                    m_mesh_memory_config,
                    *m_graphics_device.get()
                );
            }
            {
                ERHE_PROFILE_SCOPE("Program_interface");
                m_program_interface = std::make_unique<erhe::scene_renderer::Program_interface>(
                    *m_graphics_device.get(),
                    *m_mesh_memory.get(),
                    program_interface_config
                );
            }
            // Before the taskflow, and published into App_context on the
            // spot: the scene roots and the previews that taskflow builds
            // each construct a Material_set from their constructor, so this
            // has to be visible to all of them regardless of scheduling.
            m_material_set_factory = std::make_unique<Material_set_factory>(
                *m_graphics_device.get(),
                *m_app_context.current_command_buffer,
                *m_program_interface.get()
            );
            m_app_context.material_set_factory = m_material_set_factory.get();

            // Cache constructed before Programs so each Programs member
            // can hold a reference to it. The cache stays empty until
            // something calls Shader_variant_cache::get(...).
            {
                ERHE_PROFILE_SCOPE("Shader_variant_cache");
                m_shader_variant_cache = std::make_unique<erhe::scene_renderer::Shader_variant_cache>(
                    *m_graphics_device.get(),
                    *m_program_interface.get()
                );
            }
            {
                ERHE_PROFILE_SCOPE("Programs");
                m_programs = std::make_unique<Programs>(
                    *m_graphics_device.get(),
                    *m_program_interface.get()
                    //*m_shader_variant_cache.get()
                );
            }

            {
                ERHE_PROFILE_SCOPE("Text_renderer");
                m_text_renderer = std::make_unique<erhe::renderer::Text_renderer>(
                    *m_graphics_device.get(),
                    *m_app_context.current_command_buffer,
                    m_text_renderer_config.enabled,
                    m_text_renderer_config.font_size,
                    xr_view_count
                );
            }

            // Stack-local: the status display is only useful during init,
            // and Editor::Editor() is the only scope that drives it.
            // Init_status_display takes a reference to
            // m_app_context.current_command_buffer so its pump() can
            // reseat the pointer after driving a swapchain frame for
            // the loading screen. When a Headset is supplied, pump()
            // drives an OpenXR frame instead of the desktop swapchain.
            // Everything runs on the main thread: set_line() /
            // set_clear_color() from the serial init steps, and pump(),
            // so SDL_PollEvent and xrBegin/EndFrame never run
            // off-main-thread.
            erhe::xr::Headset* const init_status_headset =
#if defined(ERHE_XR_LIBRARY_OPENXR)
                m_headset.get();
#else
                nullptr;
#endif
            init_status_display_ptr = std::make_unique<Init_status_display>(
                *m_graphics_device.get(),
                m_app_context.current_command_buffer,
                *m_window.get(),
                *m_text_renderer.get(),
                true,
                init_status_headset
            );
            Init_status_display& init_status_display = *init_status_display_ptr;

            init_status_display.set_line(0, "Initializing erhe editor...");
            init_status_display.pump();

            ERHE_INIT_STEP_BEGIN("Programs (load)")
            {
                init_status_display.set_line(1, "Loading shader stages");

                m_programs->load_programs(
                    *m_executor.get(),
                    *m_graphics_device.get(),
                    *m_mesh_memory.get(),
                    *m_program_interface.get(),
                    init_status_display
                );
                init_status_display.set_line(1, "");
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Imgui_renderer")
            {
                m_imgui_renderer = std::make_unique<erhe::imgui::Imgui_renderer>(*m_graphics_device.get(), *m_app_context.current_command_buffer, m_app_settings.imgui);
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Debug_renderer")
            {
                m_debug_renderer = std::make_unique<erhe::renderer::Debug_renderer>(*m_graphics_device.get(), xr_view_count);
                Debug_visualizations::apply_debug_renderer_style(*m_debug_renderer, m_editor_settings.debug_visualizations_style);
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Thumbnails")
            {
                m_thumbnails = std::make_unique<Thumbnails>(m_editor_settings.thumbnails, *m_graphics_device.get(), *m_app_context.current_command_buffer, m_app_context, *m_app_message_bus.get());
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Rendergraph")
            {
                m_rendergraph = std::make_unique<erhe::rendergraph::Rendergraph>(*m_graphics_device.get());
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Forward_renderer")
            {
                // Glyph curve data for GPU curve-based grid axis labels.
                // Codepoint order defines the glyph slot convention shared
                // with the shaders: slots 0..9 = '0'..'9', 10 = '-', 11 = '.'.
                const std::array<char32_t, 12> glyph_codepoints{
                    U'0', U'1', U'2', U'3', U'4', U'5', U'6', U'7', U'8', U'9', U'-', U'.'
                };
                const erhe::ui::Glyph_outline_set glyph_outline_set = erhe::ui::extract_glyph_outlines(
                    std::filesystem::path{"res"} / std::filesystem::path{"fonts"} / std::filesystem::path{"SourceCodePro-Semibold.otf"},
                    glyph_codepoints
                );
                // One Scene_pass_resources, referenced by both entry points:
                // the bucket path and the draw-list path run the same pass
                // prologue, and it holds no per-frame state of its own.
                m_scene_pass_resources = std::make_unique<erhe::scene_renderer::Scene_pass_resources>(
                    *m_graphics_device.get(),
                    *m_app_context.current_command_buffer,
                    *m_program_interface.get(),
                    &glyph_outline_set,
                    m_material_set_factory->get_empty_material_set()
                );
                m_forward_renderer = std::make_unique<erhe::scene_renderer::Forward_renderer>(
                    *m_graphics_device.get(),
                    *m_mesh_memory.get(),
                    *m_program_interface.get(),
                    *m_shader_variant_cache.get(),
                    *m_scene_pass_resources.get()
                );
                m_draw_list_renderer = std::make_unique<erhe::scene_renderer::Draw_list_renderer>(
                    *m_graphics_device.get(),
                    *m_program_interface.get(),
                    *m_scene_pass_resources.get()
                );
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Shadow_renderer")
            {
                m_shadow_renderer = std::make_unique<erhe::scene_renderer::Shadow_renderer>(
                    *m_graphics_device.get(),
                    *m_app_context.current_command_buffer,
                    *m_mesh_memory.get(),
                    *m_program_interface.get(),
                    *m_shader_variant_cache.get()
                );
            }
            ERHE_INIT_STEP_END

            // The draw lists' GPU culling classification (doc/plans/
            // culling.md phase 5): the Draw_list_scenes of the scene roots
            // created after this record their dispatches through it.
            ERHE_INIT_STEP_BEGIN("Gpu_cull_classifier")
            {
                m_gpu_cull_classifier = std::make_unique<erhe::scene_renderer::Gpu_cull_classifier>(
                    *m_graphics_device.get(),
                    std::filesystem::path{"res"} / std::filesystem::path{"shaders"}
                );
                // The frame's culling classification (doc/plans/culling.md
                // phase 6a): after every rendergraph node's pre-execute hook
                // added its passes' culling views, before the first node
                // executes, outside any render pass - every view of the
                // frame classified at once, on the CPU and (gpu setting) in
                // one dispatch per scene. Every Draw_list_scene registers
                // with the registry, whether or not it has a classifier.
                m_cull_frame_registry = std::make_unique<erhe::scene_renderer::Cull_frame_registry>();
                erhe::scene_renderer::Cull_frame_registry* const cull_frame_registry = m_cull_frame_registry.get();
                m_rendergraph->set_pre_execute_callback(
                    [cull_frame_registry](erhe::graphics::Command_buffer& command_buffer) {
                        cull_frame_registry->classify_frame(command_buffer);
                    }
                );
            }
            ERHE_INIT_STEP_END

            // The GPU light binning of clustered views (doc/plans/
            // clustered_lights.md C4), shared by every Shadow_render_node.
            ERHE_INIT_STEP_BEGIN("Light_cluster_gpu_binner")
            {
                m_light_cluster_gpu_binner = std::make_unique<erhe::scene_renderer::Light_cluster_gpu_binner>(
                    *m_graphics_device.get(),
                    std::filesystem::path{"res"} / std::filesystem::path{"shaders"}
                );
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Texel_renderer")
            {
                m_texel_renderer = std::make_unique<erhe::scene_renderer::Texel_renderer>(
                    *m_graphics_device.get(),
                    *m_app_context.current_command_buffer,
                    *m_program_interface.get()
                );
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Content_wide_line_renderer")
            {
                m_content_wide_line_interface = std::make_unique<erhe::scene_renderer::Content_wide_line_interface>(
                    *m_graphics_device,
                    &m_program_interface->joint_interface.joint_block,
                    xr_view_count
                );

                const std::filesystem::path shader_path = std::filesystem::path{"res"} / std::filesystem::path{"shaders"};

                using namespace erhe::graphics;

                // Compute shader (non-skinned variant)
                {
                    Shader_stages_create_info create_info{
                        .name             = "compute_before_content_line",
                        .struct_types     = {
                            m_content_wide_line_interface->edge_line_vertex_struct.get(),
                            m_content_wide_line_interface->triangle_vertex_struct.get(),
                            &m_content_wide_line_interface->view_camera_struct
                        },
                        .interface_blocks = {
                            m_content_wide_line_interface->edge_line_vertex_buffer_block.get(),
                            m_content_wide_line_interface->triangle_vertex_buffer_block.get(),
                            &m_content_wide_line_interface->view_block
                        },
                        .shaders = { { Shader_type::compute_shader, shader_path / "compute_before_content_line.comp" } },
                        .bind_group_layout = m_content_wide_line_interface->bind_group_layout.get(),
                    };
                    Shader_stages_prototype prototype = build_shader_stages(*m_graphics_device, create_info);
                    if (prototype.is_valid()) {
                        m_content_wide_line_compute_stages = std::make_unique<Shader_stages>(*m_graphics_device, std::move(prototype));
                    }
                }
                // Compute shader (skinned variant). Same source compiled with
                // ERHE_USE_SKINNING; declares the joint side buffer and the
                // global `joint` block in addition to the regular bindings.
                if (m_content_wide_line_interface->skinned_bind_group_layout != nullptr) {
                    Shader_stages_create_info create_info{
                        .name             = "compute_before_content_line_skinned",
                        .defines          = { { "ERHE_USE_SKINNING", "1" } },
                        .struct_types     = {
                            m_content_wide_line_interface->edge_line_vertex_struct.get(),
                            m_content_wide_line_interface->edge_line_joint_vertex_struct.get(),
                            m_content_wide_line_interface->triangle_vertex_struct.get(),
                            &m_content_wide_line_interface->view_camera_struct,
                            &m_program_interface->joint_interface.joint_struct
                        },
                        .interface_blocks = {
                            m_content_wide_line_interface->edge_line_vertex_buffer_block.get(),
                            m_content_wide_line_interface->edge_line_joint_vertex_buffer_block.get(),
                            m_content_wide_line_interface->triangle_vertex_buffer_block.get(),
                            &m_content_wide_line_interface->view_block,
                            &m_program_interface->joint_interface.joint_block
                        },
                        .shaders           = { { Shader_type::compute_shader, shader_path / "compute_before_content_line.comp" } },
                        .bind_group_layout = m_content_wide_line_interface->skinned_bind_group_layout.get(),
                    };
                    Shader_stages_prototype prototype = build_shader_stages(*m_graphics_device, create_info);
                    if (prototype.is_valid()) {
                        m_content_wide_line_compute_stages_skinned = std::make_unique<Shader_stages>(*m_graphics_device, std::move(prototype));
                    }
                }
                // Graphics shader (renders compute output, single-view).
                //
                // Reads pre-transformed triangle vertices from the
                // triangle SSBO (binding 1) and the per-eye viewport
                // from the view UBO (binding 3) -- same bindings as
                // the multiview variant below; the only difference is
                // ERHE_MULTIVIEW + the multiview render pass's view
                // mask. The wide-line graphics bind group layout
                // intentionally does NOT inherit forward_renderer's
                // camera UBO; switching descriptor-set layouts inside
                // the render pass invalidates any descriptors bound
                // by upstream renderers, so the renderer binds
                // everything it needs itself.
                {
                    Shader_stages_create_info create_info{
                        .name             = "content_line_after_compute",
                        .struct_types     = {
                            m_content_wide_line_interface->triangle_vertex_struct.get(),
                            &m_content_wide_line_interface->view_camera_struct
                        },
                        .interface_blocks = {
                            m_content_wide_line_interface->triangle_vertex_buffer_read_block.get(),
                            &m_content_wide_line_interface->view_block
                        },
                        .fragment_outputs = &m_content_wide_line_interface->fragment_outputs,
                        // No vertex_format: the vertex shader reads
                        // the triangle SSBO instead of input-assembler
                        // attributes.
                        .no_vertex_input  = true,
                        .shaders = {
                            { Shader_type::vertex_shader,   shader_path / "line_after_compute.vert"        },
                            { Shader_type::fragment_shader, shader_path / "content_line_after_compute.frag" }
                        },
                        .bind_group_layout = m_content_wide_line_interface->graphics_bind_group_layout.get(),
                    };
                    Shader_stages_prototype prototype = build_shader_stages(*m_graphics_device, create_info);
                    if (prototype.is_valid()) {
                        m_content_wide_line_graphics_stages = std::make_unique<Shader_stages>(*m_graphics_device, std::move(prototype));
                    }
                }
                // Multiview-compiled graphics shader pair. Compiled
                // only when the headset can drive multiview
                // (view_count >= 2); not used by single-view
                // viewports. Reads the triangle SSBO directly from
                // the vertex stage at gl_VertexID + gl_ViewIndex *
                // stride_per_view (see line_after_compute.vert
                // ERHE_MULTIVIEW branch) so a single draw inside
                // the headset's multiview render pass produces
                // correct stereo output for content edge lines.
                if (xr_view_count >= 2) {
                    Shader_stages_create_info create_info{
                        .name             = "content_line_after_compute_multiview",
                        .struct_types     = {
                            m_content_wide_line_interface->triangle_vertex_struct.get(),
                            &m_content_wide_line_interface->view_camera_struct
                        },
                        .interface_blocks = {
                            m_content_wide_line_interface->triangle_vertex_buffer_read_block.get(),
                            &m_content_wide_line_interface->view_block
                        },
                        .fragment_outputs = &m_content_wide_line_interface->fragment_outputs,
                        // No vertex_format: the multiview vertex
                        // shader reads the triangle SSBO instead of
                        // input-assembler attributes.
                        .no_vertex_input  = true,
                        .shaders = {
                            { Shader_type::vertex_shader,   shader_path / "line_after_compute.vert"        },
                            { Shader_type::fragment_shader, shader_path / "content_line_after_compute.frag" }
                        },
                        .bind_group_layout = m_content_wide_line_interface->graphics_bind_group_layout.get(),
                        .view_count        = static_cast<uint32_t>(xr_view_count)
                    };
                    Shader_stages_prototype prototype = build_shader_stages(*m_graphics_device, create_info);
                    if (prototype.is_valid()) {
                        m_content_wide_line_multiview_graphics_stages = std::make_unique<Shader_stages>(*m_graphics_device, std::move(prototype));
                    }
                }

                m_content_wide_line_renderer = erhe::scene_renderer::make_content_wide_line_compute_renderer(
                    *m_graphics_device,
                    *m_content_wide_line_interface,
                    m_content_wide_line_compute_stages.get(),
                    m_content_wide_line_compute_stages_skinned.get(), // may be null if joint block missing
                    m_content_wide_line_graphics_stages.get(),
                    m_content_wide_line_multiview_graphics_stages.get()
                );
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Imgui_windows")
            {
                m_imgui_windows = std::make_unique<erhe::imgui::Imgui_windows>(
                    *m_imgui_renderer.get(),
                    *m_graphics_device.get(),
                    *m_rendergraph.get(),
                    conditionally_enable_window_imgui_host(m_window.get(), m_app_context.OpenXR),
                    get_imgui_config_path(m_app_context.OpenXR)
                );
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Icon_set")
            {
                m_icon_set = std::make_unique<Icon_set>(m_app_context, *m_imgui_renderer.get());
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Post_processing")
            {
                m_post_processing = std::make_unique<Post_processing>(*m_graphics_device.get(), *m_app_context.current_command_buffer, m_app_context);
                m_sky_renderer = std::make_unique<Sky_renderer>(
                    *m_graphics_device.get(),
                    *m_app_context.current_command_buffer,
                    m_app_context,
                    *m_program_interface.get(),
                    xr_view_count
                );
                m_ray_trace_renderer = std::make_unique<Ray_trace_renderer>(
                    *m_graphics_device.get(),
                    *m_app_context.current_command_buffer,
                    m_app_context,
                    *m_program_interface.get(),
                    *m_mesh_memory.get()
                );
                m_ddgi_renderer = std::make_unique<Ddgi_renderer>(
                    *m_graphics_device.get(),
                    *m_app_context.current_command_buffer,
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_program_interface.get(),
                    *m_mesh_memory.get(),
                    get_producer_selection(m_editor_settings.indirect_diffuse_source, Indirect_diffuse_source::ddgi)
                );
                m_radiance_cascades_renderer = std::make_unique<Radiance_cascades_renderer>(
                    *m_graphics_device.get(),
                    *m_app_context.current_command_buffer,
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_program_interface.get(),
                    *m_mesh_memory.get(),
                    m_editor_settings.radiance_cascades.merge_mode,
                    get_producer_selection(m_editor_settings.indirect_diffuse_source, Indirect_diffuse_source::radiance_cascades)
                );
                m_lightmap_baker  = std::make_unique<Lightmap_baker>(*m_graphics_device.get(), *m_mesh_memory.get());
                m_lightmap_baker->set_join_render_phase([this]() { join_render_phase(); });
                m_lightmap_report = std::make_unique<Lightmap_report>();
                m_lightmap_baker->set_report(m_lightmap_report.get());
                m_lightmap_streamer    = std::make_unique<Lightmap_streamer>(*m_graphics_device.get(), m_app_context);
                m_lightmap_partitioner = std::make_unique<Lightmap_partitioner>(m_app_context);
                m_lightmap_baker->set_partitioner(m_lightmap_partitioner.get());
                // Interactive bake results must never be lost to a residency
                // swap: evicting tiles park in a pending-save queue that
                // Lightmap_window::update() persists to <scene>.lightmap/.
                m_lightmap_baker->set_save_on_evict(true);
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Id_renderer")
            {
                m_id_renderer = std::make_unique<Id_renderer>(
                    m_editor_settings.id_renderer,
                    *m_graphics_device.get(),
                    *m_program_interface.get(),
                    *m_mesh_memory.get(),
                    *m_shader_variant_cache.get(),
                    *m_programs.get()
                );
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("App_rendering")
            {
                m_app_rendering = std::make_unique<App_rendering>(
                    *m_commands.get(),
                    *m_graphics_device.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_mesh_memory.get(),
                    *m_programs.get()
                );
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Some windows")
            {
                m_operation_stack        = std::make_unique<Operation_stack                 >(*m_commands.get(),       *m_imgui_renderer.get(), *m_imgui_windows.get(), m_app_context);
                m_asset_browser          = std::make_unique<Asset_browser                   >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context, *m_app_message_bus.get(), *m_executor.get());
                m_composer_window        = std::make_unique<Composer_window                 >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_selection_window       = std::make_unique<Selection_window                >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_settings_window        = std::make_unique<Settings_window                 >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context, *m_app_message_bus.get());
                m_clipboard_window       = std::make_unique<Clipboard_window                >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_commands_window        = std::make_unique<Commands_window                 >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_input_bindings_window  = std::make_unique<Input_bindings_window           >(*m_commands.get(),       *m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_geometry_graph_window  = std::make_unique<Geometry_graph_window           >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_geometry_graph_palette_window = std::make_unique<Graph_editor_palette_window>(*m_imgui_renderer.get(), *m_imgui_windows.get(), *m_geometry_graph_window.get(), "Geometry Graph Palette", "geometry_graph_palette");
                m_texture_graph_window   = std::make_unique<Texture_graph_window            >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_texture_graph_palette_window  = std::make_unique<Graph_editor_palette_window>(*m_imgui_renderer.get(), *m_imgui_windows.get(), *m_texture_graph_window.get(), "Texture Graph Palette", "texture_graph_palette");
                m_graph_window           = std::make_unique<Graph_window                    >(*m_commands.get(),       *m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context, *m_app_message_bus.get());
                m_node_properties_window = std::make_unique<Node_properties_window          >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_gradient_editor        = std::make_unique<Gradient_editor                 >(*m_imgui_renderer.get(), *m_imgui_windows.get());
                m_icon_browser           = std::make_unique<Icon_browser                    >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_sheet_window           = std::make_unique<Sheet_window                    >(*m_commands.get(),       *m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context, *m_app_message_bus.get());
                m_animation_window       = std::make_unique<Animation_window                >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context, *m_app_message_bus.get());
                m_layers_window          = std::make_unique<Layers_window                   >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_network_window         = std::make_unique<Network_window                  >(m_editor_settings.network, *m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_operations             = std::make_unique<Operations                      >(m_editor_settings.scene, *m_commands.get(),       *m_imgui_renderer.get(), *m_imgui_windows.get(), m_app_context, *m_app_message_bus.get());
                m_physics_window         = std::make_unique<Physics_window                  >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_post_processing_window = std::make_unique<Post_processing_window          >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_ray_trace_window       = std::make_unique<Ray_trace_window                >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_ddgi_window            = std::make_unique<Ddgi_window                     >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_radiance_cascades_window = std::make_unique<Radiance_cascades_window      >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_properties             = std::make_unique<Properties                      >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context, *m_app_message_bus.get());
                m_geometry_spreadsheet_window = std::make_unique<Geometry_spreadsheet_window>(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context, *m_app_message_bus.get());
                m_editor_windows         = std::make_unique<Editor_windows                  >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_frame_pacing_window    = std::make_unique<Frame_pacing_window             >(*m_imgui_renderer.get(), *m_imgui_windows.get());
                m_controller_inputs_window = std::make_unique<Controller_inputs_window     >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_lightmap_window        = std::make_unique<Lightmap_window                 >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_lightmap_texture_window = std::make_unique<Lightmap_texture_window        >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context, *m_app_message_bus.get());
                m_app_context.lightmap_texture_window = m_lightmap_texture_window.get();
                m_app_context.lightmap_window         = m_lightmap_window.get();
                m_app_context.frame_pacing_window   = m_frame_pacing_window.get();
                m_app_context.frame_pacing_observer = &m_frame_pacing_observer;
                // The pacer's presentation setup follows the window display's
                // presentation (fixed grid or variable refresh, claim C30):
                // applied now and on every change the device reports (display,
                // mode or fullscreen change; main thread, SDL event poll).
                {
                    erhe::graphics::Display_refresh_resolver& resolver = m_graphics_device->get_display_refresh_resolver();
                    apply_display_presentation(resolver.get_presentation());
                    resolver.set_presentation_callback(
                        [this](const erhe::graphics::Display_presentation& presentation) {
                            apply_display_presentation(presentation);
                        }
                    );
                    // A deadline estimate held at the calibrated ceiling means
                    // the present calibration is stale-low (the compositor got
                    // slower): its cache entry is deleted so the next start
                    // measures; this session keeps the estimate
                    // (doc/erhe/graphics.md "Present calibration cache").
                    m_frame_pacing_observer.set_deadline_ceiling_callback(
                        [this](const double deadline_offset) {
                            const std::string reason = fmt::format(
                                "deadline estimate held at its calibrated ceiling {:.3f} ms (stale-low)",
                                deadline_offset * 1000.0
                            );
                            static_cast<void>(m_graphics_device->remove_present_calibration_cache_entry(reason));
                        }
                    );
                }
                m_rendergraph_window     = std::make_unique<Rendergraph_window              >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_tool_properties_window = std::make_unique<Tool_properties_window          >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  m_app_context);
                m_logs                   = std::make_unique<erhe::imgui::Logs               >(*m_commands.get(),       *m_imgui_renderer.get(), "config/editor/logging.json");
                m_log_settings_window    = std::make_unique<erhe::imgui::Log_settings_window>(*m_imgui_renderer.get(), *m_imgui_windows.get(),  *m_logs.get());
                m_tail_log_window        = std::make_unique<erhe::imgui::Tail_log_window    >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  *m_logs.get());
                m_frame_log_window       = std::make_unique<erhe::imgui::Frame_log_window   >(*m_imgui_renderer.get(), *m_imgui_windows.get(),  *m_logs.get());
                m_performance_window     = std::make_unique<erhe::imgui::Performance_window >(*m_imgui_renderer.get(), *m_imgui_windows.get());
                m_app_context.performance_window = m_performance_window.get();
                m_transform_update_stats_tracker = std::make_unique<Transform_update_stats_tracker>(*m_performance_window.get());
                m_app_context.transform_update_stats_tracker = m_transform_update_stats_tracker.get();
                m_pipelines              = std::make_unique<erhe::imgui::Pipelines          >(*m_imgui_renderer.get(), *m_imgui_windows.get());
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Tools")
            {
                m_tools = std::make_unique<Tools>(m_app_context, m_app_settings);
                m_fly_camera_tool = std::make_unique<Fly_camera_tool>(
                    m_editor_settings.camera_controls,
                    *m_commands.get(),
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_tools.get()
                );
                m_navigation_gizmo_tool = std::make_unique<Navigation_gizmo_tool>(
                    *m_commands.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_tools.get()
                );
            }
            ERHE_INIT_STEP_END
        
            ERHE_INIT_STEP_BEGIN("Default content library")
            {
                // Only the template content library is created at init, because
                // Scene_builder builds brushes into it. It is a palette source
                // only: no scene ever owns it, and every scene (default or new)
                // seeds its OWN library with brush copies from it plus fresh
                // default materials (create_default_scene /
                // Scene_commands::create_new_scene). The Scene_root itself is
                // created later by the scene.create startup command (or a loaded
                // scene becomes the only scene) -- so a commands.json that just
                // loads a scene yields exactly one scene, with no empty default.
                m_default_content_library = std::make_shared<Content_library>();
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Scene_builder")
            {
                m_scene_builder = std::make_unique<Scene_builder>(
                    m_editor_settings.scene,                  //const Scene_config&                scene_config
                    m_editor_settings.post_processing &&
                        !m_app_context.force_post_processing_off, //bool                          enable_post_processing
                    m_default_content_library,                //std::shared_ptr<Content_library>   content_library
                    m_app_context,                            //App_context&                       app_context
                    m_app_settings,                           //App_settings&                      app_settings
                    *m_mesh_memory.get(),                     //erhe::scene_renderer::Mesh_memory& mesh_memory
                    *m_executor.get()                         //tf::Executor&                      executor
                );
                // The scene_root is assigned to Scene_builder later, by
                // create_default_scene() (the scene.create startup command).

                // Bone pick/display proxies. Constructed here because this is
                // where Mesh_memory is available; it builds its shared bone
                // primitive lazily on the first skin registration.
                m_bone_visualization = std::make_unique<Bone_visualization>(m_app_context, *m_app_message_bus.get(), *m_mesh_memory.get());
                m_app_context.bone_visualization = m_bone_visualization.get();

                // Active-joint tracking for the Joint Weight Ramp debug mode.
                // Purely message-driven; grouped here with the other
                // skin/bone-related editor parts.
                m_weight_display = std::make_unique<Weight_display>(m_app_context, *m_app_message_bus.get());
                m_app_context.weight_display = m_weight_display.get();
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Headset (init)")
            {
                m_headset_view = std::make_unique<Headset_view>(
#if defined(ERHE_XR_LIBRARY_OPENXR)
                    m_editor_settings.viewport,
#endif
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
                    m_app_settings
                );
#if defined(ERHE_XR_LIBRARY_OPENXR)
                if (m_app_context.OpenXR) {
                    m_hand_tracker = std::make_unique<Hand_tracker>(m_app_context, *m_app_rendering.get());
                }
#endif
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Headset (attach)")
            {
                // The Headset_view attaches to the scene when one first exists, via
                // the Scene_created_message handler (on_scene_created) rather than
                // here -- no scene_root is created at init any more.
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Transform tools")
            {
                m_move_tool   = std::make_unique<Move_tool  >(m_app_context, *m_icon_set.get(), *m_tools.get());
                m_rotate_tool = std::make_unique<Rotate_tool>(m_app_context, *m_icon_set.get(), *m_tools.get());
                m_scale_tool  = std::make_unique<Scale_tool >(m_app_context, *m_icon_set.get(), *m_tools.get());
                m_transform_tool = std::make_unique<Transform_tool>(
                    m_editor_settings.transform_tool,
                    *m_executor.get(),
                    *m_commands.get(),
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_headset_view.get(),
                    *m_mesh_memory.get(),
                    *m_tools.get(),
                    *m_move_tool.get(),
                    *m_rotate_tool.get(),
                    *m_scale_tool.get()
                );
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Group 1")
            {
                m_hud = std::make_unique<Hud>(
                    m_editor_settings.hud,
                    *m_commands.get(),
                    *m_graphics_device.get(),
                    *m_imgui_renderer.get(),
                    *m_rendergraph.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_app_windows.get(),
                    *m_headset_view.get(),
                    *m_mesh_memory.get(),
                    *m_tools.get()
                );
                m_hotbar = std::make_unique<Hotbar>(
                    m_editor_settings.hotbar,
                    *m_commands.get(),
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_headset_view.get(),
                    *m_mesh_memory.get(),
                    *m_tools.get()
                );
                // Hud/Hotbar attach to the scene when one first exists (driven by the
                // Scene_created_message in on_scene_created), not here -- no scene_root
                // is created at init any more.
                m_inventory_window = std::make_unique<Inventory_window>(
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_app_context,
                    m_user_state.inventory
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
                    *m_app_message_bus.get(),
                    *m_programs.get()
                );
                m_debug_view_window = std::make_unique<Depth_visualization_window>(
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    *m_rendergraph.get(),
                    *m_forward_renderer.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_mesh_memory.get(),
                    *m_programs.get()
                );
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Material_preview")
            {
                {
                    m_material_preview = std::make_unique<Material_preview>(
                        *m_graphics_device.get(),
                        *m_app_context.current_command_buffer,
                        m_app_context,
                        *m_app_message_bus.get(),
                        *m_mesh_memory.get()
                    );
                    m_brush_preview = std::make_unique<Brush_preview>(
                        *m_graphics_device.get(),
                        *m_app_context.current_command_buffer,
                        m_app_context,
                        *m_app_message_bus.get()
                    );
                }
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Brush_tool")
            {
                m_brush_tool = std::make_unique<Brush_tool>(
                    m_editor_settings.scene,
                    *m_commands.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_headset_view.get(),
                    *m_icon_set.get(),
                    *m_tools.get()
                );
            }
            ERHE_INIT_STEP_END

            ERHE_INIT_STEP_BEGIN("Group 2")
            {
                m_create = std::make_unique<Create>(
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_app_context,
                    *m_tools.get()
                );
                m_grid_tool = std::make_unique<Grid_tool>(
                    m_editor_settings.grid,
                    *m_imgui_renderer.get(),
                    *m_imgui_windows.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    m_app_settings.settings_store(),
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
                m_weight_paint_tool = std::make_unique<Weight_paint_tool>(
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
                m_mesh_component_selection_tool = std::make_unique<Mesh_component_selection_tool>(
                    *m_commands.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_mesh_component_selection.get(),
                    *m_tools.get()
                );
                m_lattice_tool = std::make_unique<Lattice_tool>(
                    *m_commands.get(),
                    m_app_context,
                    *m_app_message_bus.get(),
                    *m_tools.get()
                );
                m_draw_mode_renderer = std::make_unique<Draw_mode_renderer>(
                    m_app_context,
                    *m_tools.get()
                );
            }
            ERHE_INIT_STEP_END

        } catch (std::runtime_error& e) {
            log_startup->error("exception: {}", e.what());
            ERHE_FATAL("editor initialization failed");
        }

#if defined(ERHE_GRAPHICS_API_OPENGL)
        m_window->make_current();
#endif
        m_graphics_device->on_thread_enter();

        ERHE_PROFILE_FUNCTION();
#if defined(ERHE_GRAPHICS_API_OPENGL)
        ERHE_PROFILE_GPU_CONTEXT
#endif

#if defined(ERHE_XR_LIBRARY_OPENXR)
        if (m_app_context.OpenXR) {
            m_selection->setup_xr_bindings(*m_commands.get(), *m_headset_view.get());
        }
#endif

        fill_app_context();

        // Lightmap settings from editor_settings.json; later edits apply
        // themselves at their edit site.
        apply_lightmap_settings(m_app_context);

        // Every part has registered its commands and declared its default
        // bindings; apply the user's overrides on top.
        m_input_bindings_store->load();

        // Register Scene_builder's template palette brushes with the asset
        // manager as builtin-scope assets ({builtin, brush, <name>}). The
        // palette names are a persistence contract from here on (see the
        // note in Scene_builder::make_brushes). The brushes were built
        // synchronously in the Scene_builder constructor, so the palette is
        // complete at this point.
        {
            const std::shared_ptr<Content_library>& palette_library = m_scene_builder->get_content_library();
            if (palette_library) {
                for (const std::shared_ptr<Brush>& brush : palette_library->get_all<Brush>()) {
                    m_asset_manager->register_builtin(Asset_type::brush, brush);
                }
            }
        }

        // Start MCP server (exposes editor commands over HTTP)
        m_mcp_server = std::make_unique<Mcp_server>(*m_commands.get(), m_app_context);
        m_mcp_server->start();

        // Enforce the physics invariant on the single (config-owned) copy:
        // dynamic simulation requires static collision world.
        if (!m_editor_settings.physics.static_enable) {
            m_editor_settings.physics.dynamic_enable = false;
        }

        m_hotbar->get_all_tools();
        m_inventory_window->collect_tools();

        // Notify ImGui renderer about current font settings
        m_imgui_renderer->on_font_config_changed(m_app_settings.imgui);

#if defined(ERHE_GRAPHICS_API_OPENGL)
        if (m_graphics_device->get_info().use_clip_control) {
            gl::clip_control(gl::Clip_control_origin::lower_left, gl::Clip_control_depth::zero_to_one);
        }
        if (m_window->get_window_configuration().color_bit_depth <= 8) {
            gl::enable(gl::Enable_cap::framebuffer_srgb);
        }
#endif

        const std::shared_ptr<erhe::imgui::Window_imgui_host>& window_imgui_host = m_imgui_windows->get_window_imgui_host();
        if (!m_app_context.OpenXR) {
            window_imgui_host->set_begin_callback(
                [this](erhe::imgui::Imgui_host& imgui_host) {
                    m_app_windows->viewport_menu(imgui_host);
                }
            );
            install_default_layout(*window_imgui_host, m_app_context);
            window_imgui_host->set_status_bar_callback(
                [this](erhe::imgui::Window_imgui_host&) {
                    {
                        // std::stringstream ss;
                        // ss << fmt::format("{:.2f} ms", m_time->get_frame_time_average_ms());
                        // const int running_async_ops = m_app_context.running_async_ops.load();
                        // if (running_async_ops > 0) {
                        //     ss << " " << running_async_ops << " running operations";
                        // }
                        // const int pending_async_ops = m_app_context.pending_async_ops.load();
                        // if (pending_async_ops > 0) {
                        //     if (running_async_ops > 0) {
                        //         ss << ", ";
                        //     }
                        //     ss << pending_async_ops << " queued operations";
                        // }
                        // ImGui::TextUnformatted(ss.str().c_str());
                        const float frame_ms = m_time->get_frame_time_average_ms();
                        const int running_async_ops = m_app_context.running_async_ops.load();
                        const int pending_async_ops = m_app_context.pending_async_ops.load();
                        if (running_async_ops > 0 && pending_async_ops > 0) {
                            ImGui::Text(
                                "%.2f ms %d running operations, %d queued operations",
                                frame_ms, running_async_ops, pending_async_ops
                            );
                        }
                        else if (running_async_ops > 0) {
                            ImGui::Text(
                                "%.2f ms %d running operations",
                                frame_ms, running_async_ops
                            );
                        }
                        else if (pending_async_ops > 0) {
                            ImGui::Text(
                                "%.2f ms %d queued operations",
                                frame_ms, pending_async_ops
                            );
                        } else {
                            ImGui::Text("%.2f ms", frame_ms);
                        }
                        ImGui::SameLine();

                        // Geogram operation progress (Remesh / atlas / ...).
                        // m_geogram_progress is reused so this allocates no heap
                        // in steady-state frames; returns false when idle.
                        if (erhe::geometry::get_geogram_progress(m_geogram_progress)) {
                            ImGui::Text("%s %u%%", m_geogram_progress.task_name.c_str(), m_geogram_progress.percent);
                            ImGui::SameLine();
                            ImGui::ProgressBar(
                                static_cast<float>(m_geogram_progress.percent) / 100.0f,
                                ImVec2{160.0f, 0.0f}
                            );
                            ImGui::SameLine();
                        }
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

        // Attach the global tools to the first scene that is created (scene.create)
        // or loaded. Subscribed here, after Hud/Hotbar/Headset_view are constructed
        // and before run_startup_script() can publish Scene_created_message.
        m_scene_created_subscription = m_app_message_bus->scene_created.subscribe(
            [this](Scene_created_message& message) {
                on_scene_created(message);
            }
        );
        // Scene "Close" (Hierarchy window context menu). Queue-only: the
        // teardown destroys ImGui windows, so it runs from the message bus
        // pump in tick(), outside ImGui iteration.
        m_close_scene_subscription = m_app_message_bus->close_scene.subscribe(
            [this](Close_scene_message& message) {
                on_close_scene(message);
            }
        );
        // Explicit re-home of the global tools (MCP set_headset_scene, the
        // headset's scene combo). Queue-only, like close_scene.
        m_home_tools_subscription = m_app_message_bus->home_tools.subscribe(
            [this](Home_tools_message& message) {
                if (message.scene_root) {
                    home_tools(message.scene_root);
                }
            }
        );
        m_items_removed_subscription = m_app_message_bus->items_removed.subscribe(
            [this](Items_removed_message& message) {
                on_items_removed(message);
            }
        );

        // Run the startup command script while the init-time command
        // buffer is still open. Several scripted commands (e.g.
        // scene.add_cameras building the default Viewport_scene_view +
        // Shadow_render_node, scene.add_* invoking Brush::make_instance)
        // record GPU work during their try_call(), so they need a valid
        // m_app_context.current_command_buffer. Item_insert_remove_operation
        // execute happens later when Operation_stack::update() drains the
        // queue on the first tick, which is fine -- by then the per-frame
        // command buffer is open.
        log_startup->info("Init: run_startup_script");
        run_startup_script();
        log_startup->info("Init: run_startup_script done");

        // Drain the operation stack before prewarm. Startup script
        // commands (scene.add_lights, scene.add_room, scene.add_cameras,
        // scene.add_platonic_solids) queue Item_insert_remove_operation
        // and Scene_builder_viewport_resources_operation; until they
        // execute, the scene_root's light_layer / content layer / viewport
        // / Shadow_render_node are all empty. compute_light_layer_partition
        // would then see 0 lights, bucket_primitives would see 0 meshes,
        // and get_all_shadow_nodes() would be empty -- prewarm would
        // compile variants for a stripped-down scene and the first frame
        // would compile-on-miss for every actual variant the runtime
        // needs. Running update() here is normally a per-tick call; at
        // init it is safe because current_command_buffer is the same
        // init cb that the operations record GPU work into.
        log_startup->info("Init: draining operation stack pre-prewarm");
        m_operation_stack->update();

        // Restore the viewport windows that were open in the previous run
        // (recorded in the windows visibility config), regardless of scene
        // state: viewport windows exist independent of scenes and show
        // nothing until a scene binds into them. Runs after the startup
        // script drain, so a viewport created by scene.create keeps its slot
        // and only the remaining recorded slots are restored; a pending
        // --scene load binds into a restored empty viewport later
        // (try_repurpose_empty_viewport_window) instead of creating one.
        m_viewport_scene_views->restore_persistent_viewport_windows();

        // Drive the standard shader-variant cache from the same buckets
        // the runtime forward path would build for the loaded scene +
        // content library. Pulls glslang -> SPIR-V -> vkCreateShaderModule
        // out of the first-frame budget; the trailing wait_idle below
        // already covers any GPU work the prewarm enqueues.
        log_startup->info("Init: prewarm shader variants");
        if (init_status_display_ptr) {
            init_status_display_ptr->set_line(1, "Prewarming shader variants");
            init_status_display_ptr->pump();
        }
        prewarm_all(
            m_app_context,
            [&init_status_display_ptr](std::string_view scene_name) {
                if (!init_status_display_ptr) {
                    return;
                }
                init_status_display_ptr->set_line(2, scene_name);
                init_status_display_ptr->pump();
            }
        );
        if (init_status_display_ptr) {
            init_status_display_ptr->set_line(1, "");
            init_status_display_ptr->set_line(2, "");
            init_status_display_ptr->pump();
        }

        // Close the init-time command buffer opened in the member init
        // list (or reseated by Init_status_display::pump),
        // submit, and block until the GPU has consumed every recorded
        // upload + clear so the resources are ready for the main render
        // loop.
        ERHE_VERIFY(m_app_context.current_command_buffer != nullptr);
        log_startup->info("Init: closing + submitting init command buffer");
        m_app_context.current_command_buffer->end();
        erhe::graphics::Command_buffer* init_cbs[] = { m_app_context.current_command_buffer };
        m_graphics_device->submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{init_cbs});
        log_startup->info("Init: waiting for GPU to finish init work");
        m_graphics_device->wait_idle();
        log_startup->info("Init: GPU idle reached");
        m_app_context.current_command_buffer = nullptr;

        // Advance the frame index so subsequent frames are paced on the
        // device timeline.
        const bool init_end_frame_ok = m_graphics_device->end_frame();
        ERHE_VERIFY(init_end_frame_ok);
        log_startup->info("Init: editor ready, entering main loop");

        // Watch for a main-loop tick that spins / deadlocks and never returns;
        // the watchdog logs the last breadcrumb so the stuck phase is known.
        start_main_loop_watchdog();

        m_last_window_width  = m_window->get_width();
        m_last_window_height = m_window->get_height();

        m_window->register_redraw_callback(
            [this](){
                if (!m_run_started || m_in_tick.load()) {
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
                if ((m_last_window_width != 0) && (m_last_window_height != 0)) {
                    frame();
                }
            }
        );

        // The render phase handoff (doc/editor/editor.md "Frame phases and
        // the render thread"). In dedicated mode the constructor returns
        // once the render thread has published itself as the render role;
        // in inline mode run() publishes the main thread.
        m_render_thread_mode = select_render_thread_mode(m_editor_settings.threading.render_thread);
        m_render_thread = std::make_unique<Render_thread>(
            m_render_thread_mode,
            [this](const Frame_packet& packet, Frame_result& out_result) {
                frame_render_phase(packet, out_result);
            }
        );
        if (m_render_thread_mode == Render_thread_mode::dedicated_thread) {
            m_app_context.render_thread_id = erhe::utility::Thread_roles::get_render_thread();
        }
        m_imgui_overlap    = select_imgui_overlap(m_render_thread_mode, m_editor_settings.threading.imgui_overlap);
        m_frame_pipelining = select_frame_pipelining(m_render_thread_mode, m_app_context.OpenXR, m_editor_settings.threading.frame_pipelining);
        m_app_context.join_render_phase = [this]() {
            join_render_phase();
        };
        // The ImGui host rendergraph nodes wait for the frame's draw data
        // (published by frame_imgui_phase's caller); the wait is R time,
        // reported through Frame_result::draw_data_wait_us.
        m_imgui_renderer->set_draw_data_wait(
            [this]() {
                const std::chrono::steady_clock::time_point wait_begin = std::chrono::steady_clock::now();
                m_render_thread->wait_for_draw_data(m_render_phase_frame_number);
                m_render_phase_draw_data_wait_us += elapsed_us(wait_begin, std::chrono::steady_clock::now());
            }
        );
#if defined(ERHE_XR_LIBRARY_OPENXR)
        // The XR view command buffers are recorded in the render phase, so
        // they come from the render slot, not from slot 0 (which M2's
        // glTF image uploads use). The init pump above recorded them from
        // slot 0 on the main thread.
        if (m_headset && (m_headset->get_xr_session() != nullptr)) {
            m_headset->get_xr_session()->set_command_buffer_thread_slot(s_render_thread_slot);
        }
#endif
    }

    ~Editor() noexcept
    {
        // Stop the watchdog first so it cannot touch members during teardown.
        stop_main_loop_watchdog();

        // Then the render thread: run() joins the last frame it kicked
        // when the loop ends; a frame still outstanding (run() left by an
        // exception) is joined here. Then the dedicated thread ends; no
        // render phase may run while the GPU is quiesced and the parts are
        // torn down below.
        if (m_render_thread) {
            join_outstanding_frame();
            m_render_thread->stop();
        }
        // The packet holds strong references (the indirect diffuse scene
        // root, the lightmap atlas): released now, before the parts their
        // destructors reach (the mesh memory a scene's meshes allocate from)
        // are torn down below, not when the member itself is destroyed.
        m_frame_packet = Frame_packet{};

        // Quiesce the GPU before tearing any part down. wait_idle() drives
        // frame_completed() for every submitted frame and then drains any
        // leftover completion handlers (e.g. an Id_renderer pixel read-back
        // whose XR-submitted command buffer was never polled by a following
        // frame at exit). Those handlers release the ring-buffer ranges held
        // by Transfer_entry; running them here -- while Id_renderer and the
        // other parts are still alive -- avoids both the
        // ~Ring_buffer_range "not released / cancelled" assert and the
        // use-after-free that would occur if the device destructor ran the
        // same handlers after their owning parts were gone.
        if (m_graphics_device) {
            // The presentation callback reaches this editor's pacer.
            m_graphics_device->get_display_refresh_resolver().set_presentation_callback({});
            m_frame_pacing_observer.set_deadline_ceiling_callback({});
            m_graphics_device->wait_idle();
        }

        // No frame renders any more: the rendergraph destroys the nodes it
        // retired for frames in flight now, while every part they reference
        // is alive, and every node released from here on at once.
        if (m_rendergraph) {
            m_rendergraph->end_rendering();
        }

        // The imgui renderer retains a Texture_reference for every image drawn
        // in the last frames, and those references can be Rendergraph_nodes
        // owned elsewhere (Viewport_window draws a viewport by passing its
        // rendergraph output node, which is the Viewport_scene_view itself
        // when post processing is disabled). Imgui_renderer is declared before
        // Rendergraph and Scene_views, so it is destroyed last and would drop
        // the last reference - running ~Viewport_scene_view - after its owner
        // is gone. Release them here, GPU idle and everything still alive.
        if (m_imgui_renderer) {
            m_imgui_renderer->release_texture_references();
        }

        // Wait for all async tasks to complete, then clear task handles
        // and destroy the executor while m_mesh_memory is still alive.
        // Without this, implicit member destruction destroys m_mesh_memory
        // (declared after m_item_task_guard) before m_item_task_guard, and clearing
        // the task handles cascades through shared_ptr drops to
        // Free_list_allocator::free() on the already-destroyed allocator.
        if (m_executor) {
            m_executor->wait_for_all();
        }
        // wait_for_all() has run every closed scene's payload release task;
        // a scene root destroyed during the rest of the teardown releases its
        // payload on this thread.
        m_scene_payload_release.wait_idle();
        m_scene_payload_release.set_executor(nullptr);
        m_item_task_guard.clear();
        // Commits the drained workers left behind own scene roots / shapes;
        // drop them now, while mesh memory and scenes are still alive.
        m_scene_commit_queue.clear();
        erhe::raytrace::set_task_spawner({});
        m_executor.reset();

        if (m_mcp_server) {
            m_mcp_server->stop();
            m_mcp_server.reset();
        }
        m_default_scene_browser.reset();
        m_default_scene.reset();
        m_default_content_library.reset();

        // Tear the operation stack down ahead of Scene_builder so any
        // pending or executed operations holding raw Scene_builder*
        // (see Scene_builder_floor_resources_operation) are destroyed
        // before the builder. Reverse-declaration order would put
        // Scene_builder first (its declaration follows
        // m_operation_stack), so we drop the operations explicitly.
        m_operation_stack.reset();
    }
    void fill_app_context()
    {
        ERHE_PROFILE_FUNCTION();

        m_app_context.executor                 = m_executor.get();
        m_app_context.scene_commit_queue       = &m_scene_commit_queue;
        m_app_context.scene_payload_release    = &m_scene_payload_release;
        m_scene_payload_release.set_executor(m_executor.get());

        m_app_context.commands                 = m_commands              .get();
        m_app_context.graphics_device          = m_graphics_device       .get();
        m_app_context.imgui_renderer           = m_imgui_renderer        .get();
        m_app_context.imgui_windows            = m_imgui_windows         .get();
        m_app_context.debug_renderer           = m_debug_renderer        .get();
        m_app_context.rendergraph              = m_rendergraph           .get();
        m_app_context.text_renderer            = m_text_renderer         .get();
        m_app_context.forward_renderer         = m_forward_renderer      .get();
        m_app_context.shadow_renderer          = m_shadow_renderer       .get();
        m_app_context.gpu_cull_classifier      = m_gpu_cull_classifier   .get();
        m_app_context.light_cluster_gpu_binner = m_light_cluster_gpu_binner.get();
        m_app_context.cull_frame_registry      = m_cull_frame_registry   .get();
        m_app_context.texel_renderer           = m_texel_renderer        .get();
        m_app_context.shader_variant_cache     = m_shader_variant_cache  .get();
        m_app_context.program_interface        = m_program_interface     .get();
        m_app_context.context_window           = m_window                .get();
        m_app_context.brdf_slice               = m_brdf_slice            .get();
        m_app_context.brush_tool               = m_brush_tool            .get();
        m_app_context.clipboard                = m_clipboard             .get();
        m_app_context.clipboard_window         = m_clipboard_window      .get();
        m_app_context.create                   = m_create                .get();
        m_app_context.app_message_bus          = m_app_message_bus       .get();
        m_app_context.app_rendering            = m_app_rendering         .get();
        m_app_context.draw_list_renderer       = m_draw_list_renderer     .get();
        m_app_context.app_scenes               = m_app_scenes            .get();
        m_app_context.asset_manager            = m_asset_manager         .get();
        m_app_context.app_settings             = &m_app_settings;
        m_app_context.developer_config         = &m_editor_settings.developer;
        m_app_context.graphics_config          = &m_graphics_config;
        m_app_context.mesh_memory_config       = &m_mesh_memory_config;
        m_app_context.renderer_config          = &m_renderer_config;
        m_app_context.text_renderer_config     = &m_text_renderer_config;
        m_app_context.window_config            = &m_window_config;
        m_app_context.editor_settings          = &m_editor_settings;
        m_app_context.render_settings.frames   = &m_render_settings;
        // The main thread reads the latest copy from here on; the init
        // pump's renders use this one, the render phase of each frame the
        // copy its prepare phase made.
        m_render_settings.initialize(*m_graphics_device.get());
        m_render_settings.snapshot(m_graphics_device->get_frame_index(), m_editor_settings, m_app_settings);
        m_app_context.app_windows              = m_app_windows           .get();
        m_app_context.editor_windows           = m_editor_windows        .get();
        m_app_context.fly_camera_tool          = m_fly_camera_tool       .get();
        m_app_context.geometry_graph_window    = m_geometry_graph_window .get();
        m_app_context.rendergraph_window       = m_rendergraph_window    .get();
        m_app_context.texture_graph_window     = m_texture_graph_window  .get();
        m_app_context.navigation_gizmo_tool    = m_navigation_gizmo_tool .get();
        m_app_context.grid_tool                = m_grid_tool             .get();
#if defined(ERHE_XR_LIBRARY_OPENXR)
        m_app_context.hand_tracker             = m_hand_tracker          .get();
#endif
        m_app_context.headset_view             = m_headset_view          .get();
        m_app_context.hotbar                   = m_hotbar                .get();
        m_app_context.hud                      = m_hud                   .get();
        m_app_context.icon_set                 = m_icon_set              .get();
        m_app_context.id_renderer              = m_id_renderer           .get();
        m_app_context.input_bindings_store     = m_input_bindings_store  .get();
        m_app_context.input_bindings_window    = m_input_bindings_window .get();
        m_app_context.input_state              = m_input_state           .get();
        m_app_context.inventory_window         = m_inventory_window      .get();
        m_app_context.lattice_tool             = m_lattice_tool          .get();
        m_app_context.material_paint_tool      = m_material_paint_tool   .get();
        m_app_context.material_preview         = m_material_preview      .get();
        m_app_context.brush_preview            = m_brush_preview         .get();
        m_app_context.mesh_memory              = m_mesh_memory           .get();
        m_app_context.mesh_component_selection      = m_mesh_component_selection     .get();
        m_app_context.mesh_component_selection_tool = m_mesh_component_selection_tool.get();
        m_app_context.content_wide_line_renderer = m_content_wide_line_renderer.get();
        m_app_context.move_tool                = m_move_tool             .get();
        m_app_context.node_properties_window   = m_node_properties_window.get();
        m_app_context.operation_stack          = m_operation_stack       .get();
        m_app_context.operations               = m_operations            .get();
        m_app_context.properties               = m_properties            .get();
        m_app_context.geometry_spreadsheet_window = m_geometry_spreadsheet_window.get();
        m_app_context.paint_tool               = m_paint_tool            .get();
        m_app_context.weight_paint_tool        = m_weight_paint_tool     .get();
        m_app_context.physics_tool             = m_physics_tool          .get();
        m_app_context.post_processing          = m_post_processing       .get();
        m_app_context.prefab_library           = m_prefab_library        .get();
        m_app_context.sky_renderer             = m_sky_renderer          .get();
        m_app_context.ray_trace_renderer       = m_ray_trace_renderer    .get();
        m_app_context.ddgi_renderer            = m_ddgi_renderer         .get();
        m_app_context.radiance_cascades_renderer = m_radiance_cascades_renderer.get();
        // Probe overlays (Ddgi_config / Radiance_cascades_config
        // debug_draw_probes): Renderables, drawn with the other scene-view
        // overlays.
        if (m_app_rendering && m_ddgi_renderer) {
            m_app_rendering->add(m_ddgi_renderer.get());
        }
        if (m_app_rendering && m_radiance_cascades_renderer) {
            m_app_rendering->add(m_radiance_cascades_renderer.get());
        }
        m_app_context.lightmap_baker           = m_lightmap_baker        .get();
        m_app_context.lightmap_partitioner     = m_lightmap_partitioner  .get();
        m_app_context.lightmap_report          = m_lightmap_report       .get();
        m_app_context.lightmap_streamer        = m_lightmap_streamer     .get();
        m_app_context.programs                 = m_programs              .get();
        m_app_context.rotate_tool              = m_rotate_tool           .get();
        m_app_context.scale_tool               = m_scale_tool            .get();
        m_app_context.scene_builder            = m_scene_builder         .get();
        m_app_context.scene_commands           = m_scene_commands        .get();
        m_app_context.selection                = m_selection             .get();
        m_app_context.selection_tool           = m_selection_tool        .get();
        m_app_context.settings_window          = m_settings_window       .get();
        m_app_context.sheet_window             = m_sheet_window          .get();
        m_app_context.texture_file_loader      = m_texture_file_loader   .get();
        m_app_context.thumbnails               = m_thumbnails            .get();
        m_app_context.time                     = m_time                  .get();
        m_app_context.animation_player         = m_animation_player      .get();
        m_app_context.animation_window         = m_animation_window      .get();
        m_app_context.tools                    = m_tools                 .get();
        m_app_context.transform_tool           = m_transform_tool        .get();
        m_app_context.analytic_hover_providers.push_back(m_transform_tool.get());
        m_app_context.scene_views              = m_viewport_scene_views  .get();

        // Subsystems whose live state lives outside Editor_settings_config /
        // User_state_config provide collect callbacks; the store copies their
        // state into the config before change detection / saving. Sections
        // edited directly in the config structs (Settings window) need no
        // callback.
        m_app_settings.settings_store().register_collect_callback(
            [this](Editor_settings_config& settings, User_state_config&) {
                m_grid_tool->write_config(settings.grid);
            }
        );
        m_app_settings.settings_store().register_collect_callback(
            [this](Editor_settings_config&, User_state_config& user_state) {
                m_inventory_window->write_config(user_state.inventory);
            }
        );
    }

    auto on_key_event(const erhe::window::Input_event& input_event) -> bool override
    {
        const bool shift   = erhe::utility::test_bit_set(input_event.u.key_event.modifier_mask, erhe::window::Key_modifier_bit_shift);
        const bool control = erhe::utility::test_bit_set(input_event.u.key_event.modifier_mask, erhe::window::Key_modifier_bit_ctrl);
        const bool alt     = erhe::utility::test_bit_set(input_event.u.key_event.modifier_mask, erhe::window::Key_modifier_bit_menu);
        const bool changed = (shift != m_input_state->shift) || (control != m_input_state->control) || (alt != m_input_state->alt);
        m_input_state->shift   = shift;
        m_input_state->control = control;
        m_input_state->alt     = alt;
        // The modifier change site: the mesh component loop select preview
        // depends on Alt / Ctrl.
        if (changed && m_mesh_component_selection_tool) {
            m_mesh_component_selection_tool->on_modifiers_changed();
        }
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

    auto on_window_scale_event(const erhe::window::Input_event& input_event) -> bool override
    {
        // Runtime DPI / display-scale change (e.g. window moved to a
        // different-density external display). Update only the ImGui font
        // scale; fonts rescale on demand (ImGui 1.92 dynamic font atlas), so
        // no atlas rebuild is needed. Deliberately not apply_limits() - only
        // scale_factor changes here. The framebuffer/swapchain side is driven
        // separately by the paired SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, which
        // arrives as a window_resize_event.
        const float new_scale = input_event.u.window_scale_event.scale;
        if (new_scale != m_app_settings.imgui.scale_factor) {
            m_app_settings.imgui.scale_factor = new_scale;
            m_imgui_renderer->on_font_config_changed(m_app_settings.imgui);
        }
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

    // Build the default scene: a Scene_root with the given name, registered into
    // the editor scene list, given its content-library + browser windows, and handed
    // to Scene_builder so scene.add_* can populate it. Invoked by the scene.create
    // startup command. Publishing Scene_created_message attaches the global tools
    // (Hud / Hotbar / OpenXR Headset_view) to this scene.
    void create_default_scene(const std::string& name)
    {
        if (m_default_scene) {
            log_startup->warn("scene.create: a scene already exists ('{}'); ignoring", m_default_scene->get_name());
            return;
        }
        const bool enable_physics = m_editor_settings.physics.static_enable;
        // The default scene gets its own content library, seeded exactly like
        // Scene_commands::create_new_scene: brushes copied from the
        // Scene_builder template library (per-scene Brush items, shared
        // payload) plus fresh default materials. The template library itself
        // (m_default_content_library) is never owned by any scene - scenes
        // own their library items (item host = the Scene_root), and an item
        // is a member of exactly one library.
        std::shared_ptr<Content_library> content_library = std::make_shared<Content_library>();
        if (m_default_content_library) {
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_default_content_library->mutex};
            copy_content_library(*m_default_content_library, *content_library);
        }
        add_default_materials(*content_library.get());
        add_default_physics_materials(*content_library.get());
        const Draw_list_scene_dependencies draw_list_dependencies = make_draw_list_scene_dependencies(m_app_context);
        m_default_scene = std::make_shared<Scene_root>(
            m_app_message_bus.get(),
            content_library,
            name,
            enable_physics,
            &draw_list_dependencies,
            make_scene_root_material_set_create_info(m_app_context, "Scene forward material set")
        );
        // A from-scratch scene has no source path, so its tile-set
        // directory is the shared untitled.lightmap/ - whatever a previous
        // unsaved scene left there is foreign to this one (mismatched
        // manifests / payload sizes error into the log and stale tiles
        // stream in). Start clean.
        {
            const std::filesystem::path directory = Lightmap_tile_io::directory_for_scene(m_default_scene->get_source_path());
            std::string error;
            const int removed = Lightmap_tile_io::delete_tile_set(directory, &error);
            if (removed > 0) {
                log_startup->info("scene.create: removed {} stale lightmap tile files from {}", removed, directory.string());
            } else if (removed < 0) {
                log_startup->warn("scene.create: could not clear stale lightmap tiles: {}", error);
            }
        }
        m_default_scene->register_to_editor_scenes(*m_app_scenes);
        // The content library is shown nested under the Scene row in the Hierarchy
        // window (#240); the standalone Content Library window was removed (#241).
        m_default_scene_browser = m_default_scene->make_browser_window(
            *m_imgui_renderer.get(), *m_imgui_windows.get(), m_app_context, m_app_settings
        );
        m_scene_builder->set_scene_root(m_default_scene);
        m_app_message_bus->scene_created.send_message(Scene_created_message{m_default_scene});
    }

    // Attach the global tools to the FIRST scene that is created (scene.create) or
    // loaded. Subsequent scenes do not re-home the tools.
    // The global tools (Hud, Hotbar, OpenXR Headset_view) are homed in one
    // scene at a time: their nodes are content of that scene and the headset
    // renders it (doc/editor/tools.md "Headset scene"). The first scene created
    // or loaded homes them; closing their scene moves them to the most
    // recently opened remaining scene; home_tools() moves them on request.
    void on_scene_created(Scene_created_message& message)
    {
        if (m_tools_attached_to_scene || !message.scene_root) {
            return;
        }
        attach_tools(message.scene_root);
    }

    void attach_tools(const std::shared_ptr<Scene_root>& scene_root)
    {
        m_tools_attached_to_scene = true;
        m_tools_scene_root = scene_root;
        if (m_hud) {
            m_hud->attach_to_scene(scene_root);
        }
        if (m_hotbar) {
            m_hotbar->attach_to_scene(scene_root);
        }
#if defined(ERHE_XR_LIBRARY_OPENXR)
        if (m_app_context.OpenXR && m_headset_view) {
            m_headset_view->attach_to_scene(scene_root, *m_mesh_memory.get());
        }
#endif
        log_scene->info("Global tools homed in scene '{}'", scene_root->get_name());
    }

    void detach_tools()
    {
        if (m_hud) {
            m_hud->detach_from_scene();
        }
        if (m_hotbar) {
            m_hotbar->detach_from_scene();
        }
#if defined(ERHE_XR_LIBRARY_OPENXR)
        if (m_app_context.OpenXR && m_headset_view) {
            m_headset_view->detach_from_scene();
        }
#endif
        m_tools_attached_to_scene = false;
        m_tools_scene_root.reset();
    }

    // Moves the global tools into scene_root (a registered scene). From the
    // message bus pump (Home_tools_message).
    void home_tools(const std::shared_ptr<Scene_root>& scene_root)
    {
        const std::vector<std::shared_ptr<Scene_root>>& scene_roots = m_app_scenes->get_scene_roots();
        if (std::find(scene_roots.begin(), scene_roots.end(), scene_root) == scene_roots.end()) {
            log_scene->warn("Cannot home the global tools in scene '{}': the scene is not open", scene_root->get_name());
            return;
        }
        if (m_tools_attached_to_scene && (m_tools_scene_root.lock() == scene_root)) {
            return;
        }
        if (m_tools_attached_to_scene) {
            detach_tools();
        }
        attach_tools(scene_root);
    }

    // Close a scene (Hierarchy window Scene row "Close" context menu entry).
    // Runs from the message bus pump, outside ImGui iteration, because it
    // destroys ImGui windows (the scene's viewports and browser window).
    // Applies the two visitors to the primary graph editor windows and to
    // every extra "Open Editor" window. Both the scene-close teardown and the
    // item-removal cleanup below have to reach the same set.
    template <typename Geometry_visitor, typename Texture_visitor>
    void for_each_graph_window(Geometry_visitor&& geometry_visitor, Texture_visitor&& texture_visitor)
    {
        geometry_visitor(*m_geometry_graph_window.get());
        texture_visitor (*m_texture_graph_window.get());
        for (const std::shared_ptr<Geometry_graph_window>& window : m_editor_windows->get_extra_geometry_graph_windows()) {
            geometry_visitor(*window.get());
        }
        for (const std::shared_ptr<Texture_graph_window>& window : m_editor_windows->get_extra_texture_graph_windows()) {
            texture_visitor(*window.get());
        }
    }

    // Content taken out of the editor without a scene closing - undo of a
    // glTF import, a node delete, a scene leaving the registry. Parts that
    // cache their own references subscribe themselves; this handles the
    // references Editor owns (doc/editor/import_undo_reference_clearing.md).
    void on_items_removed(Items_removed_message& message)
    {
        const Removed_items& removed = *message.removed.get();

        // The window's resolved shared_ptr is what keeps the asset alive, so
        // the weak target never expires on its own.
        for_each_graph_window(
            [&removed](Geometry_graph_window& window) {
                const std::shared_ptr<Graph_mesh> target = window.get_target();
                if (target && removed.lookup.contains(target.get())) {
                    window.set_target({});
                }
            },
            [&removed](Texture_graph_window& window) {
                const std::shared_ptr<Graph_texture> target = window.get_target();
                if (target && removed.lookup.contains(target.get())) {
                    window.set_target({});
                }
            }
        );

        // Selection prunes itself (and its last-selected map) - see
        // Selection::on_items_removed.
    }

    void on_close_scene(Close_scene_message& message)
    {
        const std::shared_ptr<Scene_root>& scene_root = message.scene_root;
        if (!scene_root) {
            return;
        }

        const bool is_tools_scene = m_tools_attached_to_scene && (m_tools_scene_root.lock() == scene_root);

        // Drop selected items hosted by this scene (including the Scene item,
        // whose host is also this scene_root) so the selection does not keep
        // dead-scene items alive or feed them to tools. Other scenes'
        // selections are untouched.
        // The active item is tracked beside the selection and can be outside
        // it, so the close forgets it explicitly when this scene hosts it
        // (doc/editor/active_item.md D2).
        m_selection->clear_selection(static_cast<erhe::Item_host*>(scene_root.get()), Active_item::forget_hosted);

        // The lightmap partitioner stores shared_ptrs to this scene's meshes
        // and piece nodes; drop them so the closed scene's content is released.
        if (m_lightmap_partitioner) {
            m_lightmap_partitioner->on_scene_closed(scene_root.get());
        }

        // Stop tracking this scene as the active scene; consumers fall back
        // via Selection::get_active_scene_root() until another scene is
        // activated by selection or window focus.
        if (m_selection->get_active_scene_root() == scene_root) {
            m_selection->set_active_scene_root({});
        }

        // Detach the global tools when they are homed in this scene; they
        // re-home to another scene below (or to the next created scene).
        if (is_tools_scene) {
            detach_tools();
        }

        // Recorded undo/redo operations hold shared_ptrs to scene content and
        // viewport resources (e.g. Scene_builder_viewport_resources_operation
        // owns the startup viewport): they would keep the closed scene's
        // objects alive and its viewport rendergraph nodes registered -- and
        // executing against the torn-down scene every frame. Drop the history.
        m_operation_stack->clear_history();

        // The lightmap bake working set holds a BLAS per traced mesh - each
        // pinning its Primitive and the GPU ranges behind it - plus the
        // G-buffer and accumulation targets. Pause semantics keep all of that
        // on a plain disable so a resume continues where it left off, but a
        // closed scene is never resumed (doc/editor/reloadable_asset_loads.md).
        if (m_lightmap_baker) {
            m_lightmap_baker->release_working_set();
        }

        // Graph editor windows (the primaries and the "Open Editor" extras)
        // may be editing a Graph_mesh / Graph_texture asset owned by this
        // scene's content library. The target weak_ptr does not expire on
        // its own here - the window's resolved shared_ptr keeps the asset
        // alive - so clear it explicitly, or the window keeps showing (and
        // editing) content of the closed scene.
        //
        // Part-owned close cleanup (tool state, caches) is NOT here: parts
        // subscribe to close_scene themselves and drop their own hosted
        // references (Brush_tool, Material_paint_tool, Material_preview,
        // Brdf_slice, Physics_tool, Operations, Animation_player /
        // Animation_window; see doc/editor/coding_rules.md "Scene-hosted references").
        {
            erhe::Item_host* const closing_host = static_cast<erhe::Item_host*>(scene_root.get());
            for_each_graph_window(
                [closing_host](Geometry_graph_window& window) {
                    const std::shared_ptr<Graph_mesh> target = window.get_target();
                    if (target && (target->get_item_host() == closing_host)) {
                        window.set_target({});
                    }
                },
                [closing_host](Texture_graph_window& window) {
                    const std::shared_ptr<Graph_texture> target = window.get_target();
                    if (target && (target->get_item_host() == closing_host)) {
                        window.set_target({});
                    }
                }
            );
        }

        // Viewport windows are not destroyed with the scene: they exist
        // independent of scenes (their open state persists in the windows
        // visibility config). Unbind them instead -- they stay open, empty,
        // until another scene binds into them.
        m_viewport_scene_views->unbind_views_from_scene(scene_root);
        scene_root->remove_browser_window();
        scene_root->unregister_from_editor_scenes(*m_app_scenes);

        if (m_default_scene == scene_root) {
            m_default_scene_browser.reset();
            m_default_scene.reset();
        }
        // Scene_builder targets this scene for the scene.add_* commands; drop
        // the reference so they do not build into a closed scene (same state
        // as a --no-scene start before any scene.create).
        if (m_scene_builder && (m_scene_builder->get_scene_root() == scene_root)) {
            m_scene_builder->set_scene_root(std::shared_ptr<Scene_root>{});
        }

        // Re-home the tools to the most recently opened remaining scene
        // (scenes are registered in open order), when one exists; otherwise
        // the next Scene_created_message re-attaches them.
        if (is_tools_scene) {
            const std::vector<std::shared_ptr<Scene_root>>& remaining = m_app_scenes->get_scene_roots();
            if (!remaining.empty()) {
                attach_tools(remaining.back());
            }
        }

        // Queue the scene for the scene-close leak watchdog. The watch itself
        // is armed in update_scene_close_leak_watches(), which runs after the
        // message bus pump in tick(): by then EVERY close_scene subscriber
        // (this handler plus the per-part cleanup subscriptions) has run,
        // regardless of subscription order, so content the cleanup
        // legitimately re-homed (e.g. HUD / Hotbar nodes detached from a
        // tools scene) is never tracked. The shared_ptr keeps the scene alive
        // until the arming collects its weak references.
        m_scene_roots_pending_close_watch.push_back(scene_root);

        log_scene->info("Closed scene '{}'", scene_root->get_name());
    }

    // Scene-close leak watchdog. Called from tick() after the message bus
    // pump. First arms watches for scenes whose close was handled this frame
    // (queued by on_close_scene): collects weak references to the closed
    // scene's content (the scene root, its nodes, its owned content-library
    // items). Because arming runs after the pump, every close_scene
    // subscriber's cleanup has already run and re-homed content is never
    // tracked. Then checks armed watches a fixed frame count after the
    // close, so legitimately deferred releases (an in-flight background
    // graph evaluation holding its target, queued ImGui window teardown)
    // have drained; warns - does not abort - because rare legitimate
    // survivors exist (e.g. a prefab template whose instances in other
    // scenes keep resources alive). Anything reported is an instance of the
    // scene-close bug class: a subsystem cached a shared_ptr to scene-hosted
    // content and did not enroll in close cleanup (see doc/editor/coding_rules.md
    // "Scene-hosted references in editor parts"). Items intentionally kept
    // alive by inventory / hotbar slots (persistent inventory) are reported
    // as info, not warnings.
    void update_scene_close_leak_watches()
    {
        std::vector<std::uint64_t> courtesy_unload_records;
        for (const std::shared_ptr<Scene_root>& scene_root : m_scene_roots_pending_close_watch) {
            Scene_close_leak_watch watch;
            watch.scene_name       = scene_root->get_name();
            watch.frames_remaining = k_scene_close_leak_check_frames;
            watch.scene_root       = scene_root;
            const std::shared_ptr<Content_library> library = scene_root->get_content_library();
            if (library) {
                for (const uint64_t kind_type_bit : Content_library::get_kind_type_bits()) {
                    for (const std::shared_ptr<erhe::Item_base>& item : library->get_all_of_kind(kind_type_bit)) {
                        // Every listed resource is a prim this scene owns, so
                        // every one of them must die with it
                        // (doc/erhe/usd_compatibility_design.md U4).
                        if (item) {
                            watch.items.emplace_back(item);
                        }
                    }
                }
            }
            scene_root->get_scene().for_each_node(
                [&watch](const std::shared_ptr<erhe::scene::Node>& node) {
                    watch.items.emplace_back(node);
                    return true;
                }
            );
            m_scene_close_leak_watches.push_back(std::move(watch));

            // R5.6: sever the record's scene identity while the Scene_root
            // is still alive (the pointers must not dangle past the clear()
            // below) and queue the courtesy unload (plan resolution 4).
            if (m_asset_manager) {
                const std::uint64_t record_id = m_asset_manager->detach_scene_record(scene_root.get());
                if (record_id != 0) {
                    courtesy_unload_records.push_back(record_id);
                }
            }
        }
        // In the clean case this destroys the Scene_root: the content
        // library's nodes release their entry userships here, so the
        // courtesy unload below sees only EXTERNAL users.
        m_scene_roots_pending_close_watch.clear();
        // Courtesy unload (R5.6, plan resolution 4): success means closing a
        // scene frees its assets (memory behavior matches the pre-flip
        // library ownership); refusal (slots, other scenes' reference
        // entries, debug holds) is normal and keeps the container loaded -
        // its assets then report below as manager-pinned info, not leaks.
        for (const std::uint64_t record_id : courtesy_unload_records) {
            m_asset_manager->courtesy_unload_container(record_id);
        }

        for (std::size_t i = 0; i < m_scene_close_leak_watches.size(); ) {
            Scene_close_leak_watch& watch = m_scene_close_leak_watches[i];
            --watch.frames_remaining;
            if (watch.frames_remaining > 0) {
                ++i;
                continue;
            }
            // Items intentionally pinned by the clipboard: copied content
            // (and what it transitively holds, e.g. mesh materials) stays
            // alive so paste-after-source-scene-close works.
            std::unordered_set<const erhe::Item_base*> clipboard_pinned_items;
            if (m_app_context.clipboard != nullptr) {
                m_app_context.clipboard->collect_pinned_items(clipboard_pinned_items);
            }
            constexpr std::size_t max_reported = 16;
            std::size_t survivor_count = 0;
            std::size_t pinned_count   = 0;
            std::size_t rehomed_count  = 0;
            const std::shared_ptr<Scene_root> pinned_scene_root = watch.scene_root.lock();
            if (pinned_scene_root) {
                ++survivor_count;
                log_scene->warn(
                    "scene-close leak: Scene_root '{}' is still alive {} frames after close",
                    watch.scene_name, k_scene_close_leak_check_frames
                );
            }
            for (const std::weak_ptr<erhe::Item_base>& weak_item : watch.items) {
                const std::shared_ptr<erhe::Item_base> item = weak_item.lock();
                if (!item) {
                    continue;
                }
                // Items the asset manager keeps alive intentionally: a
                // manager-owned strong reference (builtin, loaded container,
                // a surviving scene record after a refused courtesy unload -
                // including its materials' record-transitive texture pins)
                // or a declared usership (slots, tools, debug holds). The
                // R2-era slot whitelist is gone: the surviving record's
                // strong entries cover the former transitive pins.
                if (m_asset_manager && m_asset_manager->is_pinned(item.get())) {
                    ++pinned_count;
                    log_scene->info(
                        "scene-close check: {} '{}' of closed scene '{}' intentionally pinned by the asset manager",
                        item->get_type_name(), item->get_name(), watch.scene_name
                    );
                    continue;
                }
                if (clipboard_pinned_items.contains(item.get())) {
                    ++pinned_count;
                    log_scene->info(
                        "scene-close check: {} '{}' of closed scene '{}' intentionally pinned by the clipboard",
                        item->get_type_name(), item->get_name(), watch.scene_name
                    );
                    continue;
                }
                // An item hosted by a REGISTERED live scene at check time was
                // re-homed after the close (e.g. a clipboard paste explicitly
                // re-registered a material into another scene): it is owned
                // content of that scene now, not a leak of the closed one.
                {
                    erhe::Item_host* const item_host = item->get_item_host();
                    if ((item_host != nullptr) && m_app_scenes->is_host_registered(item_host)) {
                        ++rehomed_count;
                        log_scene->info(
                            "scene-close check: {} '{}' of closed scene '{}' re-homed into live scene host '{}'",
                            item->get_type_name(), item->get_name(), watch.scene_name, item_host->get_host_name()
                        );
                        continue;
                    }
                }
                ++survivor_count;
                if (survivor_count <= max_reported) {
                    // use_count - 1 excludes the lock above: the number of
                    // strong references actually pinning the item.
                    log_scene->warn(
                        "scene-close leak: {} '{}' of closed scene '{}' is still alive {} frames after close ({} holder(s))",
                        item->get_type_name(), item->get_name(), watch.scene_name, k_scene_close_leak_check_frames, item.use_count() - 1
                    );
                }
            }
            if (survivor_count > max_reported) {
                log_scene->warn(
                    "scene-close leak: ... and {} more surviving items of closed scene '{}'",
                    survivor_count - max_reported, watch.scene_name
                );
            }
            if (survivor_count == 0) {
                log_scene->info(
                    "scene-close check: all {} tracked items of closed scene '{}' released ({} intentionally pinned)",
                    watch.items.size(), watch.scene_name, pinned_count
                );
            }
            m_scene_close_leak_watches.erase(m_scene_close_leak_watches.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }

    void run_set_property_command(simdjson::ondemand::object* args_obj)
    {
        if (args_obj == nullptr) {
            log_startup->warn("commands.json: scene.set_property requires args 'item', 'property' and optionally 'value'");
            return;
        }
        if (!m_default_scene) {
            log_startup->warn("commands.json: scene.set_property needs a scene; run scene.create first (skipping)");
            return;
        }
        std::string_view item_sv;
        std::string_view property_sv;
        std::string_view value_sv;
        if (args_obj->find_field_unordered("item").get_string().get(item_sv) != simdjson::SUCCESS) {
            log_startup->warn("commands.json: scene.set_property requires a string 'args.item'");
            return;
        }
        if (args_obj->find_field_unordered("property").get_string().get(property_sv) != simdjson::SUCCESS) {
            log_startup->warn("commands.json: scene.set_property requires a string 'args.property'");
            return;
        }
        const bool has_value = (args_obj->find_field_unordered("value").get_string().get(value_sv) == simdjson::SUCCESS);
        std::string_view expression_sv;
        const bool has_expression = (args_obj->find_field_unordered("expression").get_string().get(expression_sv) == simdjson::SUCCESS);
        uint64_t sub_object_index = 0;
        const bool has_sub_object = (args_obj->find_field_unordered("sub_object").get_uint64().get(sub_object_index) == simdjson::SUCCESS);

        const std::shared_ptr<erhe::Item_base> item = find_item_in_scene_by_name(*m_default_scene, item_sv);
        if (!item) {
            log_startup->warn("commands.json: scene.set_property: item '{}' not found", item_sv);
            return;
        }
        // D29: an optional sub-object index (a mesh primitive).
        std::optional<std::size_t>         sub_object;
        erhe::property::Dependency_object* target = item.get();
        if (has_sub_object) {
            sub_object = static_cast<std::size_t>(sub_object_index);
            target     = item->get_property_sub_object(sub_object.value());
            if (target == nullptr) {
                log_startup->warn("commands.json: scene.set_property: '{}' has no sub-object {}", item->get_name(), sub_object.value());
                return;
            }
        }
        const erhe::property::Dependency_property* property = erhe::property::Property_registry::get().find_for_object(*target, property_sv);
        if (property == nullptr) {
            log_startup->warn("commands.json: scene.set_property: {} '{}' has no property '{}'", item->get_type_name(), item->get_name(), property_sv);
            return;
        }
        if (item->is_sealed()) {
            log_startup->warn("commands.json: scene.set_property: '{}' is sealed (lock_edit)", item->get_name());
            return;
        }
        std::optional<erhe::property::Local_state> after;
        if (has_expression) {
            std::string error;
            if (!erhe::property::validate_expression_text(*property, expression_sv, error)) {
                log_startup->warn("commands.json: scene.set_property: expression '{}' for '{}' rejected: {}", expression_sv, property_sv, error);
                return;
            }
            after = erhe::property::Local_state{erhe::property::Expression_text{std::string{expression_sv}}};
        } else if (has_value) {
            std::optional<erhe::property::Property_value> value;
            if (erhe::property::is_object_reference_type(property->get_type())) {
                // D28: a name resolved in the item's scene; empty clears.
                const std::shared_ptr<erhe::Item_base> referenced = value_sv.empty() ? std::shared_ptr<erhe::Item_base>{} : resolve_reference_by_name(m_app_context, *item, value_sv);
                if (!value_sv.empty() && !referenced) {
                    log_startup->warn("commands.json: scene.set_property: '{}' does not name an item of the scene of '{}'", value_sv, item->get_name());
                    return;
                }
                value = erhe::property::make_object_reference(property->get_type(), referenced);
            } else {
                value = erhe::property::parse_value(*property, value_sv);
            }
            if (!value.has_value()) {
                log_startup->warn("commands.json: scene.set_property: '{}' is not a valid {} for '{}'", value_sv, erhe::property::c_str(property->get_type()), property_sv);
                return;
            }
            after = erhe::property::Local_state{std::move(value.value())};
        }
        log_startup->info("commands.json: scene.set_property {} '{}' {} = {}", item->get_type_name(), item->get_name(), property_sv, describe_local_state(*property, after));
        m_app_context.operation_stack->queue(
            std::make_shared<Property_set_operation>(item, sub_object, *property, target->read_local_state(*property), after)
        );
    }

    void run_startup_script()
    {
        ERHE_PROFILE_FUNCTION();

        // --no-scene: start with an empty scene -- the scene File > New Scene
        // creates (Scene_commands::create_new_scene(): a default camera and
        // its viewport, no lights, no meshes); no procedural default scene and
        // no scene load. Takes precedence over --scene. Queued like the --scene
        // load, so it runs once the main loop pumps the message bus.
        if (m_no_startup_scene) {
            log_startup->info("Starting with an empty scene (--no-scene); skipping procedural startup script and scene load");
            m_app_message_bus->create_scene.queue_message(Create_scene_message{});
            return;
        }

        // --scene <file>: load a saved scene file (.glb / .gltf) on startup
        // instead of procedurally building the default scene. Uses the same
        // queued load path as File > Load Scene / the scene.load_scene command; the
        // load runs once the main loop starts pumping the message bus, and the
        // resulting Scene_created_message homes the global tools (Hud / Hotbar /
        // Headset_view) onto it. Takes precedence over the commands.json script.
        if (!m_startup_scene_path.empty()) {
            m_resolved_startup_commands_path = m_startup_scene_path;
            log_startup->info("Loading startup scene '{}' (--scene); skipping procedural startup script", m_startup_scene_path);
            m_app_message_bus->load_scene_file.queue_message(
                Load_scene_file_message{ .path = std::filesystem::path{m_startup_scene_path} }
            );
            return;
        }

        // On OpenXR, prefer config/editor/openxr_commands.json (a smaller room
        // sized for room-scale passthrough) when the caller did not override
        // --commands. Mirrors the openxr_windows.json / graphics_presets_openxr.json
        // variants. A missing OpenXR script falls back to the default commands.json.
        std::string                commands_path = m_startup_commands_path;
        std::optional<std::string> file_content;
        if (m_editor_settings.headset.openxr && (commands_path == "config/editor/commands.json")) {
            const std::string openxr_commands_path = "config/editor/openxr_commands.json";
            file_content = erhe::file::read("commands script", openxr_commands_path);
            if (file_content.has_value()) {
                commands_path = openxr_commands_path;
                log_startup->info("Using OpenXR startup commands script '{}'", commands_path);
            }
        }
        if (!file_content.has_value()) {
            file_content = erhe::file::read("commands script", commands_path);
        }
        if (!file_content.has_value()) {
            log_startup->info("startup commands script '{}' not found; skipping startup script", commands_path);
            return;
        }
        m_resolved_startup_commands_path = commands_path;

        simdjson::ondemand::parser parser;
        simdjson::padded_string padded{file_content.value()};
        simdjson::ondemand::document doc;
        simdjson::error_code error = parser.iterate(padded).get(doc);
        if (error != simdjson::SUCCESS) {
            log_startup->error("commands.json: failed to parse JSON: {}", simdjson::error_message(error));
            return;
        }

        simdjson::ondemand::object root;
        error = doc.get_object().get(root);
        if (error != simdjson::SUCCESS) {
            log_startup->error("commands.json: top-level value is not an object");
            return;
        }

        simdjson::ondemand::array commands_array;
        error = root["commands"].get_array().get(commands_array);
        if (error != simdjson::SUCCESS) {
            log_startup->info("commands.json: no 'commands' array; skipping startup script");
            return;
        }

        Scene_commands& sc = *m_app_context.scene_commands;

        log_startup->info("commands.json: running startup script");

        // Ondemand parses lazily: parser.iterate() above does NOT validate the
        // whole document, so a syntax error later in the file (e.g. a missing
        // comma between command objects) surfaces here during iteration. Take
        // each element as a simdjson_result and check it - the implicit
        // value conversion of a plain range-for THROWS on error - and keep a
        // catch as backstop for anything the element processing itself throws.
        try {
        for (simdjson::simdjson_result<simdjson::ondemand::value> element_result : commands_array) {
            simdjson::ondemand::value element;
            const simdjson::error_code element_error = element_result.get(element);
            if (element_error != simdjson::SUCCESS) {
                log_startup->error(
                    "commands script '{}': malformed JSON in 'commands' array: {} - remaining commands skipped",
                    commands_path, simdjson::error_message(element_error)
                );
                break; // the document iterator is not usable past a parse error
            }
            std::string                name;
            bool                       has_args{false};
            simdjson::ondemand::object args_obj;

            simdjson::ondemand::json_type type;
            if (element.type().get(type) != simdjson::SUCCESS) {
                log_startup->warn("commands.json: malformed array element (type lookup failed)");
                continue;
            }

            if (type == simdjson::ondemand::json_type::string) {
                std::string_view sv;
                if (element.get_string().get(sv) == simdjson::SUCCESS) {
                    name = std::string{sv};
                }
            } else if (type == simdjson::ondemand::json_type::object) {
                simdjson::ondemand::object obj;
                if (element.get_object().get(obj) != simdjson::SUCCESS) {
                    log_startup->warn("commands.json: malformed object entry");
                    continue;
                }
                std::string_view name_sv;
                // simdjson ondemand only supports in-document-order key
                // access via operator[]; if the JSON entry has "args"
                // before "name", a second operator[]("name") after
                // operator[]("args") would fail. find_field_unordered
                // restarts from the object's first key, so the access
                // order of "name" and "args" no longer matters.
                if (obj.find_field_unordered("name").get_string().get(name_sv) != simdjson::SUCCESS) {
                    log_startup->warn("commands.json: object entry missing 'name' field");
                    continue;
                }
                name = std::string{name_sv};
                if (obj.find_field_unordered("args").get_object().get(args_obj) == simdjson::SUCCESS) {
                    has_args = true;
                }
            } else {
                log_startup->warn("commands.json: unexpected array element type");
                continue;
            }

            if (name.empty()) {
                log_startup->warn("commands.json: empty command name");
                continue;
            }

            // scene.create builds the (previously always-on) default scene: a
            // Scene_root with the given name, registered and given its content-library
            // and browser windows, and handed to Scene_builder so the scene.add_*
            // commands can populate it. Put it FIRST in commands.json, before the
            // add_* steps. Omitting it -- e.g. a load-only commands.json -- leaves no
            // default scene, so only a loaded scene exists. Handled directly (not via
            // the command registry) because it is editor-level scene lifecycle, not a
            // Scene_commands building step.
            if (name == "scene.create") {
                std::string scene_name = "Default Scene";
                if (has_args) {
                    std::string_view name_sv;
                    if (args_obj.find_field_unordered("name").get_string().get(name_sv) == simdjson::SUCCESS) {
                        scene_name = std::string{name_sv};
                    }
                }
                log_startup->info("commands.json: scene.create '{}'", scene_name);
                create_default_scene(scene_name);
                continue;
            }

            // scene.load_scene loads a saved scene file (path given in args)
            // instead of building a scene procedurally, so a commands.json can
            // replace the scene.add_* building steps with a single load. It is
            // handled here directly rather than through the command registry
            // because loading is a queued file action -- it reuses the exact same
            // path as File > Open Scene (Load_scene_file_message -> editor::load_scene
            // plus content-library / browser / viewport window setup) -- not a
            // Scene_commands building step. The queued message is delivered once the
            // main loop starts pumping the message bus.
            if (name == "scene.load_scene") {
                std::string_view path_sv;
                if (has_args && (args_obj.find_field_unordered("path").get_string().get(path_sv) == simdjson::SUCCESS)) {
                    std::filesystem::path path{std::string{path_sv}};
                    log_startup->info("commands.json: scene.load_scene '{}'", path.string());
                    m_app_message_bus->load_scene_file.queue_message(
                        Load_scene_file_message{ .path = path }
                    );
                } else {
                    log_startup->warn("commands.json: scene.load_scene requires a string 'args.path'");
                }
                continue;
            }

            if (name == "scene.set_property") {
                // {"name": "scene.set_property", "args": {"item": "<item name>", "property": "<name>", "value": "<string form>"}}
                // Sets one registered property of an item in the default
                // scene through Property_set_operation (undoable); value is
                // the erhe_property/property_string.hpp form, omitted to
                // reset to default (doc/erhe/property_system.md D13).
                run_set_property_command(has_args ? &args_obj : nullptr);
                continue;
            }

            erhe::commands::Command* command = m_commands->find_command(name);
            if (command == nullptr) {
                log_startup->warn("commands.json: unknown command '{}'", name);
                continue;
            }

            // Dispatch args by command name. The five scene.add_* mesh
            // commands take Make_mesh_args; scene.add_lights takes
            // Add_lights_args; everything else takes none.
            const bool is_mesh_command =
                (name == "scene.add_platonic_solids") ||
                (name == "scene.add_johnson_solids")  ||
                (name == "scene.add_curved_shapes")   ||
                (name == "scene.add_chain")           ||
                (name == "scene.add_toruses");

            // The scene.add_* commands populate the default scene through
            // Scene_builder, which only has a scene_root after scene.create. Skip
            // them with a clear warning otherwise, so a malformed commands.json
            // (add before create, or a load-only script) cannot crash.
            const bool needs_scene =
                is_mesh_command ||
                (name == "scene.add_cameras") ||
                (name == "scene.add_lights")  ||
                (name == "scene.add_room");
            if (needs_scene && !m_default_scene) {
                log_startup->warn("commands.json: '{}' needs a scene; run scene.create first (skipping)", name);
                continue;
            }

            if (is_mesh_command) {
                Make_mesh_args args{};
                if (has_args) {
                    deserialize(args_obj, args);
                }
                if      (name == "scene.add_platonic_solids") sc.get_add_platonic_solids_command().apply_args(args);
                else if (name == "scene.add_johnson_solids")  sc.get_add_johnson_solids_command ().apply_args(args);
                else if (name == "scene.add_curved_shapes")   sc.get_add_curved_shapes_command  ().apply_args(args);
                else if (name == "scene.add_chain")           sc.get_add_chain_command          ().apply_args(args);
                else if (name == "scene.add_toruses")         sc.get_add_toruses_command        ().apply_args(args);
            } else if (name == "scene.add_cameras") {
                Add_cameras_args args{};
                if (has_args) {
                    deserialize(args_obj, args);
                }
                sc.get_add_cameras_command().apply_args(args);
            } else if (name == "scene.add_lights") {
                Add_lights_args args{};
                if (has_args) {
                    deserialize(args_obj, args);
                }
                sc.get_add_lights_command().apply_args(args);
            } else if (name == "scene.add_room") {
                Add_room_args args{};
                if (has_args) {
                    deserialize(args_obj, args);
                }
                sc.get_add_room_command().apply_args(args);
            } else if (has_args) {
                log_startup->warn("commands.json: command '{}' does not accept args; ignoring args block", name);
            }

            log_startup->info("commands.json: invoking '{}'", name);
            const bool ok = command->try_call();
            if (!ok) {
                log_startup->warn("commands.json: command '{}' returned false", name);
            } else {
                log_startup->info("commands.json: '{}' returned ok", name);
            }
        }
        } catch (const simdjson::simdjson_error& e) {
            log_startup->error(
                "commands script '{}': JSON error while running startup script: {} - remaining commands skipped",
                commands_path, e.what()
            );
        }

        log_startup->info("commands.json: startup script complete");
    }

    void run()
    {
        ERHE_PROFILE_FUNCTION();

        m_run_started = true;
        if (m_benchmark_driver) {
            begin_benchmark();
        }
        // The frame loop starts: from here on the render-side asserts and
        // the frame phase asserts are live (see the constructor). In
        // dedicated mode the render thread published itself when
        // Render_thread was constructed (end of the constructor; nothing
        // renders between that and here); in inline mode the main thread is
        // the render role.
        if (m_render_thread_mode == Render_thread_mode::inline_main_thread) {
            erhe::utility::Thread_roles::set_render_thread(m_app_context.render_thread_id);
        }
        // TODO: https://registry.khronos.org/OpenGL/extensions/NV/GLX_NV_delay_before_swap.txt
        // Also:
        //  - Measure time since first swapbuffers
        //  - Count number of swapbuffers
        //  - Wait to avoid presenting frames faster than display refreshrate
        while (!m_close_requested && !m_app_context.close_requested) {
            // Drains what the main thread autoreleased during this iteration
            // (Metal; a no-op elsewhere). See Scoped_transient_object_pool.
            erhe::graphics::Scoped_transient_object_pool object_pool{};

            // Classify window activity for power saving and derive the
            // poll_events wait timeout. While unfocused or not visible we block
            // on events with a timeout instead of busy-spinning, which caps the
            // frame rate and yields the CPU. OpenXR drives its own pacing (the
            // desktop window is only a mirror), so never throttle under OpenXR.
            float wait_time = 0.0f;
            m_frame_activity = Frame_activity::active;
            if (!m_app_context.OpenXR && m_window->is_session_locked()) {
                // Session locked: presents cannot reach the display at all.
                // Pause presentation deliberately instead of waiting for the
                // swapchain to react to errors (with present timing active
                // the per-swapchain timing queue stops draining and
                // vkQueuePresentKHR is eventually rejected with
                // VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT; observed live on a
                // locked session). Resumes by itself on unlock - the state
                // is polled, so no unlock event needs to arrive.
                m_frame_activity = Frame_activity::hidden;
                const int fps = (m_app_context.hidden_fps > 0) ? m_app_context.hidden_fps : 1;
                wait_time = 1.0f / static_cast<float>(fps);
            } else if (!m_app_context.OpenXR && m_window->is_fullscreen() && !m_window->is_focused()) {
                // Fullscreen without input focus (alt-tabbed away): treat as
                // hidden REGARDLESS of power_save. An exclusive-fullscreen
                // swapchain cannot reach the display in this state; presenting
                // into it stops draining present-timing feedback until the
                // timing queue fills and vkQueuePresentKHR dies with
                // VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT (observed live on
                // alt-tab). Skipping rendering also releases the GPU to the
                // foreground application.
                m_frame_activity = Frame_activity::hidden;
                const int fps = (m_app_context.hidden_fps > 0) ? m_app_context.hidden_fps : 1;
                wait_time = 1.0f / static_cast<float>(fps);
            } else if (m_app_context.power_save && !m_app_context.OpenXR) {
                if (!m_window->is_visible()) {
                    m_frame_activity = Frame_activity::hidden;
                    const int fps = (m_app_context.hidden_fps > 0) ? m_app_context.hidden_fps : 1;
                    wait_time = 1.0f / static_cast<float>(fps);
                } else if (!m_window->is_focused()) {
                    m_frame_activity = Frame_activity::unfocused;
                    const int fps = (m_app_context.unfocused_fps > 0) ? m_app_context.unfocused_fps : 1;
                    wait_time = 1.0f / static_cast<float>(fps);
                }
            }
            // The frame start (device frame start, pacing wait) precedes
            // the poll: the poll is the last thing before frame(), so the
            // input M2 dispatches is the newest. A paused Android loop
            // iteration keeps the started frame for the next one.
            start_frame();
            m_window->poll_events(wait_time);
#if defined(ERHE_OS_ANDROID)
            // Skip render while the activity is in the background. SDL's
            // Java side blocks the event loop on pause via
            // SDL_HINT_ANDROID_BLOCK_ON_PAUSE, but we still need to avoid
            // submitting Vulkan work after WILL_ENTER_BACKGROUND.
            if (m_window->is_paused()) {
                std::this_thread::sleep_for(std::chrono::milliseconds{50});
                continue;
            }
#endif
            // The events this poll queued are dispatched at the start of the
            // next prepare phase (M2): dispatch reaches the input state,
            // tools, ImGui font config and the swapchain resize fields, which
            // the render phase reads only through what M2 copies for its
            // frame.
            m_input_events_pending = true;
            frame();

            if ((m_editor_settings.quit_after_frames > 0) &&
                (m_time->get_frame_number() >= static_cast<uint64_t>(m_editor_settings.quit_after_frames))) {
                m_close_requested = true;
            }
            // A benchmark run exits when it has measured (or failed).
            if (m_benchmark_driver && m_benchmark_driver->is_done()) {
                m_close_requested = true;
            }

            ERHE_PROFILE_FRAME_END
        }
        // The last kicked frame is joined here, so its result (a device
        // error) is accounted and no render phase runs past the loop.
        join_outstanding_frame();
        // Settings are autosaved from edit sites (touch()); this final flush
        // compares and writes regardless of the dirty marks, catching an edit
        // still deferred by a held mouse button and any change site that
        // failed to notify.
        m_app_settings.flush();
        m_run_stopped = true;
    }

    bool m_close_requested{false};
    // Main thread: run() queued input events that the next prepare phase
    // dispatches (see frame_prepare_phase()).
    bool m_input_events_pending{false};
    // Main thread: a kicked render phase has not been joined yet (see
    // join_outstanding_frame()).
    bool m_frame_outstanding{false};
    bool                                    m_run_started{false};
    std::atomic<bool>                       m_in_tick    {false};
    bool m_run_stopped    {false};

    // Window-activity classification for power saving (see run()). Computed
    // once per loop iteration and read by frame() to pause the simulation and
    // skip rendering while the window is not visible.
    enum class Frame_activity {
        active,    // focused and visible    -> full rate
        unfocused, // visible but not focused -> reduced rate
        hidden     // not visible             -> reduced rate, no render, sim paused
    };
    Frame_activity m_frame_activity{Frame_activity::active};

    // Main-loop stall watchdog. The main thread executes frame() with
    // m_in_tick == true from the start of M1 (which joins the previous
    // frame's render phase) until the kick,
    // holding m_mutex for that span (M1 and M2); R runs outside it, on the
    // render thread or inside the kick. If a frame spins (CPU-bound loop) or deadlocks on either
    // thread it never returns, so it cannot log where it is stuck. A render
    // phase that stalls is reported once the next frame() blocks in its
    // join.
    // This background thread watches the diagnostic breadcrumbs
    // (erhe::log::set_breadcrumb) of the main and render threads and reports
    // the newest of each, with the frame phase, when the frame fails to
    // progress past a threshold.
    // See doc/erhe/geogram.md.
    std::thread             m_watchdog_thread;
    std::mutex              m_watchdog_mutex;
    std::condition_variable m_watchdog_cv;
    bool                    m_watchdog_stop{false};
    // Guarded by m_watchdog_mutex. The thread hashes are set once, on the
    // first frame; the phases mirror Thread_roles' per-role phases (see
    // set_frame_phase() and set_render_frame_phase()).
    bool                       m_watchdog_threads_known{false}; // read and written by the main thread only
    std::size_t                m_main_thread_hash      {0};
    std::size_t                m_render_thread_hash    {0};
    erhe::utility::Frame_phase m_watchdog_main_frame_phase  {erhe::utility::Frame_phase::none};
    erhe::utility::Frame_phase m_watchdog_render_frame_phase{erhe::utility::Frame_phase::none};


    std::string                         m_startup_commands_path; // startup script path (--commands); declared first so it initializes before run_startup_script() runs
    std::string                         m_resolved_startup_commands_path; // the script run_startup_script() ran (OpenXR may substitute openxr_commands.json), or the --scene file
    // --benchmark: the timedemo driver (doc/plans/quest_performance.md
    // "Benchmark mode"), null otherwise. Main thread only.
    std::unique_ptr<Benchmark_driver>   m_benchmark_driver;
    std::string                         m_startup_scene_path;    // startup scene bundle path (--scene); when set, loaded instead of running the commands.json scene build
    bool                                m_no_startup_scene{false}; // --no-scene: start with an empty scene (default camera only; no procedural scene, no scene load); takes precedence over --scene
    Graphics_config                     m_graphics_config;
    Mesh_memory_config                  m_mesh_memory_config;
    Renderer_config                     m_renderer_config;
    Text_renderer_config                m_text_renderer_config;
    Window_config                       m_window_config;
    // Settings root: owns Editor_settings_store, which owns the loaded
    // Editor_settings_config and its autosave. Declared before all parts so
    // it outlives every collect-callback client (e.g. Scene_view dtors
    // unregister against the store).
    App_settings                        m_app_settings;
    Render_settings_frames              m_render_settings; // the render phase's copies of the settings, one per frame in flight (renderers/render_settings.hpp)
    // Convenience alias for the loaded config; same object as
    // m_app_settings.config().
    Editor_settings_config&             m_editor_settings{m_app_settings.config()};
    User_state_config&                  m_user_state     {m_app_settings.user_state()};

    std::unique_ptr<tf::Executor>       m_executor;
    Item_async_task_guard               m_item_task_guard; // destroyed before m_executor
    Scene_commit_queue                  m_scene_commit_queue; // cleared in shutdown after m_executor->wait_for_all()
    Scene_payload_release               m_scene_payload_release; // declared before the parts: outlives every Scene_root; executor detached in shutdown

    App_context                         m_app_context;

    // Reused by the status-bar callback so reading Geogram progress allocates no
    // heap memory in steady-state frames (its std::string keeps capacity).
    erhe::geometry::Geogram_progress_state m_geogram_progress;

    // Created at init (brushes are built into it by Scene_builder). The scene_root
    // and its windows below are created later, by create_default_scene() when the
    // scene.create startup command runs -- or stay null when commands.json only
    // loads a scene.
    std::shared_ptr<Content_library>        m_default_content_library;
    std::shared_ptr<Scene_root>             m_default_scene;
    std::shared_ptr<Item_tree_window>       m_default_scene_browser;

    // Global tools (Hud / Hotbar / OpenXR Headset_view) live inside a scene, but no
    // scene exists at init; this subscription attaches them to the first scene that
    // is created or loaded. m_tools_attached_to_scene makes that happen exactly once.
    erhe::message_bus::Subscription<Scene_created_message> m_scene_created_subscription;
    erhe::message_bus::Subscription<Close_scene_message>   m_close_scene_subscription;
    erhe::message_bus::Subscription<Items_removed_message>   m_items_removed_subscription;

    // Scene-close leak watchdog state (see on_close_scene /
    // update_scene_close_leak_watches).
    static constexpr int k_scene_close_leak_check_frames = 60;
    class Scene_close_leak_watch
    {
    public:
        std::string                                 scene_name;
        int                                         frames_remaining{0};
        std::weak_ptr<Scene_root>                   scene_root;
        std::vector<std::weak_ptr<erhe::Item_base>> items;
    };
    // Scenes closed this frame, waiting for their watch to be armed after
    // the message bus pump (the shared_ptr keeps the scene alive until the
    // arming collects its weak references).
    std::vector<std::shared_ptr<Scene_root>> m_scene_roots_pending_close_watch;
    std::vector<Scene_close_leak_watch>      m_scene_close_leak_watches;
    bool                                    m_tools_attached_to_scene{false};
    // The scene the global tools are currently homed in (set by
    // on_scene_created, cleared when that scene is closed).
    std::weak_ptr<Scene_root>               m_tools_scene_root{};
    erhe::message_bus::Subscription<Home_tools_message> m_home_tools_subscription;

    // No dependencies (constructors)
    std::unique_ptr<erhe::commands::Commands      > m_commands;
    std::unique_ptr<Input_bindings_store          > m_input_bindings_store;
    std::unique_ptr<App_message_bus               > m_app_message_bus;
    std::unique_ptr<Input_state                   > m_input_state;
    std::unique_ptr<Time                          > m_time;

    std::unique_ptr<Clipboard                              > m_clipboard;
    std::unique_ptr<erhe::window::Context_window           > m_window;
    std::unique_ptr<erhe::graphics::Device                 > m_graphics_device;
    // Frame pacing observer mode (implementation plan step P2.1): the pacer
    // fed by real inputs, enforcing nothing; decisions go to log_frame_pacing.
    erhe::frame_pacing::Frame_pacing_observer                m_frame_pacing_observer;
    // Release gating (FR2, step P2.3): high-resolution timer for the pacer
    // wait at the tick.
    erhe::time::Waitable_timer                               m_pacer_release_timer;
    // FR4 routing (P2.4): previous frame's predicted display time; the delta
    // to the current prediction advances the simulation clock. 0 = invalid
    // (pacing inactive last frame; next valid prediction re-seeds).
    double                                                   m_last_predicted_display_time{0.0};
    // Per-frame data crossing the frame phases (see Frame_packet).
    Frame_packet                                             m_frame_packet{};
    // The handoff to the render phase; created at the end of the
    // constructor, stopped first thing in ~Editor after the watchdog.
    Render_thread_mode                                       m_render_thread_mode{Render_thread_mode::inline_main_thread};
    bool                                                     m_imgui_overlap{false}; // the ImGui phase runs after the kick (select_imgui_overlap)
    bool                                                     m_frame_pipelining{false}; // the kick joins the previous frame; M1 does not (select_frame_pipelining)
    std::unique_ptr<Render_thread>                           m_render_thread;
    uint64_t                                                 m_render_phase_frame_number{0};   // R: the frame being rendered (the draw data wait)
    double                                                   m_render_phase_draw_data_wait_us{0.0}; // R: this frame's draw data wait, into Frame_result
    bool                                                     m_sky_luts_ready{false}; // main: Frame_result::sky_luts_ready of the last joined frame
    // Tier S slop-servo fallback (P4.1/P4.2): pre-input sleep servoed from
    // measured backpressure blocking. Period corrected each tick from the
    // swapchain timing query.
    erhe::frame_pacing::Slop_servo_pacer                     m_slop_servo{erhe::frame_pacing::Slop_servo_tunables{}, 1.0 / 60.0};
    bool                                                     m_frame_started{false}; // start_frame() ran for the frame the next frame() runs
    std::chrono::steady_clock::time_point                    m_frame_start_begin{};   // start_frame() of the frame frame() runs; the phase log's full tick starts here
    double                                                   m_slop_servo_last_tick_time{0.0};
    std::unique_ptr<erhe::imgui::Imgui_renderer            > m_imgui_renderer;
    std::unique_ptr<erhe::renderer::Debug_renderer         > m_debug_renderer;
    std::unique_ptr<erhe::scene_renderer::Program_interface> m_program_interface;
    std::unique_ptr<erhe::rendergraph::Rendergraph         > m_rendergraph;
    std::unique_ptr<erhe::renderer::Text_renderer          > m_text_renderer;
    std::unique_ptr<erhe::scene_renderer::Shader_variant_cache>       m_shader_variant_cache;
    std::unique_ptr<Material_set_factory                  >           m_material_set_factory;
    std::unique_ptr<Programs                              >           m_programs;
    // Declared before the two renderers that hold a reference to it.
    std::unique_ptr<erhe::scene_renderer::Scene_pass_resources>       m_scene_pass_resources;
    std::unique_ptr<erhe::scene_renderer::Forward_renderer>           m_forward_renderer;
    std::unique_ptr<erhe::scene_renderer::Draw_list_renderer>         m_draw_list_renderer;
    std::unique_ptr<erhe::scene_renderer::Shadow_renderer >           m_shadow_renderer;
    std::unique_ptr<erhe::scene_renderer::Gpu_cull_classifier>        m_gpu_cull_classifier;
    std::unique_ptr<erhe::scene_renderer::Light_cluster_gpu_binner>   m_light_cluster_gpu_binner;
    std::unique_ptr<erhe::scene_renderer::Cull_frame_registry>        m_cull_frame_registry;
    std::unique_ptr<erhe::scene_renderer::Texel_renderer >            m_texel_renderer;
    std::unique_ptr<erhe::scene_renderer::Mesh_memory     >           m_mesh_memory;
    std::unique_ptr<erhe::scene_renderer::Content_wide_line_interface> m_content_wide_line_interface;
    std::unique_ptr<erhe::scene_renderer::Content_wide_line_renderer>  m_content_wide_line_renderer;
    std::unique_ptr<erhe::graphics::Shader_stages>                    m_content_wide_line_compute_stages;
    std::unique_ptr<erhe::graphics::Shader_stages>                    m_content_wide_line_compute_stages_skinned;
    std::unique_ptr<erhe::graphics::Shader_stages>                    m_content_wide_line_graphics_stages;
    std::unique_ptr<erhe::graphics::Shader_stages>                    m_content_wide_line_multiview_graphics_stages;

    std::unique_ptr<erhe::imgui::Imgui_windows>              m_imgui_windows;
    std::unique_ptr<App_scenes             >                 m_app_scenes;
    std::unique_ptr<App_windows            >                 m_app_windows;

    std::unique_ptr<Asset_browser                   >        m_asset_browser;
    std::unique_ptr<Icon_set                        >        m_icon_set;
    std::unique_ptr<Post_processing                 >        m_post_processing;
    std::unique_ptr<Sky_renderer                    >        m_sky_renderer;
    std::unique_ptr<Ray_trace_renderer              >        m_ray_trace_renderer;
    std::unique_ptr<Ddgi_renderer                   >        m_ddgi_renderer;
    std::unique_ptr<Radiance_cascades_renderer      >        m_radiance_cascades_renderer;
    std::unique_ptr<Lightmap_baker                  >        m_lightmap_baker;
    std::unique_ptr<Lightmap_report                 >        m_lightmap_report;
    std::unique_ptr<Lightmap_streamer               >        m_lightmap_streamer;
    std::unique_ptr<Lightmap_partitioner            >        m_lightmap_partitioner;
    bool                                                     m_lightmap_offline_was_active{false};
    // The interactive baker held the lightmap binding last frame; a hand-
    // off to the streamer re-pushes the streamer's region mappings.
    bool                                                     m_lightmap_baker_owned_binding{false};
    std::unique_ptr<Id_renderer                     >        m_id_renderer;
    std::unique_ptr<Composer_window                 >        m_composer_window;
    std::unique_ptr<Selection_window                >        m_selection_window;
    std::unique_ptr<Settings_window                 >        m_settings_window;
    std::unique_ptr<Scene_views                     >        m_viewport_scene_views;
    std::unique_ptr<App_rendering                   >        m_app_rendering;
    std::unique_ptr<Selection                       >        m_selection;
    std::unique_ptr<Mesh_component_selection        >        m_mesh_component_selection;
    std::unique_ptr<Bone_visualization              >        m_bone_visualization;
    std::unique_ptr<Weight_display                  >        m_weight_display;
    std::unique_ptr<Operation_stack                 >        m_operation_stack;
    std::unique_ptr<Scene_commands                  >        m_scene_commands;
    std::unique_ptr<Clipboard_window                >        m_clipboard_window;
    std::unique_ptr<Commands_window                 >        m_commands_window;
    std::unique_ptr<Input_bindings_window           >        m_input_bindings_window;
    std::unique_ptr<Geometry_graph_window           >        m_geometry_graph_window;
    std::unique_ptr<Graph_editor_palette_window     >        m_geometry_graph_palette_window;
    std::unique_ptr<Texture_graph_window            >        m_texture_graph_window;
    std::unique_ptr<Graph_editor_palette_window     >        m_texture_graph_palette_window;
    std::unique_ptr<Graph_window                    >        m_graph_window;
    std::unique_ptr<Node_properties_window          >        m_node_properties_window;
    std::unique_ptr<Gradient_editor                 >        m_gradient_editor;
    std::unique_ptr<Icon_browser                    >        m_icon_browser;
    std::unique_ptr<Thumbnails                      >        m_thumbnails;
    std::unique_ptr<Texture_file_loader             >        m_texture_file_loader;
    std::unique_ptr<Sheet_window                    >        m_sheet_window;
    std::unique_ptr<Layers_window                   >        m_layers_window;
    std::unique_ptr<Network_window                  >        m_network_window;
    std::unique_ptr<Operations                      >        m_operations;
    std::unique_ptr<Physics_window                  >        m_physics_window;
    std::unique_ptr<Post_processing_window          >        m_post_processing_window;
    std::unique_ptr<Ray_trace_window                >        m_ray_trace_window;
    std::unique_ptr<Ddgi_window                     >        m_ddgi_window;
    std::unique_ptr<Radiance_cascades_window        >        m_radiance_cascades_window;
    std::unique_ptr<Properties                      >        m_properties;
    std::unique_ptr<Geometry_spreadsheet_window     >        m_geometry_spreadsheet_window;
    std::unique_ptr<Editor_windows                  >        m_editor_windows;
    std::unique_ptr<Frame_pacing_window             >        m_frame_pacing_window;
    std::unique_ptr<Controller_inputs_window        >        m_controller_inputs_window;
    std::unique_ptr<Lightmap_window                 >        m_lightmap_window;
    std::unique_ptr<Lightmap_texture_window         >        m_lightmap_texture_window;
    std::unique_ptr<Rendergraph_window              >        m_rendergraph_window;
    std::unique_ptr<Animation_player                >        m_animation_player;
    std::unique_ptr<Animation_window                >        m_animation_window;
    std::unique_ptr<Tool_properties_window          >        m_tool_properties_window;
    std::unique_ptr<erhe::imgui::Logs               >        m_logs;
    std::unique_ptr<erhe::imgui::Log_settings_window>        m_log_settings_window;
    std::unique_ptr<erhe::imgui::Tail_log_window    >        m_tail_log_window;
    std::unique_ptr<erhe::imgui::Frame_log_window   >        m_frame_log_window;
    std::unique_ptr<erhe::imgui::Performance_window >        m_performance_window;
    // Declared after m_performance_window: destroyed first, so the tracker's
    // plots unregister while the window is still alive.
    std::unique_ptr<Transform_update_stats_tracker  >        m_transform_update_stats_tracker;
    std::unique_ptr<erhe::imgui::Pipelines          >        m_pipelines;

    std::unique_ptr<Tools            >                       m_tools;
    std::unique_ptr<Scene_builder    >                       m_scene_builder;
    std::unique_ptr<Prefab_library   >                       m_prefab_library;
    // Declared after Scene_builder / Prefab_library: destroyed before them,
    // so the manager releases its builtin palette brushes while
    // Scene_builder still co-owns them.
    std::unique_ptr<Asset_manager    >                       m_asset_manager;
    std::unique_ptr<Fly_camera_tool  >                       m_fly_camera_tool;
    std::unique_ptr<Navigation_gizmo_tool>                   m_navigation_gizmo_tool;
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
    std::unique_ptr<Inventory_window    >                    m_inventory_window;
    std::unique_ptr<Hover_tool          >                    m_hover_tool;
    std::unique_ptr<Brdf_slice          >                    m_brdf_slice;
    std::unique_ptr<Depth_visualization_window>              m_debug_view_window;
    std::unique_ptr<Material_preview    >                    m_material_preview;
    std::unique_ptr<Brush_preview       >                    m_brush_preview;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    ////Theremin                                m_theremin;
#endif

    std::unique_ptr<Brush_tool         >                     m_brush_tool;
    std::unique_ptr<Create             >                     m_create;
    std::unique_ptr<Grid_tool          >                     m_grid_tool;
    std::unique_ptr<Material_paint_tool>                     m_material_paint_tool;
    std::unique_ptr<Mesh_component_selection_tool>           m_mesh_component_selection_tool;
    std::unique_ptr<Lattice_tool>                            m_lattice_tool;
    std::unique_ptr<Draw_mode_renderer>                      m_draw_mode_renderer;
    std::unique_ptr<Paint_tool         >                     m_paint_tool;
    std::unique_ptr<Weight_paint_tool  >                     m_weight_paint_tool;
    std::unique_ptr<Physics_tool       >                     m_physics_tool;
    std::unique_ptr<Selection_tool     >                     m_selection_tool;

    // MCP server (exposes editor commands over HTTP)
    std::unique_ptr<Mcp_server         >                     m_mcp_server;
};

void run_editor(const std::string& startup_commands_path, const std::string& startup_scene_path, const bool no_startup_scene, const bool force_post_processing_off, const bool fix_gltf_spot_lights, const std::string& benchmark_path, const Headset_culling headset_culling)
{
//#if defined(ERHE_PROFILE_LIBRARY_TRACY) && TRACY_ENABLE
//    while (!TracyIsConnected) {
//        std::this_thread::sleep_for(std::chrono::milliseconds(50));
//    }
//#endif

    ERHE_PROFILE_FUNCTION();

    // Before any item exists: the editor's flag bits and type indices
    // (editor_item_bits.hpp).
    register_editor_item_bits();

#if defined(ERHE_PROFILE_LIBRARY_NVTX)
    nvtxInitialize(nullptr);
#endif
    // Workaround for
    // https://intellij-support.jetbrains.com/hc/en-us/community/posts/27792220824466-CMake-C-git-project-How-to-share-working-directory-in-git
    erhe::file::ensure_working_directory_contains("config/editor/editor_settings.json");

    // initialize_log_sinks creates "logs/log.txt" relative to cwd; must
    // run AFTER ensure_working_directory_contains so the file lands in
    // the same logs/ directory used by graphics_log (logs/vulkan.txt).
    erhe::log::redirect_stderr_to_file("logs/stderr.txt");
    {
        ERHE_PROFILE_SCOPE("erhe::log::initialize_log_sinks()");
        erhe::log::initialize_log_sinks();
    }

    {
        std::optional<std::string> contents = erhe::file::read("logging config", "config/editor/logging.json");
        if (contents.has_value()) {
            erhe::log::load_log_configuration(contents.value());
        }
    }

    {
        ERHE_PROFILE_SCOPE("initialize logging");
#if defined(ERHE_GRAPHICS_API_OPENGL)
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
        erhe::property::initialize_logging();
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
#if defined(ERHE_USD_LIBRARY_LIGHTUSD)
        erhe::usd::initialize_logging();
#endif
#if defined(ERHE_XR_LIBRARY_OPENXR)
        erhe::xr::initialize_logging();
#endif
        editor::initialize_logging();
        // The build identity, first thing in the log: the project version
        // and the source tree's git describe (erhe::version, stamped at
        // every build) beside the dependency commits CMake captured.
        log_startup->info("{}; geogram {}; bvh {}", erhe::version::get_description(), ERHE_GEOGRAM_GIT_COMMIT, ERHE_BVH_GIT_COMMIT);
    }

    // Descriptor-driven property registrations (texture graph node
    // parameters): the descriptors are function-local statics, so this
    // cannot ride C++ static initialization; here we are still
    // single-threaded, which is the registry's write-window invariant
    // (property-system D3 / R12).
    register_texture_graph_properties();

    erhe::time::sleep_initialize();
    erhe::physics::initialize_physics_system();

    {
        ERHE_PROFILE_SCOPE("initialize geogram");
        GEO::initialize(GEO::GEOGRAM_INSTALL_NONE);
        // Under a debugger, make Geogram asserts break into it at the failure site
        // (ASSERT_BREAKPOINT -> __debugbreak) instead of throwing.
        // geo_assert in parallel code (e.g. parallel_delaunay_3d, reached via chamfer's
        // convex hull) fires on Geogram's own worker threads, where the throw escapes
        // every editor-side catch (including the async mesh-operation worker boundary)
        // and just calls std::terminate - so the process exits with no chance to break
        // and inspect. Outside a debugger set ASSERT_THROW EXPLICITLY: Geogram's
        // default is ASSERT_ABORT in GEO_DEBUG builds, and on Windows geo_abort()
        // blocks on getchar() ("press any key to continue") - in an unattended
        // headless run a Geogram assert then wedges the process forever instead of
        // failing loudly (observed live: brush-preview convex hull tripping
        // CellStatusArray::resize's !Process::is_running_threads() precondition).
        // ASSERT_THROW lets the editor-side catch boundaries skip the degenerate
        // operation and keep the run going.
        if (is_debugger_present()) {
            GEO::set_assert_mode(GEO::ASSERT_BREAKPOINT);
        } else {
            GEO::set_assert_mode(GEO::ASSERT_THROW);
        }
        GEO::CmdLine::import_arg_group("algo");
        // Required by GEO::remesh_smooth() (Remesh / Anisotropic Remesh operations),
        // which reads remesh:multi_nerve and remesh:RVC_centroids; querying an
        // undeclared arg aborts via geo_assert_arg_type.
        GEO::CmdLine::import_arg_group("remesh");
        GEO::CmdLine::set_arg("sys:multithread", "true");
        // GEO::remesh_smooth() defaults remesh:multi_nerve=true; we keep it on.
        // multi_nerve was previously disabled because remeshing e.g. a
        // uv_sphere(48,24) to 2000 points produced an empty output mesh and an
        // abort in mesh_adjust_surface (OpenNL nl_assert(NL_NB_VARIABLES > 0)).
        // Root cause: the editor was handing remesh_smooth() an input surface
        // with no facet adjacency (every edge a boundary), because
        // Mesh::facets::triangulate() rebuilds the facets and drops adjacency.
        // The multinerve RDT path computes per-seed restricted-Voronoi connected
        // components, so a disconnected surface explodes into a fragmented
        // triangle soup that mesh_postprocess_RDT() then strips to nothing. The
        // fix is in erhe::geometry::operation::remesh (input.facets.connect()
        // after triangulate); with a connected input, multi_nerve works
        // correctly. It was never a Geogram bug.
        GEO::CmdLine::set_arg("remesh:multi_nerve", "true");
        erhe::geometry::register_geogram_attribute_types();
        // Route Geogram's GEO::Logger output into erhe::geometry::log_geogram.
        // Requires the Geogram Logger singleton (created by GEO::initialize()
        // above) and log_geogram (created by erhe::geometry::initialize_logging()).
        erhe::geometry::register_geogram_logger();
        // Route Geogram progress (ProgressTask begin/progress/end) into a snapshot
        // the status bar reads. GEO::initialize() above ran Progress::initialize(),
        // which installed the default progress client we replace here.
        erhe::geometry::register_geogram_progress();
    }

    {
        ERHE_PROFILE_SCOPE("Construct and run Editor");

        //for (std::size_t i = 0; i < 20; ++i) {
        //    log_startup->info("Stress test iteration {}", i);
        //    Editor editor{};
        //    editor.tick();
        //}

        Editor editor{startup_commands_path, startup_scene_path, no_startup_scene, force_post_processing_off, fix_gltf_spot_lights, benchmark_path, headset_culling};
        editor.run();
    }

    // Detach the Geogram -> log_geogram forwarding client while log_geogram is
    // still alive, so no late Geogram message can reach an already-destroyed
    // logger during static destruction.
    erhe::geometry::unregister_geogram_logger();
    erhe::geometry::unregister_geogram_progress();
}

}
