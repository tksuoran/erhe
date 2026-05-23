#include "rendering_test_app.hpp"

#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/span.hpp"

#include <filesystem>

namespace rendering_test {

void Rendering_test::make_depth_visualize_pipeline()
{
    // Unlike the other test cells, the depth visualize cell sources its
    // texture from a dedicated combined_image_sampler binding (s_depth)
    // declared in its own default uniform block, *not* from the texture
    // heap's bindless array (set 1). The bindless array can only sample
    // color textures, and our resolved depth target is depth-only.
    //
    // The Sampler_aspect::depth annotation on s_depth is what tells
    // Render_command_encoder::set_sampled_image() to bind a depth-aspect
    // image view when this cell pushes the resolved depth target.
    const std::filesystem::path shader_path = std::filesystem::path{"res"} / std::filesystem::path{"rendering_test"} / std::filesystem::path{"shaders"};

    // The depth visualize cell needs its own bind group layout that adds
    // an s_depth combined_image_sampler binding alongside the quad's
    // uniform buffer. The other cells keep using m_test_bind_group_layout
    // (which doesn't declare a depth sampler). The Sampler_aspect::depth
    // annotation tells Render_command_encoder::set_sampled_image() to
    // bind a depth-aspect VkImageView when this cell pushes the resolved
    // depth target.
    m_depth_visualize_bind_group_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
        m_graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                {
                    .binding_point = 0,
                    .type = erhe::graphics::Binding_type::uniform_buffer
                },
                {
                    .binding_point   = 0,
                    .type            = erhe::graphics::Binding_type::combined_image_sampler,
                    .sampler_aspect  = erhe::graphics::Sampler_aspect::depth,
                    .name            = "s_depth",
                    .glsl_type       = erhe::graphics::Glsl_type::sampler_2d,
                    .is_texture_heap = false
                }
            },
            .debug_label = "Depth Visualize"
        }
    );

    erhe::graphics::Shader_stages_create_info create_info{
        .name              = "depth_visualize",
        .interface_blocks  = { m_quad_block.get() },
        .fragment_outputs  = &m_quad_fragment_outputs,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   shader_path / "depth_visualize.vert" },
            { erhe::graphics::Shader_type::fragment_shader, shader_path / "depth_visualize.frag" }
        },
        .bind_group_layout = m_depth_visualize_bind_group_layout.get()
    };
    m_depth_visualize_shader_stages = std::make_unique<erhe::graphics::Shader_stages>(
        m_graphics_device,
        erhe::graphics::build_shader_stages(m_graphics_device, std::move(create_info))
    );

    m_depth_visualize_pipeline = erhe::graphics::Base_render_pipeline{
        m_graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label       = erhe::utility::Debug_label{"Depth Visualize Pipeline"},
            //.shader_stages     = m_depth_visualize_shader_stages.get(),
            //.vertex_input      = &m_empty_vertex_input,
            .input_assembly    = erhe::graphics::Input_assembly_state::triangle,
            .rasterization     = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil     = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
            .color_blend       = erhe::graphics::Color_blend_state::color_blend_disabled,
            .bind_group_layout = m_depth_visualize_bind_group_layout.get()
        }
    };
}

// Cell (0,3): visualize the resolved depth from the 4x MSAA depth-resolve
// render pass. The cube was rendered into the MSAA pass earlier in the
// frame; here we sample the single-sample resolve target through s_depth
// (a dedicated combined_image_sampler binding declared with
// Sampler_aspect::depth) and display it as grayscale. The texture heap is
// bypassed entirely because the heap is color-only and the resolved depth
// target is depth-only.
void Rendering_test::draw_depth_visualize_cell(
    erhe::graphics::Render_command_encoder& encoder,
    const erhe::math::Viewport&             viewport
)
{
    using erhe::graphics::as_span;
    using erhe::graphics::write;

    const auto& conventions = m_graphics_device.get_info().coordinate_conventions;
    const bool top_left = (conventions.texture_origin == erhe::math::Texture_origin::top_left);
    const glm::vec2 uv_min = top_left ? glm::vec2{0.0f, 1.0f} : glm::vec2{0.0f, 0.0f};
    const glm::vec2 uv_max = top_left ? glm::vec2{1.0f, 0.0f} : glm::vec2{1.0f, 1.0f};

    erhe::graphics::Ring_buffer_range quad_buffer_range = m_quad_buffer.acquire(
        erhe::graphics::Ring_buffer_usage::CPU_write,
        m_quad_block->get_size_bytes()
    );
    const std::span<std::byte> gpu_data   = quad_buffer_range.get_span();
    const std::span<float>     float_data{reinterpret_cast<float*>(gpu_data.data()), gpu_data.size_bytes() / sizeof(float)};
    const std::span<uint32_t>  uint_data {reinterpret_cast<uint32_t*>(gpu_data.data()), gpu_data.size_bytes() / sizeof(uint32_t)};

    // The shader samples through s_depth, not through the bindless heap,
    // so the per-cell uniform block doesn't need a texture handle. Zero
    // out the unused texture_handle slot.
    const uint32_t zero_handle[2] = {0u, 0u};
    write(uint_data,  m_quad_texture_handle->get_offset_in_parent(), std::span<const uint32_t>{&zero_handle[0], 2});
    write(float_data, m_quad_uv_min->get_offset_in_parent(),         as_span(uv_min));
    write(float_data, m_quad_uv_max->get_offset_in_parent(),         as_span(uv_max));
    quad_buffer_range.bytes_written(m_quad_block->get_size_bytes());
    quad_buffer_range.close();

    encoder.set_bind_group_layout(m_depth_visualize_bind_group_layout.get());
    {
        erhe::graphics::Render_pipeline* p = m_depth_visualize_pipeline.get_pipeline_for(
            m_swapchain_render_pass->get_descriptor(),
            m_depth_visualize_shader_stages.get(),
            &m_empty_vertex_input,
            nullptr // TODO where do we get vertex format from
        );
        if (p != nullptr) {
            encoder.set_render_pipeline(*p);
        }
    }
    encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
    encoder.set_scissor_rect(viewport.x, viewport.y, viewport.width, viewport.height);
    m_quad_buffer.bind(encoder, quad_buffer_range);
    encoder.set_sampled_image(/*binding_point=*/0, *m_msaa_depth_resolve_texture.get(), m_nearest_sampler);

    encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);

    quad_buffer_range.release();
}

} // namespace rendering_test
