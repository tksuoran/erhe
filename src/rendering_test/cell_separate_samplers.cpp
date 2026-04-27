#include "rendering_test_app.hpp"

#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/texture_heap.hpp"

#include <filesystem>

namespace rendering_test {

void Rendering_test::make_separate_samplers_pipeline()
{
    const std::filesystem::path shader_path = std::filesystem::path{"res"} / std::filesystem::path{"rendering_test"} / std::filesystem::path{"shaders"};

    // Uniform block with texture handles and count
    m_sep_tex_block = std::make_unique<erhe::graphics::Shader_resource>(
        m_graphics_device, "separate_tex", 0, erhe::graphics::Shader_resource::Type::uniform_block
    );
    m_sep_tex_handle_0 = m_sep_tex_block->add_uvec2("texture_handle_0"); // offset  0, size  8
    m_sep_tex_handle_1 = m_sep_tex_block->add_uvec2("texture_handle_1"); // offset  8, size  8
    m_sep_tex_handle_2 = m_sep_tex_block->add_uvec2("texture_handle_2"); // offset 16, size  8
    m_sep_tex_count    = m_sep_tex_block->add_uint ("texture_count");    // offset 24, size  4
    m_sep_tex_padding0 = m_sep_tex_block->add_uint ("padding0");         // offset 28, size  4
                                                                          // total  32 = 2 x vec4

    // Dedicated bind group layout for the sep-tex cell: 1 UBO + 3
    // combined_image_sampler bindings for s_tex0/s_tex1/s_tex2. The
    // samplers are bound directly via Render_command_encoder::set_sampled_image()
    // before each draw (post-processing pattern) -- they are not in the
    // texture heap. The texture heap below is still constructed so the
    // bindless shader paths (OpenGL bindless, Vulkan descriptor indexing)
    // get their handles populated in the UBO; on Metal and OpenGL
    // sampler array the heap is unused and the dedicated bindings do
    // the actual sampling.
    m_sep_tex_bind_group_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
        m_graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                {
                    .binding_point = 0,
                    .type          = erhe::graphics::Binding_type::uniform_buffer
                },
                {
                    .binding_point   = 0,
                    .type            = erhe::graphics::Binding_type::combined_image_sampler,
                    .sampler_aspect  = erhe::graphics::Sampler_aspect::color,
                    .name            = "s_tex0",
                    .glsl_type       = erhe::graphics::Glsl_type::sampler_2d,
                    .is_texture_heap = false
                },
                {
                    .binding_point   = 1,
                    .type            = erhe::graphics::Binding_type::combined_image_sampler,
                    .sampler_aspect  = erhe::graphics::Sampler_aspect::color,
                    .name            = "s_tex1",
                    .glsl_type       = erhe::graphics::Glsl_type::sampler_2d,
                    .is_texture_heap = false
                },
                {
                    .binding_point   = 2,
                    .type            = erhe::graphics::Binding_type::combined_image_sampler,
                    .sampler_aspect  = erhe::graphics::Sampler_aspect::color,
                    .name            = "s_tex2",
                    .glsl_type       = erhe::graphics::Glsl_type::sampler_2d,
                    .is_texture_heap = false
                }
            },
            .debug_label = "Separate Samplers"
        }
    );

    erhe::graphics::Shader_stages_create_info create_info{
        .name              = "separate_samplers_test",
        .interface_blocks  = { m_sep_tex_block.get() },
        .fragment_outputs  = &m_quad_fragment_outputs,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   shader_path / "multi_texture_test.vert" },
            { erhe::graphics::Shader_type::fragment_shader, shader_path / "separate_samplers_test.frag" }
        },
        .bind_group_layout = m_sep_tex_bind_group_layout.get()
    };
    m_sep_tex_shader_stages = std::make_unique<erhe::graphics::Shader_stages>(
        m_graphics_device,
        erhe::graphics::build_shader_stages(m_graphics_device, std::move(create_info))
    );

    m_sep_tex_pipeline = erhe::graphics::Lazy_render_pipeline{
        m_graphics_device,
        erhe::graphics::Render_pipeline_create_info{
            .debug_label       = erhe::utility::Debug_label{"Separate Samplers Test Pipeline"},
            .shader_stages     = m_sep_tex_shader_stages.get(),
            .vertex_input      = &m_empty_vertex_input,
            .input_assembly    = erhe::graphics::Input_assembly_state::triangle,
            .rasterization     = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil     = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
            .color_blend       = erhe::graphics::Color_blend_state::color_blend_disabled,
            .bind_group_layout = m_sep_tex_bind_group_layout.get()
        }
    };
}

void Rendering_test::draw_separate_samplers_quad(
    erhe::graphics::Render_command_encoder&                      encoder,
    const erhe::math::Viewport&                                  viewport,
    const std::vector<std::shared_ptr<erhe::graphics::Texture>>& textures,
    const erhe::graphics::Render_pass&                           render_pass
)
{
    using erhe::graphics::as_span;
    using erhe::graphics::write;

    const uint32_t tex_count = static_cast<uint32_t>(textures.size());

    // Create the texture heap so the bindless shader paths (OpenGL
    // bindless, Vulkan descriptor indexing) get their handles in the
    // UBO. On Metal and OpenGL sampler array the heap is unused -- the
    // explicit set_sampled_image() calls below do the actual binding.
    erhe::graphics::Texture_heap texture_heap{
        m_graphics_device, *m_red_texture.get(), m_nearest_sampler,
        m_sep_tex_bind_group_layout.get()
    };
    uint64_t handles[3] = {0, 0, 0};
    for (uint32_t i = 0; i < tex_count; ++i) {
        handles[i] = texture_heap.allocate(textures[i].get(), &m_nearest_sampler);
    }

    erhe::graphics::Ring_buffer_range buffer_range = m_quad_buffer.acquire(
        erhe::graphics::Ring_buffer_usage::CPU_write,
        m_sep_tex_block->get_size_bytes()
    );
    const std::span<std::byte> gpu_data  = buffer_range.get_span();
    const std::span<uint32_t>  uint_data{reinterpret_cast<uint32_t*>(gpu_data.data()), gpu_data.size_bytes() / sizeof(uint32_t)};

    auto write_handle = [&](erhe::graphics::Shader_resource* field, uint64_t handle) {
        const uint32_t h[2] = {
            static_cast<uint32_t>(handle & 0xffffffffu),
            static_cast<uint32_t>(handle >> 32u)
        };
        write(uint_data, field->get_offset_in_parent(), std::span<const uint32_t>{&h[0], 2});
    };

    if (tex_count > 0) write_handle(m_sep_tex_handle_0, handles[0]);
    if (tex_count > 1) write_handle(m_sep_tex_handle_1, handles[1]);
    if (tex_count > 2) write_handle(m_sep_tex_handle_2, handles[2]);
    write(uint_data, m_sep_tex_count->get_offset_in_parent(), std::span<const uint32_t>{&tex_count, 1});

    buffer_range.bytes_written(m_sep_tex_block->get_size_bytes());
    buffer_range.close();

    encoder.set_bind_group_layout(m_sep_tex_bind_group_layout.get());
    {
        erhe::graphics::Render_pipeline* p = m_sep_tex_pipeline.get_pipeline_for(render_pass.get_descriptor());
        if (p != nullptr) {
            encoder.set_render_pipeline(*p);
        }
    }
    encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
    encoder.set_scissor_rect(viewport.x, viewport.y, viewport.width, viewport.height);
    m_quad_buffer.bind(encoder, buffer_range);
    texture_heap.bind(encoder);

    // Bind the named scalar samplers via the explicit per-call API. On
    // Metal these become direct [[texture(N)]]/[[sampler(N)]] bindings;
    // on Vulkan they are pushed via push descriptors. Bind every slot,
    // padding with the red fallback texture when fewer than 3 textures
    // are supplied so the descriptor set is fully populated.
    const erhe::graphics::Texture* tex0 = (tex_count > 0) ? textures[0].get() : m_red_texture.get();
    const erhe::graphics::Texture* tex1 = (tex_count > 1) ? textures[1].get() : m_red_texture.get();
    const erhe::graphics::Texture* tex2 = (tex_count > 2) ? textures[2].get() : m_red_texture.get();
    encoder.set_sampled_image(0, *tex0, m_nearest_sampler);
    encoder.set_sampled_image(1, *tex1, m_nearest_sampler);
    encoder.set_sampled_image(2, *tex2, m_nearest_sampler);

    encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);

    texture_heap.unbind();
    buffer_range.release();
}

} // namespace rendering_test
