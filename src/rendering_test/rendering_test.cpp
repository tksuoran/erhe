#include "rendering_test.hpp"
#include "rendering_test_app.hpp"
#include "rendering_test_log.hpp"
#include "generated/rendering_test/rendering_test_settings_serialization.hpp"

#include "erhe_codegen/config_io.hpp"
#include "erhe_dataformat/dataformat_log.hpp"
#include "erhe_file/file.hpp"
#include "erhe_file/file_log.hpp"
#include "erhe_gltf/gltf_log.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/generated/graphics_config_serialization.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/swapchain.hpp"

#include <span>
#include "erhe_item/item_log.hpp"
#include "erhe_log/log.hpp"
#include "erhe_math/math_log.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_scene_renderer/generated/mesh_memory_config.hpp"
#include "erhe_scene_renderer/generated/mesh_memory_config_serialization.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_ui/ui_log.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_window/window_log.hpp"

#include <SDL3/SDL.h>

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_gl/gl_log.hpp"
#endif

namespace rendering_test {

Rendering_test::Rendering_test(std::string_view config_path)
    : m_settings{ erhe::codegen::load_config<Rendering_test_settings>(config_path) }
    , m_graphics_config{ erhe::codegen::load_config<Graphics_config>("config/rendering_test/erhe_graphics.json") }
    , m_window{
        erhe::window::Window_configuration{
            .use_depth                = true,
            .size                     = glm::ivec2{1280, 720},
            .title                    = "erhe rendering test",
            .initialize_frame_capture = m_graphics_config.renderdoc_capture_support
        }
    }
    , m_graphics_device{
        erhe::graphics::Surface_create_info{
            .context_window            = &m_window,
            .prefer_low_bandwidth      = false,
            .prefer_high_dynamic_range = false
        },
        m_graphics_config,
        [](erhe::graphics::Message_severity severity, const std::string& error_message, const std::string& callstack) {
            if (
                (severity == erhe::graphics::Message_severity::warning) ||
                (severity == erhe::graphics::Message_severity::error)
            )
            {
                std::string clipboard_text = error_message + "\n=== Callstack ===\n" + callstack;
                SDL_SetClipboardText(clipboard_text.c_str());
                ERHE_FATAL("Device error (copied to clipboard): %s", error_message.c_str());
            }
        }
    }
    , m_error_callback_set{(
        m_graphics_device.set_shader_error_callback(
            [](const std::string& error_log, const std::string& shader_source, const std::string& callstack) {
                std::string clipboard_text = "=== Shader Error ===\n" + error_log + "\n=== Shader Source ===\n" + shader_source + "\n=== Callstack ===\n" + callstack;
                SDL_SetClipboardText(clipboard_text.c_str());
                ERHE_FATAL("Shader compilation/linking failed (error and source copied to clipboard)");
            }
        ),
    true)}
    , m_init_command_buffer{[this]() -> erhe::graphics::Command_buffer& {
        const bool init_wait_frame_ok = m_graphics_device.wait_frame();
        ERHE_VERIFY(init_wait_frame_ok);
        erhe::graphics::Command_buffer& cb = m_graphics_device.get_command_buffer(0);
        cb.begin();
        return cb;
    }()}
    , m_image_transfer   {m_graphics_device, m_init_command_buffer}
    , m_vertex_format{
        {
            0,
            {
                { erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::position, 0 },
            }
        },
        {
            1,
            {
                { erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::normal,    0 },
                { erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::tangent,   0 },
                { erhe::dataformat::Format::format_32_vec2_float, erhe::dataformat::Vertex_attribute_usage::tex_coord, 0 },
                { erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::color,     0 },
            }
        }
    }
    , m_mesh_memory      {erhe::codegen::load_config<Mesh_memory_config>("config/rendering_test/mesh_memory.json"), m_graphics_device, m_vertex_format}
    , m_program_interface_config{
        .shader_paths = {
            std::filesystem::path{"res"} / std::filesystem::path{"shaders"},
            std::filesystem::path{"res"} / std::filesystem::path{"rendering_test"} / std::filesystem::path{"shaders"}
        }
    }
    , m_program_interface{m_graphics_device, m_mesh_memory.vertex_format, m_program_interface_config}
    , m_programs         {m_graphics_device, m_program_interface}
    , m_scene            {"rendering test scene", nullptr}
{
    m_window.set_title(
        erhe::window::format_window_title("erhe rendering test", m_graphics_device.get_info().api_info)
    );

    print_conventions();

    // Init-time GPU work batches into m_init_command_buffer (opened in
    // the member init list). Submit + wait happens at the bottom of
    // the constructor body before the run loop starts.
    create_test_scene(m_init_command_buffer);
    m_forward_renderer = std::make_unique<erhe::scene_renderer::Forward_renderer>(
        m_graphics_device, m_init_command_buffer, m_program_interface
    );

    m_last_window_width  = m_window.get_width();
    m_last_window_height = m_window.get_height();

#if !defined(ERHE_GRAPHICS_LIBRARY_METAL)
    m_window.register_redraw_callback(
        [this](){
            if (!m_first_frame_rendered || m_in_tick.load()) {
                return;
            }
            if (
                (m_last_window_width  != m_window.get_width ()) ||
                (m_last_window_height != m_window.get_height())
            ) {
                m_request_resize_pending.store(true);
                m_last_window_width  = m_window.get_width();
                m_last_window_height = m_window.get_height();
            }
        }
    );
#endif

    make_render_pipeline_states();
    make_stencil_pipelines();
    make_content_wide_line_renderer();
    make_stencil_wide_line_pipeline();
    make_minimal_compute_triangle();
    make_quad_pipeline();
    make_multi_texture_pipeline();
    make_separate_samplers_pipeline();
    make_depth_visualize_pipeline();
    create_test_textures(m_init_command_buffer);

    // Close + submit the init cb and block until the GPU is done so
    // every recorded upload / clear / pipeline barrier is visible
    // before the main render loop starts.
    m_init_command_buffer.end();
    erhe::graphics::Command_buffer* init_cbs[] = { &m_init_command_buffer };
    m_graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{init_cbs});
    m_graphics_device.wait_idle();

    const bool init_end_frame_ok = m_graphics_device.end_frame();
    ERHE_VERIFY(init_end_frame_ok);
}

auto Rendering_test::on_window_resize_event(const erhe::window::Input_event& input_event) -> bool
{
    m_window_resize_event = input_event;
    return true;
}

auto Rendering_test::on_window_close_event(const erhe::window::Input_event&) -> bool
{
    m_close_requested = true;
    return true;
}

auto Rendering_test::on_key_event(const erhe::window::Input_event& input_event) -> bool
{
    if (input_event.u.key_event.pressed && (input_event.u.key_event.keycode == erhe::window::Key_escape)) {
        m_close_requested = true;
        return true;
    }
    return false;
}

void Rendering_test::run()
{
    while (!m_close_requested) {
        const bool wait_ok = m_graphics_device.wait_frame();
        ERHE_VERIFY(wait_ok);

        m_window.poll_events();

        auto& input_events = m_window.get_input_events();
        for (erhe::window::Input_event& input_event : input_events) {
            static_cast<void>(this->dispatch_input_event(input_event));
        }

        if (m_window_resize_event.has_value()) {
            m_request_resize_pending.store(true);
            m_window_resize_event.reset();
            m_last_window_width  = m_window.get_width();
            m_last_window_height = m_window.get_height();
        }

        const erhe::graphics::Frame_begin_info frame_begin_info{
            .resize_width   = static_cast<uint32_t>(m_last_window_width),
            .resize_height  = static_cast<uint32_t>(m_last_window_height),
            .request_resize = m_request_resize_pending.load()
        };
        m_request_resize_pending.store(false);

        // hello_swap-style cb-driven tick.
        erhe::graphics::Command_buffer& command_buffer = m_graphics_device.get_command_buffer(0);
        erhe::graphics::Frame_state frame_state{};
        const bool wait_swap_ok  = command_buffer.wait_for_swapchain(frame_state);
        const bool should_render = wait_swap_ok && command_buffer.begin_swapchain(frame_begin_info, frame_state);

        if (should_render) {
            command_buffer.begin();
            m_in_tick.store(true);
            tick(command_buffer);
            m_in_tick.store(false);
            m_first_frame_rendered = true;
            command_buffer.end();

            const erhe::graphics::Frame_end_info frame_end_info{
                .requested_display_time = 0
            };
            command_buffer.end_swapchain(frame_end_info);

            erhe::graphics::Command_buffer* cbs[] = { &command_buffer };
            m_graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{cbs});
        }

        const bool end_frame_ok = m_graphics_device.end_frame();
        ERHE_VERIFY(end_frame_ok);
    }
}

// Dispatch a subtest by name into the given viewport within the
// currently-active swapchain render pass. Subtest names and their
// meanings are documented at the top of
// definitions/rendering_test_settings.py.
void Rendering_test::dispatch_subtest(
    std::string_view                                        name,
    erhe::graphics::Command_buffer&                         command_buffer,
    erhe::graphics::Render_command_encoder&                 encoder,
    const erhe::math::Viewport&                             viewport,
    const std::vector<std::shared_ptr<erhe::scene::Light>>& lights,
    const std::vector<std::shared_ptr<erhe::scene::Mesh>>&  meshes
)
{
    static_cast<void>(command_buffer);
    if (name.empty()) {
        return;
    }
    if (name == "cube") {
        // Forward phong cube only, no edge lines.
        render_scene(encoder, viewport, lights, meshes, m_swapchain_render_pass.get());
    } else if (name == "cube_with_edge_lines") {
        // Forward phong cube plus its wide edge lines, always rendered
        // via content_wide_line_renderer. The test stays backend-agnostic
        // and never calls a geometry-shader path directly.
        render_scene(encoder, viewport, lights, meshes, m_swapchain_render_pass.get());
        if (m_content_wide_line_renderer && m_content_wide_line_renderer->is_enabled() && m_compute_edge_lines_pipeline_state) {
            m_content_wide_line_renderer->render(encoder, *m_compute_edge_lines_pipeline_state, *m_swapchain_render_pass.get());
        }
    } else if (name == "cube_with_edge_lines_stencil") {
        // Like cube_with_edge_lines, but uses the STENCIL wide-line
        // pipeline (m_compute_edge_lines_stencil_pipeline_state, which
        // tests stencil != 1) instead of the non-stencil one. No
        // separate stencil-write step; the swapchain's stencil is
        // cleared to 0 so the test always passes and edges are fully
        // visible.
        //
        // Pair with cube_with_edge_lines in a grid to test whether
        // switching between two different wide-line VkPipelines is
        // sufficient to trigger the bug even without any stencil-write
        // step between them.
        render_scene(encoder, viewport, lights, meshes, m_swapchain_render_pass.get());
        if (m_content_wide_line_renderer && m_content_wide_line_renderer->is_enabled() && m_compute_edge_lines_stencil_pipeline_state) {
            m_content_wide_line_renderer->render(encoder, *m_compute_edge_lines_stencil_pipeline_state, *m_swapchain_render_pass.get());
        }
    } else if (name == "cube_edge_lines_only") {
        // Minimal reproducer: one content_wide_line_renderer draw of the
        // cube's edges (group 0), no polygon-fill cube, no stencil masking.
        // Uses the same pipeline as sphere_edge_lines_only so an "X then Y"
        // grid does not introduce any VkPipeline switch between the two
        // wide-line draws.
        //
        // render_scene() is still called with an empty mesh list so that
        // forward_renderer binds its descriptor sets (camera UBO etc.),
        // which content_wide_line_renderer::render() depends on. No draws
        // are issued by forward_renderer when the mesh span is empty.
        if (m_content_wide_line_renderer && m_content_wide_line_renderer->is_enabled() && m_compute_edge_lines_pipeline_state) {
            const std::vector<std::shared_ptr<erhe::scene::Mesh>> empty_meshes;
            render_scene(encoder, viewport, lights, empty_meshes, m_swapchain_render_pass.get());
            m_content_wide_line_renderer->render(encoder, *m_compute_edge_lines_pipeline_state, *m_swapchain_render_pass.get(), /*group=*/0);
        }
    } else if (name == "sphere_edge_lines_only") {
        // Same as cube_edge_lines_only but draws the sphere (group 1).
        // Shares the non-stencil pipeline so an "X then Y" grid that pairs
        // cube_edge_lines_only and sphere_edge_lines_only rebinds the same
        // VkPipeline twice - isolating "two wide-line draws" from
        // "two different VkPipelines".
        if (m_content_wide_line_renderer && m_content_wide_line_renderer->is_enabled() && m_compute_edge_lines_pipeline_state) {
            const std::vector<std::shared_ptr<erhe::scene::Mesh>> empty_meshes;
            render_scene(encoder, viewport, lights, empty_meshes, m_swapchain_render_pass.get());
            m_content_wide_line_renderer->render(encoder, *m_compute_edge_lines_pipeline_state, *m_swapchain_render_pass.get(), /*group=*/1);
        }
    } else if (name == "cube_edge_lines_stencil_masked") {
        // Same as cube_edge_lines_only, but with stencil masking: the
        // stencil cube is first drawn with "stencil always replace 1" so
        // the cube silhouette in this tile gets stencil=1; the wide-line
        // draw then uses the stencil-test pipeline (test != 1) so edges
        // inside the cube silhouette are masked out. Uses group=0 (cube
        // edges). Shares its wide-line pipeline with sphere_edge_lines_
        // stencil_masked so a grid pairing the two does NOT introduce a
        // wide-line-pipeline switch - only the stencil-write pipeline and
        // the wide-line-stencil pipeline appear in an A->B->A->B sequence.
        if (m_content_wide_line_renderer && m_content_wide_line_renderer->is_enabled() &&
            m_compute_edge_lines_stencil_pipeline_state && m_stencil_cube)
        {
            const std::vector<std::shared_ptr<erhe::scene::Mesh>> cube_meshes{m_stencil_cube};
            encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
            encoder.set_scissor_rect (viewport.x, viewport.y, viewport.width, viewport.height);
            const auto& conventions = m_graphics_device.get_info().coordinate_conventions;
            m_forward_renderer->render(
                erhe::scene_renderer::Forward_renderer::Render_parameters{
                    .render_encoder         = encoder,
                    .index_type             = erhe::dataformat::Format::format_32_scalar_uint,
                    .index_buffer           = &m_mesh_memory.index_buffer,
                    .vertex_buffer0         = &m_mesh_memory.vertex_buffer_position,
                    .vertex_buffer1         = &m_mesh_memory.vertex_buffer_non_position,
                    .ambient_light          = glm::vec3{0.3f, 0.3f, 0.3f},
                    .camera                 = m_camera.get(),
                    .light_projections      = &m_light_projections,
                    .lights                 = lights,
                    .skins                  = {},
                    .materials              = m_materials,
                    .mesh_spans             = { cube_meshes },
                    .render_pipeline_states = m_stencil_write_1_pipeline_states,
                    .render_pass            = m_swapchain_render_pass.get(),
                    .primitive_mode         = erhe::primitive::Primitive_mode::polygon_fill,
                    .primitive_settings     = erhe::scene_renderer::Primitive_interface_settings{},
                    .viewport               = viewport,
                    .filter                 = erhe::Item_filter{},
                    .override_shader_stages = m_stencil_cyan_shader_stages.get(),
                    .debug_label            = "cube_edge_lines_stencil_masked: stencil cube",
                    .reverse_depth          = true,
                    .depth_range            = conventions.native_depth_range,
                    .conventions            = conventions
                }
            );
            m_content_wide_line_renderer->render(encoder, *m_compute_edge_lines_stencil_pipeline_state, *m_swapchain_render_pass.get(), /*group=*/0);
        }
    } else if (name == "sphere_edge_lines_stencil_masked") {
        // Same as cube_edge_lines_stencil_masked but draws group=1 (sphere
        // edges) for the wide-line step. Wide-line pipeline is identical.
        if (m_content_wide_line_renderer && m_content_wide_line_renderer->is_enabled() &&
            m_compute_edge_lines_stencil_pipeline_state && m_stencil_cube)
        {
            const std::vector<std::shared_ptr<erhe::scene::Mesh>> cube_meshes{m_stencil_cube};
            encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
            encoder.set_scissor_rect (viewport.x, viewport.y, viewport.width, viewport.height);
            const auto& conventions = m_graphics_device.get_info().coordinate_conventions;
            m_forward_renderer->render(
                erhe::scene_renderer::Forward_renderer::Render_parameters{
                    .render_encoder         = encoder,
                    .index_type             = erhe::dataformat::Format::format_32_scalar_uint,
                    .index_buffer           = &m_mesh_memory.index_buffer,
                    .vertex_buffer0         = &m_mesh_memory.vertex_buffer_position,
                    .vertex_buffer1         = &m_mesh_memory.vertex_buffer_non_position,
                    .ambient_light          = glm::vec3{0.3f, 0.3f, 0.3f},
                    .camera                 = m_camera.get(),
                    .light_projections      = &m_light_projections,
                    .lights                 = lights,
                    .skins                  = {},
                    .materials              = m_materials,
                    .mesh_spans             = { cube_meshes },
                    .render_pipeline_states = m_stencil_write_1_pipeline_states,
                    .render_pass            = m_swapchain_render_pass.get(),
                    .primitive_mode         = erhe::primitive::Primitive_mode::polygon_fill,
                    .primitive_settings     = erhe::scene_renderer::Primitive_interface_settings{},
                    .viewport               = viewport,
                    .filter                 = erhe::Item_filter{},
                    .override_shader_stages = m_stencil_cyan_shader_stages.get(),
                    .debug_label            = "sphere_edge_lines_stencil_masked: stencil cube",
                    .reverse_depth          = true,
                    .depth_range            = conventions.native_depth_range,
                    .conventions            = conventions
                }
            );
            m_content_wide_line_renderer->render(encoder, *m_compute_edge_lines_stencil_pipeline_state, *m_swapchain_render_pass.get(), /*group=*/1);
        }
    } else if ((name == "minimal_compute_A") ||
               (name == "minimal_compute_B") ||
               (name == "minimal_compute_C") ||
               (name == "minimal_compute_D"))
    {
        // Smallest non-wide-line-renderer repro. Compute shader has
        // already produced one triangle's vertices into a ring-buffer
        // range (see dispatch_minimal_compute_triangle in tick()).
        // This just switches to the graphics bind_group_layout, binds
        // the chosen pipeline, and draws.
        //
        //   A: stencil_test=false, red
        //   B: stencil_test=true, not_equal 1, green
        //   C: stencil_test=true, always,      blue (stencil_test=true
        //      but function=always, so always draws)
        //   D: stencil_test=true, equal 1,     yellow (never draws
        //      when stencil has not been written)
        //
        // Pairing (C, D) in a 2-cell grid is the targeted repro for a
        // KosmicKrisp pipeline-switch failure that keeps
        // stencil_test_enable set but drops the stencil function/
        // reference update. Expected correct result: blue triangle in
        // tile 0, empty tile 1. If the bug fires: yellow triangle
        // also appears in tile 1.
        erhe::graphics::Lazy_render_pipeline* pipe = nullptr;
        if (name == "minimal_compute_A") {
            pipe = m_minimal_pipeline_A_red.get();
        } else if (name == "minimal_compute_B") {
            pipe = m_minimal_pipeline_B_green.get();
        } else if (name == "minimal_compute_C") {
            pipe = m_minimal_pipeline_C_stencil_always_blue.get();
        } else {
            pipe = m_minimal_pipeline_D_stencil_eq_1_yellow.get();
        }
        if (pipe != nullptr) {
            draw_minimal_compute_triangle(encoder, viewport, *pipe);
        }
    } else if (name == "C_then_stw1_then_D") {
        // Mirrors the full Phase B5 failing pattern inside one subtest:
        //   1. Minimal pipe C (stencil_test=true, always, blue) draws
        //      the compute triangle. First bind of the minimal graphics
        //      bind_group_layout.
        //   2. forward_renderer(stw1, stencil cube). Binds the
        //      program_interface bind_group_layout and stw1 (stencil_
        //      test=true, always, replace 1).
        //   3. Minimal pipe D (stencil_test=true, equal 1, yellow) draws
        //      the compute triangle. Second bind of the minimal graphics
        //      bind_group_layout.
        //
        // Layout interleave: minimal -> program_interface -> minimal.
        // Pipeline switches on the minimal layout: C -> D. Both have
        // stencil_test_enable=true but differ in stencil function.
        // On KosmicKrisp this is the structural twin of the original
        // content_wide_line_renderer pipeline-switch regression.
        //
        // Expected correct: blue triangle visible (from C); gray cube
        // visible (from stw1); yellow ONLY inside the cube silhouette
        // (D's equal-1 passes where stw1 wrote stencil=1).
        // Expected bug: yellow fills the whole triangle (D inherits a
        // previous pipeline's "always" stencil function).
        if (!m_stencil_cube || !m_minimal_pipeline_C_stencil_always_blue ||
            !m_minimal_pipeline_D_stencil_eq_1_yellow) {
            return;
        }
        // 1) minimal pipe C
        draw_minimal_compute_triangle(encoder, viewport, *m_minimal_pipeline_C_stencil_always_blue);
        // 2) forward_renderer(stw1)
        const std::vector<std::shared_ptr<erhe::scene::Mesh>> cube_meshes{m_stencil_cube};
        const auto& conventions = m_graphics_device.get_info().coordinate_conventions;
        m_forward_renderer->render(
            erhe::scene_renderer::Forward_renderer::Render_parameters{
                .render_encoder         = encoder,
                .index_type             = erhe::dataformat::Format::format_32_scalar_uint,
                .index_buffer           = &m_mesh_memory.index_buffer,
                .vertex_buffer0         = &m_mesh_memory.vertex_buffer_position,
                .vertex_buffer1         = &m_mesh_memory.vertex_buffer_non_position,
                .ambient_light          = glm::vec3{0.3f, 0.3f, 0.3f},
                .camera                 = m_camera.get(),
                .light_projections      = &m_light_projections,
                .lights                 = lights,
                .skins                  = {},
                .materials              = m_materials,
                .mesh_spans             = { cube_meshes },
                .render_pipeline_states = m_stencil_write_1_pipeline_states,
                .render_pass            = m_swapchain_render_pass.get(),
                .primitive_mode         = erhe::primitive::Primitive_mode::polygon_fill,
                .primitive_settings     = erhe::scene_renderer::Primitive_interface_settings{},
                .viewport               = viewport,
                .filter                 = erhe::Item_filter{},
                .override_shader_stages = m_stencil_cyan_shader_stages.get(),
                .debug_label            = "C_then_stw1_then_D: stencil cube",
                .reverse_depth          = true,
                .depth_range            = conventions.native_depth_range,
                .conventions            = conventions
            }
        );
        // 3) minimal pipe D
        draw_minimal_compute_triangle(encoder, viewport, *m_minimal_pipeline_D_stencil_eq_1_yellow);
    } else if (name == "stw1_then_minimal_D") {
        // Mirrors the Phase B5 failing pattern (cube_with_edge_lines +
        // stencil_wide_line) but with a compute-produced triangle
        // instead of a compute-produced wide line:
        //   1. forward_renderer draws the stencil cube using
        //      m_stencil_write_1_pipeline_states (stencil_test=true,
        //      always, replace 1, write_mask=0xFF). This also binds the
        //      program_interface bind_group_layout.
        //   2. draw_minimal_compute_triangle() then binds the minimal
        //      empty graphics bind_group_layout and pipeline D
        //      (stencil_test=true, equal 1, keep). Both pipelines have
        //      stencil_test_enable=true.
        //
        // Expected correct behaviour:
        //   - cube silhouette has color (stw1 draws it) and stencil=1.
        //   - minimal_D triangle passes only where stencil=1 (inside
        //     cube silhouette); fails elsewhere.
        //
        // Expected bug behaviour: minimal_D inherits stw1's "always"
        // stencil function (stencil function/reference delta is dropped
        // on pipeline switch), so the triangle is drawn in full.
        if (!m_stencil_cube || !m_minimal_pipeline_D_stencil_eq_1_yellow) {
            return;
        }
        const std::vector<std::shared_ptr<erhe::scene::Mesh>> cube_meshes{m_stencil_cube};
        encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
        encoder.set_scissor_rect (viewport.x, viewport.y, viewport.width, viewport.height);
        const auto& conventions = m_graphics_device.get_info().coordinate_conventions;
        m_forward_renderer->render(
            erhe::scene_renderer::Forward_renderer::Render_parameters{
                .render_encoder         = encoder,
                .index_type             = erhe::dataformat::Format::format_32_scalar_uint,
                .index_buffer           = &m_mesh_memory.index_buffer,
                .vertex_buffer0         = &m_mesh_memory.vertex_buffer_position,
                .vertex_buffer1         = &m_mesh_memory.vertex_buffer_non_position,
                .ambient_light          = glm::vec3{0.3f, 0.3f, 0.3f},
                .camera                 = m_camera.get(),
                .light_projections      = &m_light_projections,
                .lights                 = lights,
                .skins                  = {},
                .materials              = m_materials,
                .mesh_spans             = { cube_meshes },
                .render_pipeline_states = m_stencil_write_1_pipeline_states,
                .render_pass            = m_swapchain_render_pass.get(),
                .primitive_mode         = erhe::primitive::Primitive_mode::polygon_fill,
                .primitive_settings     = erhe::scene_renderer::Primitive_interface_settings{},
                .viewport               = viewport,
                .filter                 = erhe::Item_filter{},
                .override_shader_stages = m_stencil_cyan_shader_stages.get(),
                .debug_label            = "stw1_then_minimal_D: stencil cube",
                .reverse_depth          = true,
                .depth_range            = conventions.native_depth_range,
                .conventions            = conventions
            }
        );
        draw_minimal_compute_triangle(encoder, viewport, *m_minimal_pipeline_D_stencil_eq_1_yellow);
    } else if ((name == "cube_no_stencil") ||
               (name == "cube_stencil_test_ne_0") ||
               (name == "cube_stencil_test_ne_0_no_depth")) {
        // Minimal VkPipeline-switch repro (no wide-line renderer).
        // Both subtests draw m_stencil_cube via forward_renderer using a
        // pipeline that differs only in its depth_stencil state:
        //   cube_no_stencil         : stencil_test_enable=false
        //   cube_stencil_test_ne_0  : stencil_test_enable=true,
        //                             function=not_equal, reference=0
        // With a swapchain stencil cleared to 0, cube_stencil_test_ne_0
        // should produce NO visible cube (0 != 0 is false -> masked out).
        // cube_no_stencil always produces a cube. When paired in a grid,
        // the SECOND tile's VkPipeline switch is what the KosmicKrisp
        // MTLDepthStencilState regression tests.
        if (!m_stencil_cube) {
            return;
        }
        const std::vector<std::shared_ptr<erhe::scene::Mesh>> cube_meshes{m_stencil_cube};
        std::vector<erhe::graphics::Lazy_render_pipeline*>* pipeline_states_ptr = nullptr;
        const char* debug_label = "";
        if (name == "cube_no_stencil") {
            pipeline_states_ptr = &m_no_stencil_red_pipeline_states;
            debug_label         = "pipeline-switch repro: cube_no_stencil (red)";
        } else if (name == "cube_stencil_test_ne_0") {
            pipeline_states_ptr = &m_stencil_test_ne_0_green_pipeline_states;
            debug_label         = "pipeline-switch repro: cube_stencil_test_ne_0 (green)";
        } else {
            pipeline_states_ptr = &m_stencil_test_ne_0_no_depth_green_pipeline_states;
            debug_label         = "pipeline-switch repro: cube_stencil_test_ne_0_no_depth (green)";
        }
        std::vector<erhe::graphics::Lazy_render_pipeline*>& pipeline_states = *pipeline_states_ptr;
        encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
        encoder.set_scissor_rect (viewport.x, viewport.y, viewport.width, viewport.height);
        const auto& conventions = m_graphics_device.get_info().coordinate_conventions;
        m_forward_renderer->render(
            erhe::scene_renderer::Forward_renderer::Render_parameters{
                .render_encoder         = encoder,
                .index_type             = erhe::dataformat::Format::format_32_scalar_uint,
                .index_buffer           = &m_mesh_memory.index_buffer,
                .vertex_buffer0         = &m_mesh_memory.vertex_buffer_position,
                .vertex_buffer1         = &m_mesh_memory.vertex_buffer_non_position,
                .ambient_light          = glm::vec3{0.3f, 0.3f, 0.3f},
                .camera                 = m_camera.get(),
                .light_projections      = &m_light_projections,
                .lights                 = lights,
                .skins                  = {},
                .materials              = m_materials,
                .mesh_spans             = { cube_meshes },
                .render_pipeline_states = pipeline_states,
                .render_pass            = m_swapchain_render_pass.get(),
                .primitive_mode         = erhe::primitive::Primitive_mode::polygon_fill,
                .primitive_settings     = erhe::scene_renderer::Primitive_interface_settings{},
                .viewport               = viewport,
                .filter                 = erhe::Item_filter{},
                .override_shader_stages = nullptr,
                .debug_label            = debug_label,
                .reverse_depth          = true,
                .depth_range            = conventions.native_depth_range,
                .conventions            = conventions
            }
        );
    } else if (name == "rtt") {
        draw_textured_quad_cell(encoder, viewport);
    } else if (name == "texture_heap_2") {
        std::vector<std::shared_ptr<erhe::graphics::Texture>> tex2{m_red_texture, m_green_texture};
        draw_multi_texture_quad(encoder, viewport, tex2, *m_swapchain_render_pass.get());
    } else if (name == "texture_heap_3") {
        std::vector<std::shared_ptr<erhe::graphics::Texture>> tex3{m_red_texture, m_green_texture, m_blue_texture};
        draw_multi_texture_quad(encoder, viewport, tex3, *m_swapchain_render_pass.get());
    } else if (name == "separate_samplers_2") {
        std::vector<std::shared_ptr<erhe::graphics::Texture>> tex2{m_red_texture, m_green_texture};
        draw_separate_samplers_quad(encoder, viewport, tex2, *m_swapchain_render_pass.get());
    } else if (name == "separate_samplers_3") {
        std::vector<std::shared_ptr<erhe::graphics::Texture>> tex3{m_red_texture, m_green_texture, m_blue_texture};
        draw_separate_samplers_quad(encoder, viewport, tex3, *m_swapchain_render_pass.get());
    } else if (name == "msaa_depth") {
        draw_depth_visualize_cell(encoder, viewport);
    } else if (name == "stencil_polygon") {
        render_stencil_cell(encoder, viewport, lights, m_swapchain_render_pass.get());
    } else if (name == "stencil_wide_line") {
        render_stencil_wide_line_cell(encoder, viewport, lights, m_swapchain_render_pass.get());
    } else {
        log_test->warn("dispatch_subtest: unknown subtest name '{}'", std::string{name});
    }
}

void Rendering_test::tick(erhe::graphics::Command_buffer& command_buffer)
{
    const int full_width  = m_window.get_width();
    const int full_height = m_window.get_height();

    m_scene.update_node_transforms();

    const auto& conventions = m_graphics_device.get_info().coordinate_conventions;
    std::vector<std::shared_ptr<erhe::scene::Light>> lights;
    lights.push_back(m_light);

    // The "reference tile" is the grid slot at (0,0); its dimensions are
    // used as the canonical pre-pass / shadow-map viewport. All grid
    // tiles have identical dimensions so this is also the size used by
    // the compute wide-line dispatch.
    const erhe::math::Viewport ref_tile = get_grid_tile_viewport(0, 0);

    m_light_projections.apply(
        lights,
        m_camera.get(),
        ref_tile,
        erhe::math::Viewport{},
        std::shared_ptr<erhe::graphics::Texture>{},
        true,
        conventions.native_depth_range,
        conventions
    );

    std::vector<std::shared_ptr<erhe::scene::Mesh>> meshes;
    meshes.push_back(m_test_cube);

    // --- Pass 1: render 3D scene to texture (consumed by "rtt" subtest) ---
    if (has_subtest("rtt")) {
        update_texture_render_pass(ref_tile.width, ref_tile.height);
        erhe::graphics::Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(command_buffer);
        erhe::graphics::Scoped_render_pass scoped_render_pass{*m_texture_render_pass.get(), command_buffer};
        render_scene(encoder, erhe::math::Viewport{0, 0, ref_tile.width, ref_tile.height}, lights, meshes, m_texture_render_pass.get());
    }

    // --- Pass 1b: MSAA depth resolve prepass (consumed by "msaa_depth" subtest) ---
    if (has_subtest("msaa_depth")) {
        update_msaa_depth_render_pass(ref_tile.width, ref_tile.height);
        erhe::graphics::Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(command_buffer);
        erhe::graphics::Scoped_render_pass scoped_render_pass{*m_msaa_depth_render_pass.get(), command_buffer};
        render_scene(encoder, erhe::math::Viewport{0, 0, ref_tile.width, ref_tile.height}, lights, meshes, m_msaa_depth_render_pass.get());
    }

    // --- Compute pass: expand edge lines to triangles.
    //     "edge_lines_compute" subtest uses group 0 (cube edges).
    //     "stencil_wide_line" subtest uses group 1 (sphere edges). ---
    const bool compute_cube   = has_subtest("cube_with_edge_lines")
                              || has_subtest("cube_with_edge_lines_stencil")
                              || has_subtest("cube_edge_lines_only")
                              || has_subtest("cube_edge_lines_stencil_masked");
    const bool compute_sphere = has_subtest("stencil_wide_line")
                              || has_subtest("sphere_edge_lines_only")
                              || has_subtest("sphere_edge_lines_stencil_masked");
    const bool compute_minimal_triangle = has_subtest("minimal_compute_A")
                                        || has_subtest("minimal_compute_B")
                                        || has_subtest("minimal_compute_C")
                                        || has_subtest("minimal_compute_D")
                                        || has_subtest("stw1_then_minimal_D")
                                        || has_subtest("C_then_stw1_then_D");
    const bool wide_line_needed = (compute_cube || compute_sphere);

    // Minimal-compute-triangle dispatch. Independent of the wide-line
    // renderer; runs in its own compute encoder scope.
    if (compute_minimal_triangle) {
        erhe::graphics::Compute_command_encoder compute_encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        dispatch_minimal_compute_triangle(compute_encoder);
    }

    if (wide_line_needed && m_content_wide_line_renderer && m_content_wide_line_renderer->is_enabled()) {
        m_content_wide_line_renderer->begin_frame();
        if (compute_cube) {
            m_content_wide_line_renderer->add_mesh(*m_test_cube, glm::vec4{0.0f, 0.0f, 0.4f, 1.0f}, -6.0f, /*group=*/0);
        }
        if (compute_sphere && m_stencil_sphere) {
            m_content_wide_line_renderer->add_mesh(
                *m_stencil_sphere,
                glm::vec4{1.0f, 1.0f, 0.0f, 1.0f},
                -6.0f,
                /*group=*/1
            );
        }
        {
            erhe::graphics::Compute_command_encoder compute_encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
            const bool reverse_depth = (conventions.native_depth_range == erhe::math::Depth_range::zero_to_one);
            m_content_wide_line_renderer->compute(
                compute_encoder, ref_tile, *m_camera.get(),
                reverse_depth,
                conventions.native_depth_range,
                conventions
            );
        }
        // Compute -> vertex-attribute barrier; must be emitted after the
        // compute encoder scope ends.
        command_buffer.memory_barrier(
            erhe::graphics::Memory_barrier_mask::vertex_attrib_array_barrier_bit
        );
    }

    // Minimal-compute-triangle uses its own compute encoder (above);
    // emit its compute -> vertex-attribute barrier here too.
    if (compute_minimal_triangle) {
        command_buffer.memory_barrier(
            erhe::graphics::Memory_barrier_mask::vertex_attrib_array_barrier_bit
        );
    }

    // --- Pass 2: swapchain pass (drive subtests via the cells[] layout) ---
    update_swapchain_render_pass(full_width, full_height);
    {
        erhe::graphics::Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(command_buffer);
        erhe::graphics::Scoped_render_pass scoped_render_pass{*m_swapchain_render_pass.get(), command_buffer};

        if (is_fullscreen_mode()) {
            const std::string_view name = get_subtest_at(m_settings.fullscreen_cell_col, m_settings.fullscreen_cell_row);
            const erhe::math::Viewport fw{0, 0, full_width, full_height};
            erhe::graphics::Scoped_debug_group scope{"Fullscreen subtest"};
            dispatch_subtest(name, command_buffer, encoder, fw, lights, meshes);
        } else if (is_replicate_mode()) {
            const std::string_view name = get_subtest_at(m_settings.replicate_cell_col, m_settings.replicate_cell_row);
            const int cols = m_settings.cols;
            const int rows = m_settings.rows;
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    const erhe::math::Viewport tile = get_grid_tile_viewport(c, r);
                    erhe::graphics::Scoped_debug_group scope{"Replicated subtest"};
                    dispatch_subtest(name, command_buffer, encoder, tile, lights, meshes);
                }
            }
        } else {
            const int cols = m_settings.cols;
            const int rows = m_settings.rows;
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    const std::string_view name = get_subtest_at(c, r);
                    if (name.empty()) {
                        continue;
                    }
                    const erhe::math::Viewport tile = get_grid_tile_viewport(c, r);
                    erhe::graphics::Scoped_debug_group scope{"Grid subtest"};
                    dispatch_subtest(name, command_buffer, encoder, tile, lights, meshes);
                }
            }
        }
    }

    if (wide_line_needed && m_content_wide_line_renderer && m_content_wide_line_renderer->is_enabled()) {
        m_content_wide_line_renderer->end_frame();
    }
    if (compute_minimal_triangle) {
        end_frame_minimal_compute_triangle();
    }
}

void run_rendering_test(std::string_view config_path)
{
    erhe::file::ensure_working_directory_contains("config/rendering_test/erhe_graphics.json");

    erhe::log::redirect_stderr_to_file("logs/stderr.txt");
    erhe::log::initialize_log_sinks();
    {
        std::optional<std::string> contents = erhe::file::read("logging config", "config/rendering_test/logging.json");
        if (contents.has_value()) {
            erhe::log::load_log_configuration(contents.value());
        }
    }

    rendering_test::initialize_logging();

    erhe::dataformat::initialize_logging();
    erhe::file::initialize_logging();
    erhe::graphics::initialize_logging();
    erhe::gltf::initialize_logging();
    erhe::item::initialize_logging();
    erhe::math::initialize_logging();
    erhe::primitive::initialize_logging();
    erhe::raytrace::initialize_logging();
    erhe::renderer::initialize_logging();
    erhe::scene::initialize_logging();
    erhe::scene_renderer::initialize_logging();
    erhe::ui::initialize_logging();
    erhe::window::initialize_logging();

    Rendering_test test{config_path};
    test.run();
}

} // namespace rendering_test
