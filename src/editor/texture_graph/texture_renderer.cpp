#include "texture_graph/texture_renderer.hpp"
#include "editor_log.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/fragment_output.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"

#include "erhe_texgen/shader_code.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <span>

namespace editor {

namespace {

// Fullscreen-triangle vertex shader emitting a [0,1] v_uv varying, matching
// erhe::texgen's uv_source_expression "v_uv" (identical to the proven recipe in
// src/erhe/graphics/test/test_texgen_render.cpp).
constexpr const char* c_vertex_source = R"glsl(
layout(location = 0) out vec2 v_uv;
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    v_uv = (positions[gl_VertexID] * 0.5) + vec2(0.5);
}
)glsl";

// The assembled fragment body reads the "v_uv" varying; erhe injects only
// fragment outputs, so the input declaration is prepended to the body.
constexpr const char* c_fragment_varying = "layout(location = 0) in vec2 v_uv;\n";

} // namespace

Texture_renderer::Texture_renderer(erhe::graphics::Device& device)
    : m_device{device}
{
    m_empty_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
        device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"texture graph empty layout"},
            .uses_texture_heap = false
        }
    );
    m_ubo_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
        device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = 0u,
                    .type          = erhe::graphics::Binding_type::uniform_buffer,
                    .stage_flags   = erhe::graphics::Shader_stage_flags::fragment
                }
            },
            .debug_label       = erhe::utility::Debug_label{"texture graph ubo layout"},
            .uses_texture_heap = false
        }
    );
    m_fragment_outputs = std::make_unique<erhe::graphics::Fragment_outputs>(
        std::initializer_list<erhe::graphics::Fragment_output>{
            erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 }
        }
    );
    m_sampler = std::make_unique<erhe::graphics::Sampler>(
        device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::linear,
            .mag_filter  = erhe::graphics::Filter::linear,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .debug_label = erhe::utility::Debug_label{"texture graph sampler"}
        }
    );
}

Texture_renderer::~Texture_renderer() noexcept = default;

auto Texture_renderer::color_format() -> erhe::dataformat::Format
{
    return erhe::dataformat::Format::format_8_vec4_unorm;
}

void Texture_renderer::begin_frame()
{
    m_current_slot = static_cast<std::size_t>(m_device.get_frame_index() % s_frame_ring);
    m_ubo_ring[m_current_slot].clear(); // keep capacity - no steady-state allocation
}

auto Texture_renderer::make_target(const int size) -> std::shared_ptr<erhe::graphics::Texture>
{
    const int clamped = std::max(1, size);
    std::shared_ptr<erhe::graphics::Texture> texture = std::make_shared<erhe::graphics::Texture>(
        m_device,
        erhe::graphics::Texture_create_info{
            .device      = m_device,
            .usage_mask  =
                erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                erhe::graphics::Image_usage_flag_bit_mask::sampled          |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_src,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = color_format(),
            .width       = clamped,
            .height      = clamped,
            .debug_label = erhe::utility::Debug_label{"texture graph target"}
        }
    );
    return texture;
}

auto Texture_renderer::get_compiled(
    const std::string&                        fragment_body,
    const std::vector<erhe::texgen::Uniform>& uniforms
) -> const Compiled*
{
    const std::size_t hash = std::hash<std::string>{}(fragment_body);
    const auto it = m_cache.find(hash);
    if (it != m_cache.end()) {
        return &it->second;
    }

    Compiled compiled{};
    compiled.has_ubo = !uniforms.empty();

    const std::string full_fragment_source = std::string{c_fragment_varying} + fragment_body;
    const erhe::graphics::Bind_group_layout& layout = compiled.has_ubo ? *m_ubo_layout : *m_empty_layout;

    // Build the std140 UBO block "params" from the ordered uniform list (only
    // when the composition actually has uniforms).
    std::unique_ptr<erhe::graphics::Shader_resource> ubo_block;
    std::vector<const erhe::graphics::Shader_resource*> interface_blocks;
    if (compiled.has_ubo) {
        ubo_block = std::make_unique<erhe::graphics::Shader_resource>(
            m_device,
            erhe::graphics::Shader_resource::Block_create_info{
                .name          = "params",
                .binding_point = 0,
                .type          = erhe::graphics::Shader_resource::Type::uniform_block
            }
        );
        for (const erhe::texgen::Uniform& uniform : uniforms) {
            erhe::graphics::Shader_resource* member =
                (uniform.kind == erhe::texgen::Uniform_kind::vec4_uniform)
                    ? ubo_block->add_vec4 (uniform.name)
                    : ubo_block->add_float(uniform.name);
            compiled.member_offsets.push_back(member->get_offset_in_parent());
        }
        compiled.ubo_bytes = ubo_block->get_size_bytes(erhe::graphics::Shader_resource::Layout::std140);
        interface_blocks.push_back(ubo_block.get());
    }

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "texture_graph_fragment",
        .interface_blocks = interface_blocks,
        .fragment_outputs = m_fragment_outputs.get(),
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source} },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{full_fragment_source} }
        },
        .bind_group_layout = &layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(m_device, shader_create_info);
    if (!prototype.is_valid()) {
        log_graph_editor->warn("Texture graph: fragment shader failed to compile/link; keeping previous texture");
        // Cache the failure (valid == false) so the same bad source is not
        // recompiled every frame until the composition text changes.
        const auto inserted = m_cache.emplace(hash, std::move(compiled));
        return &inserted.first->second;
    }
    compiled.shader_stages = std::make_unique<erhe::graphics::Shader_stages>(m_device, std::move(prototype));

    // Pipeline format compatibility, set to exactly match the render pass built
    // in render_into() (single sampled color attachment, no depth). Size is not
    // part of the format hash, so one pipeline serves all target sizes.
    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
    pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
    pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    pipeline_create_info.base.bind_group_layout                 = &layout;
    pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
    pipeline_create_info.shader_stages                          = compiled.shader_stages.get();
    pipeline_create_info.vertex_input                           = nullptr;
    pipeline_create_info.color_attachment_count                 = 1;
    pipeline_create_info.color_attachment_formats[0]            = color_format();
    pipeline_create_info.color_usage_before[0]                  = erhe::graphics::Image_usage_flag_bit_mask::sampled;
    pipeline_create_info.color_usage_after[0]                   = erhe::graphics::Image_usage_flag_bit_mask::sampled;
    pipeline_create_info.sample_count                           = 1;

    compiled.pipeline = std::make_unique<erhe::graphics::Render_pipeline>(m_device, pipeline_create_info);
    compiled.valid    = compiled.pipeline->is_valid();
    if (!compiled.valid) {
        log_graph_editor->warn("Texture graph: render pipeline is not valid; keeping previous texture");
    }

    const auto inserted = m_cache.emplace(hash, std::move(compiled));
    return &inserted.first->second;
}

auto Texture_renderer::render_into(
    erhe::graphics::Command_buffer&           command_buffer,
    std::shared_ptr<erhe::graphics::Texture>& target,
    const int                                 size,
    const std::string&                        fragment_body,
    const std::vector<erhe::texgen::Uniform>& uniforms
) -> bool
{
    if (!command_buffer.is_recording()) {
        return false; // window hidden / no swapchain image this frame
    }

    const Compiled* compiled = get_compiled(fragment_body, uniforms);
    if ((compiled == nullptr) || !compiled->valid) {
        return false; // compile failure - keep previous texture
    }

    const int clamped = std::max(1, size);
    if (!target || (target->get_width() != clamped) || (target->get_height() != clamped)) {
        target = make_target(clamped);
        // Move the fresh texture out of UNDEFINED so the render pass can declare
        // layout_before = shader_read_only_optimal uniformly.
        command_buffer.transition_texture_layout(*target, erhe::graphics::Image_layout::shader_read_only_optimal);
    }

    const erhe::graphics::Bind_group_layout& layout = compiled->has_ubo ? *m_ubo_layout : *m_empty_layout;

    // Upload the live uniform values into a fresh per-render UBO buffer, kept
    // alive until this frame-in-flight slot is recycled.
    std::shared_ptr<erhe::graphics::Buffer> ubo;
    if (compiled->has_ubo && (compiled->ubo_bytes > 0)) {
        ubo = std::make_shared<erhe::graphics::Buffer>(
            m_device,
            erhe::graphics::Buffer_create_info{
                .capacity_byte_count                    = compiled->ubo_bytes,
                .memory_allocation_create_flag_bit_mask = erhe::graphics::Memory_allocation_create_flag_bit_mask::mapped,
                .usage                                  = erhe::graphics::Buffer_usage::uniform,
                .required_memory_property_bit_mask      =
                    erhe::graphics::Memory_property_flag_bit_mask::host_read |
                    erhe::graphics::Memory_property_flag_bit_mask::host_write,
                .preferred_memory_property_bit_mask     =
                    erhe::graphics::Memory_property_flag_bit_mask::host_coherent |
                    erhe::graphics::Memory_property_flag_bit_mask::host_persistent,
                .debug_label = erhe::utility::Debug_label{"texture graph ubo"}
            }
        );
        const std::span<std::byte> mapped = ubo->map_bytes(0, compiled->ubo_bytes);
        std::memset(mapped.data(), 0, compiled->ubo_bytes);
        for (std::size_t i = 0, end = uniforms.size(); i < end; ++i) {
            const erhe::texgen::Uniform& uniform = uniforms[i];
            const std::size_t            offset  = compiled->member_offsets[i];
            const std::size_t            count   = (uniform.kind == erhe::texgen::Uniform_kind::vec4_uniform) ? 4u : 1u;
            std::memcpy(mapped.data() + offset, uniform.value.data(), count * sizeof(float));
        }
        ubo->unmap();
        m_ubo_ring[m_current_slot].push_back(ubo);
    }

    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = target.get();
    descriptor.color_attachments[0].clear_value   = std::array<double, 4>{0.0, 0.0, 0.0, 1.0};
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::sampled;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::shader_read_only_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::sampled;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::shader_read_only_optimal;
    descriptor.render_target_width  = clamped;
    descriptor.render_target_height = clamped;
    descriptor.debug_label = erhe::utility::Debug_label{"texture graph render"};

    erhe::graphics::Render_pass            render_pass{m_device, descriptor};
    erhe::graphics::Render_command_encoder encoder = m_device.make_render_command_encoder(command_buffer);
    const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
    encoder.set_viewport_rect(0, 0, clamped, clamped);
    encoder.set_scissor_rect (0, 0, clamped, clamped);
    encoder.set_bind_group_layout(&layout);
    encoder.set_render_pipeline(*compiled->pipeline);
    if (ubo) {
        encoder.set_buffer(erhe::graphics::Buffer_target::uniform, ubo.get(), 0, compiled->ubo_bytes, 0);
    }
    encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
    return true;
}

} // namespace editor
