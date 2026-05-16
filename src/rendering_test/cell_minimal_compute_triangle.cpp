#include "rendering_test_app.hpp"
#include "rendering_test_log.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_verify/verify.hpp"

#include <filesystem>

namespace rendering_test {

// Builds the smallest possible reproducer for the KosmicKrisp
// VkPipeline-switch bug that does NOT use Content_wide_line_renderer.
//
// Flow, mirroring the wide-line renderer structurally:
//   1. A compute shader writes 3 vec4 positions into a storage buffer
//      allocated per-frame from a Ring_buffer_client.
//   2. A memory barrier (vertex_attrib_array_barrier_bit) is emitted
//      between the compute and render encoders.
//   3. A graphics pipeline reads the same buffer range as a vertex
//      buffer (via set_vertex_buffer) and draws 3 vertices as a
//      triangle.
//
// Two graphics pipelines exist:
//   - pipeline A: stencil_test=false, red fragment color. Always
//     draws the triangle.
//   - pipeline B: stencil_test=true, function=not_equal, reference=1,
//     write_mask=0, keep/keep/keep, green fragment color. With the
//     swapchain stencil cleared to 0, 0 != 1 -> test passes ->
//     triangle IS drawn. Pairing A then B in a 2-cell grid is a
//     VkPipeline switch between two pipelines that differ only in
//     depth_stencil state, using a compute-produced vertex buffer.
//
// The graphics pipelines use a dedicated empty Bind_group_layout so
// the pipeline's layout statically uses NO descriptor set and the
// test does not require any prior forward_renderer call to bind
// descriptors (no VUID-08600).
void Rendering_test::make_minimal_compute_triangle()
{
    if (!m_graphics_device.get_info().use_compute_shader) {
        log_test->warn("minimal_compute_triangle: compute shaders unavailable on this backend");
        return;
    }

    using namespace erhe::graphics;

    // --- Vertex format: stream 0 with one vec4 position attribute.
    m_minimal_vertex_format = erhe::dataformat::Vertex_format{
        {
            0,
            {
                { erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::position, 0 },
            }
        }
    };
    m_minimal_vertex_input = std::make_unique<Vertex_input_state>(
        m_graphics_device,
        Vertex_input_state_data::make(m_minimal_vertex_format)
    );

    // --- SSBO that the compute shader writes to. Struct `triangle_vertex`
    // has one `position_0` field (erhe names struct fields from
    // Vertex_attribute_usage + usage_index - see add_attribute).
    m_minimal_triangle_vertex_struct = std::make_unique<Shader_resource>(m_graphics_device, "triangle_vertex");
    m_minimal_triangle_ssbo_block = std::make_unique<Shader_resource>(
        m_graphics_device,
        Shader_resource::Block_create_info{
            .name          = "triangle_vertex_buffer",
            .binding_point = 0,
            .type          = Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    );
    add_vertex_stream(
        m_minimal_vertex_format.streams.front(),
        *m_minimal_triangle_vertex_struct.get(),
        *m_minimal_triangle_ssbo_block.get()
    );

    // --- Bind group layouts.
    // Compute side uses a 3-binding layout structurally matching
    // Content_wide_line_renderer's bind_group_layout: the triangle
    // output SSBO at binding 0 (the one actually written), plus two
    // decoy bindings at 1 (storage) and 3 (uniform) that our compute
    // shader does not reference but which make the layout's shape
    // identical to the wide-line renderer's layout.
    m_minimal_compute_bind_group_layout = std::make_unique<Bind_group_layout>(
        m_graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                { m_minimal_triangle_ssbo_block->get_binding_point(), Binding_type::storage_buffer },
                { 1, Binding_type::storage_buffer },
                { 3, Binding_type::uniform_buffer },
            },
            .debug_label       = "Minimal compute (3-binding wide-line shape)",
            .uses_texture_heap = false
        }
    );
    m_minimal_graphics_bind_group_layout = std::make_unique<Bind_group_layout>(
        m_graphics_device,
        Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = "Minimal graphics",
            .uses_texture_heap = false
        }
    );

    const std::filesystem::path shader_path = std::filesystem::path{"res"} / std::filesystem::path{"rendering_test"} / std::filesystem::path{"shaders"};

    // --- Compile the compute shader.
    {
        Shader_stages_create_info create_info{
            .name             = "minimal_compute_triangle",
            .struct_types     = { m_minimal_triangle_vertex_struct.get() },
            .interface_blocks = { m_minimal_triangle_ssbo_block.get() },
            .shaders          = { { Shader_type::compute_shader, shader_path / "minimal_compute_triangle.comp" } },
            .bind_group_layout = m_minimal_compute_bind_group_layout.get(),
        };
        Shader_stages_prototype prototype = build_shader_stages(m_graphics_device, create_info);
        if (!prototype.is_valid()) {
            log_test->warn("minimal_compute_triangle: compute shader invalid");
            return;
        }
        m_minimal_compute_shader_stages = std::make_unique<Shader_stages>(m_graphics_device, std::move(prototype));
    }

    // --- Compile two graphics shader-stage variants, identical except
    // for the MINIMAL_TRIANGLE_COLOR define.
    //
    // The graphics shader statically reads `camera.cameras[0].viewport`
    // so the resulting VkPipeline's layout statically uses descriptor
    // set 0 (mirroring Content_wide_line_renderer's graphics pipeline).
    // This means the shader_stages / pipeline layout comes from the
    // shared program_interface.bind_group_layout, not the empty
    // m_minimal_graphics_bind_group_layout. The empty layout is still
    // used at render time via set_bind_group_layout() to create the
    // wide-line-style layout mismatch.
    auto make_graphics_stages = [&](const char* color_glsl, const char* debug_name) -> std::unique_ptr<Shader_stages>
    {
        Shader_stages_create_info create_info{
            .name             = debug_name,
            .defines          = { { "MINIMAL_TRIANGLE_COLOR", color_glsl } },
            .struct_types     = { &m_program_interface.camera_interface.camera_struct },
            .interface_blocks = { &m_program_interface.camera_interface.camera_block  },
            .fragment_outputs = &m_minimal_fragment_outputs,
            .vertex_format    = &m_minimal_vertex_format,
            .shaders = {
                { Shader_type::vertex_shader,   shader_path / "minimal_triangle.vert" },
                { Shader_type::fragment_shader, shader_path / "minimal_triangle.frag" }
            },
            .bind_group_layout = m_program_interface.bind_group_layout.get(),
        };
        Shader_stages_prototype prototype = build_shader_stages(m_graphics_device, create_info);
        if (!prototype.is_valid()) {
            log_test->warn("minimal_compute_triangle: graphics shader '{}' invalid", debug_name);
            return nullptr;
        }
        return std::make_unique<Shader_stages>(m_graphics_device, std::move(prototype));
    };
    m_minimal_graphics_shader_stages_red    = make_graphics_stages("vec4(1.0, 0.0, 0.0, 1.0)", "minimal_triangle_red");
    m_minimal_graphics_shader_stages_green  = make_graphics_stages("vec4(0.0, 1.0, 0.0, 1.0)", "minimal_triangle_green");
    m_minimal_graphics_shader_stages_blue   = make_graphics_stages("vec4(0.2, 0.4, 1.0, 1.0)", "minimal_triangle_blue");
    m_minimal_graphics_shader_stages_yellow = make_graphics_stages("vec4(1.0, 1.0, 0.0, 1.0)", "minimal_triangle_yellow");
    if (!m_minimal_graphics_shader_stages_red ||
        !m_minimal_graphics_shader_stages_green ||
        !m_minimal_graphics_shader_stages_blue ||
        !m_minimal_graphics_shader_stages_yellow)
    {
        return;
    }

    // --- Compute pipeline.
    m_minimal_compute_pipeline.emplace(
        m_graphics_device,
        Compute_pipeline_data{
            .name              = "minimal_compute_triangle",
            .shader_stages     = m_minimal_compute_shader_stages.get(),
            .bind_group_layout = m_minimal_compute_bind_group_layout.get()
        }
    );

    // --- Graphics pipelines A (stencil_test=false, red) and B
    // (stencil_test=true, not_equal 1, green). They differ only in
    // depth_stencil state, mirroring the wl_ns / wl_st pair.
    m_minimal_pipeline_A_red = std::make_unique<Lazy_render_pipeline>(
        m_graphics_device,
        Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Minimal pipeline A (stencil=off, red)"},
            //.shader_stages  = m_minimal_graphics_shader_stages_red.get(),
            //.vertex_input   = m_minimal_vertex_input.get(),
            .input_assembly = Input_assembly_state::triangle,
            .rasterization  = Rasterization_state::cull_mode_none,
            .depth_stencil  = {
                .depth_test_enable   = true,
                .depth_write_enable  = true,
                .depth_compare_op    = get_depth_function(Compare_operation::less, true),
                .stencil_test_enable = false
            },
            .color_blend    = Color_blend_state::color_blend_premultiplied
        }
    );

    const Stencil_op_state test_ne_1_op{
        .stencil_fail_op = Stencil_op::keep,
        .z_fail_op       = Stencil_op::keep,
        .z_pass_op       = Stencil_op::keep,
        .function        = Compare_operation::not_equal,
        .reference       = 0x01u,
        .test_mask       = 0xFFu,
        .write_mask      = 0x00u
    };
    m_minimal_pipeline_B_green = std::make_unique<Lazy_render_pipeline>(
        m_graphics_device,
        Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Minimal pipeline B (stencil=on ne1, green)"},
            //.shader_stages  = m_minimal_graphics_shader_stages_green.get(),
            //.vertex_input   = m_minimal_vertex_input.get(),
            .input_assembly = Input_assembly_state::triangle,
            .rasterization  = Rasterization_state::cull_mode_none,
            .depth_stencil  = {
                .depth_test_enable   = false,
                .depth_write_enable  = false,
                .depth_compare_op    = Compare_operation::always,
                .stencil_test_enable = true,
                .stencil_front       = test_ne_1_op,
                .stencil_back        = test_ne_1_op
            },
            .color_blend    = Color_blend_state::color_blend_premultiplied
        }
    );

    // Pipeline C: stencil_test=true, function=always, keep ops, blue.
    // Always draws regardless of stencil state.
    const Stencil_op_state always_keep_op{
        .stencil_fail_op = Stencil_op::keep,
        .z_fail_op       = Stencil_op::keep,
        .z_pass_op       = Stencil_op::keep,
        .function        = Compare_operation::always,
        .reference       = 0x00u,
        .test_mask       = 0xFFu,
        .write_mask      = 0x00u
    };
    m_minimal_pipeline_C_stencil_always_blue = std::make_unique<Lazy_render_pipeline>(
        m_graphics_device,
        Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Minimal pipeline C (stencil=on always, blue)"},
            //.shader_stages  = m_minimal_graphics_shader_stages_blue.get(),
            //.vertex_input   = m_minimal_vertex_input.get(),
            .input_assembly = Input_assembly_state::triangle,
            .rasterization  = Rasterization_state::cull_mode_none,
            .depth_stencil  = {
                .depth_test_enable   = false,
                .depth_write_enable  = false,
                .depth_compare_op    = Compare_operation::always,
                .stencil_test_enable = true,
                .stencil_front       = always_keep_op,
                .stencil_back        = always_keep_op
            },
            .color_blend    = Color_blend_state::color_blend_premultiplied
        }
    );

    // Pipeline D: stencil_test=true, function=equal 1, keep ops, yellow.
    // Swapchain stencil is cleared to 0, and nothing writes it, so
    // "0 == 1" is always false and this pipeline should draw NOTHING.
    // If KosmicKrisp fails to re-emit MTLDepthStencilState when the
    // pipeline-switch delta is limited to the stencil function/
    // reference (stencil_test_enable stays true), pipeline D will
    // inherit C's "always" function and draw a yellow triangle.
    const Stencil_op_state eq_1_keep_op{
        .stencil_fail_op = Stencil_op::keep,
        .z_fail_op       = Stencil_op::keep,
        .z_pass_op       = Stencil_op::keep,
        .function        = Compare_operation::equal,
        .reference       = 0x01u,
        .test_mask       = 0xFFu,
        .write_mask      = 0x00u
    };
    m_minimal_pipeline_D_stencil_eq_1_yellow = std::make_unique<Lazy_render_pipeline>(
        m_graphics_device,
        Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Minimal pipeline D (stencil=on eq1, yellow)"},
            //.shader_stages  = m_minimal_graphics_shader_stages_yellow.get(),
            //.vertex_input   = m_minimal_vertex_input.get(),
            .input_assembly = Input_assembly_state::triangle,
            .rasterization  = Rasterization_state::cull_mode_none,
            .depth_stencil  = {
                .depth_test_enable   = false,
                .depth_write_enable  = false,
                .depth_compare_op    = Compare_operation::always,
                .stencil_test_enable = true,
                .stencil_front       = eq_1_keep_op,
                .stencil_back        = eq_1_keep_op
            },
            .color_blend    = Color_blend_state::color_blend_premultiplied
        }
    );

    m_minimal_compute_enabled = true;
    log_test->info("minimal_compute_triangle: enabled");
}

// Dispatches one compute workgroup that writes 3 vec4 positions into
// a freshly-acquired SSBO range. Call OUTSIDE any render pass, from
// inside a Compute_command_encoder scope. The caller is responsible
// for emitting the compute->vertex memory barrier after the encoder
// scope closes.
void Rendering_test::dispatch_minimal_compute_triangle(erhe::graphics::Compute_command_encoder& compute_encoder)
{
    if (!m_minimal_compute_enabled || !m_minimal_compute_pipeline.has_value()) {
        return;
    }
    using namespace erhe::graphics;

    compute_encoder.set_bind_group_layout(m_minimal_compute_bind_group_layout.get());
    compute_encoder.set_compute_pipeline(m_minimal_compute_pipeline.value());

    const std::size_t stride = m_minimal_vertex_format.streams.front().stride;
    // Pad byte_count like wide-line does: reserve space for 32 threads
    // worth of triangles (3 vertices * 32 threads) even though only
    // thread 0 actually writes. This matches Content_wide_line_renderer's
    // padded_edge_count sizing.
    const std::size_t padded_thread_count = 32;
    const std::size_t byte_count = 3 * padded_thread_count * stride;

    // Mirror Content_wide_line_renderer's multi-dispatch pattern: wide-
    // line issues one acquire+bind+dispatch cycle per mesh added. Do TWO
    // cycles here even though only the first range is consumed by the
    // graphics draw. The second cycle acquires a separate ring range
    // and dispatches into it, exercising the same multi-bind pattern
    // inside a single compute encoder scope.
    m_minimal_triangle_range = m_minimal_triangle_buffer_client.acquire(
        Ring_buffer_usage::GPU_access,
        byte_count
    );
    m_minimal_triangle_range.bytes_gpu_used(byte_count);
    m_minimal_triangle_range.close();
    m_minimal_triangle_buffer_client.bind(compute_encoder, m_minimal_triangle_range);
    m_minimal_triangle_range_valid = true;
    compute_encoder.dispatch_compute(1, 1, 1);

    // Second (decoy) dispatch into a separate ring range. Its output
    // is not used; purpose is solely to match wide-line's compute
    // encoder shape.
    erhe::graphics::Ring_buffer_range decoy_range = m_minimal_triangle_buffer_client.acquire(
        Ring_buffer_usage::GPU_access,
        byte_count
    );
    decoy_range.bytes_gpu_used(byte_count);
    decoy_range.close();
    m_minimal_triangle_buffer_client.bind(compute_encoder, decoy_range);
    compute_encoder.dispatch_compute(1, 1, 1);
    decoy_range.release();
}

// Draws the compute-produced triangle into the given viewport using
// the supplied graphics pipeline. Mirrors Content_wide_line_renderer::
// render() structurally: set_bind_group_layout(different) +
// set_render_pipeline(raw) + set_vertex_buffer + draw_primitives.
void Rendering_test::draw_minimal_compute_triangle(
    erhe::graphics::Render_command_encoder& encoder,
    const erhe::math::Viewport&             viewport,
    erhe::graphics::Lazy_render_pipeline&   pipeline_state
)
{
    if (!m_minimal_compute_enabled || !m_minimal_triangle_range_valid || !m_swapchain_render_pass) {
        return;
    }
    using namespace erhe::graphics;

    Render_pipeline* render_pipeline = pipeline_state.get_pipeline_for(
        m_swapchain_render_pass->get_descriptor(),
        nullptr,
        nullptr,
        nullptr
    );
    if (render_pipeline == nullptr) {
        return;
    }

    // Prep: invoke forward_renderer with an empty mesh span so the
    // program_interface bind_group_layout's descriptor sets (camera UBO
    // etc.) are bound. The minimal graphics pipelines statically use
    // descriptor set 0 via camera_block, so without this prep call
    // VUID-vkCmdDraw-None-08600 would fire on the first minimal draw.
    // This mirrors how Content_wide_line_renderer::render() relies on a
    // prior forward_renderer call from the surrounding cell body.
    {
        const std::vector<std::shared_ptr<erhe::scene::Light>> empty_lights;
        const std::vector<std::shared_ptr<erhe::scene::Mesh>>  empty_meshes;
        render_scene(encoder, viewport, empty_lights, empty_meshes, m_swapchain_render_pass.get());
    }

    encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
    encoder.set_scissor_rect (viewport.x, viewport.y, viewport.width, viewport.height);

    // Intentionally introduce a layout mismatch mirroring Content_wide_line_
    // renderer::render(): our pipelines are built against
    // m_program_interface.bind_group_layout, but we set the encoder to use
    // m_minimal_compute_bind_group_layout (which has one storage-buffer
    // binding). This matches the wide-line renderer's pattern of binding
    // its own multi-binding layout before a graphics pipeline whose
    // VkPipelineLayout was built from program_interface. Uses the raw
    // set_render_pipeline path (just vkCmdBindPipeline, no further state
    // re-emit) to stay consistent with Content_wide_line_renderer.
    encoder.set_bind_group_layout(m_minimal_compute_bind_group_layout.get());
    encoder.set_render_pipeline(*render_pipeline);

    Buffer*       triangle_buffer = m_minimal_triangle_range.get_buffer()->get_buffer();
    std::size_t   offset          = m_minimal_triangle_range.get_byte_start_offset_in_buffer();
    encoder.set_vertex_buffer(triangle_buffer, offset, 0);
    encoder.draw_primitives(Primitive_type::triangle, 0, 3);
}

// Releases the frame's ring-buffer range so it can be recycled.
// Mirrors Content_wide_line_renderer::end_frame().
void Rendering_test::end_frame_minimal_compute_triangle()
{
    if (m_minimal_triangle_range_valid) {
        m_minimal_triangle_range.release();
        m_minimal_triangle_range_valid = false;
    }
}

} // namespace rendering_test
