#include "renderers/id_renderer.hpp"

#include "editor_log.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "renderers/programs.hpp"

#include "config/generated/id_renderer_config.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_scene_renderer/joint_buffer.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/shader_variant_cache.hpp"

#include <algorithm>
#include <filesystem>

namespace editor {

using erhe::graphics::Render_pass;
using erhe::graphics::Renderbuffer;
using erhe::graphics::Texture;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;

using glm::mat4;

// TODO Check mapping and coherency

using Vertex_input_state   = erhe::graphics::Vertex_input_state;
using Input_assembly_state = erhe::graphics::Input_assembly_state;
using Rasterization_state  = erhe::graphics::Rasterization_state;
using Depth_stencil_state  = erhe::graphics::Depth_stencil_state;
using Color_blend_state    = erhe::graphics::Color_blend_state;

Id_renderer::Id_renderer(
    const Id_renderer_config&                   id_renderer_config,
    erhe::graphics::Device&                     graphics_device,
    erhe::scene_renderer::Program_interface&    program_interface,
    erhe::scene_renderer::Mesh_memory&          mesh_memory,
    erhe::scene_renderer::Shader_variant_cache& shader_variant_cache,
    Programs&                                   programs
)
    : m_graphics_device      {graphics_device}
    , m_mesh_memory          {mesh_memory}
    , m_shader_variant_cache {shader_variant_cache}
    , m_programs             {programs}
    , m_bind_group_layout    {program_interface.bind_group_layout.get()}
    , m_y_flip               {graphics_device.get_info().coordinate_conventions.clip_space_y_flip == erhe::math::Clip_space_y_flip::enabled}
    , m_camera_buffers       {graphics_device, program_interface.camera_interface}
    , m_draw_indirect_buffers{graphics_device, program_interface.config.max_draw_count}
    , m_primitive_buffers    {graphics_device, program_interface.primitive_interface}
    , m_pipeline{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"ID Renderer"},
            .input_assembly = Input_assembly_state::triangle,
            .rasterization  = Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(m_y_flip),
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(graphics_device.get_reverse_depth()),
            .color_blend    = &Color_blend_state::color_blend_disabled
        }
    }
    , m_selective_depth_clear_pipeline{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"ID Renderer selective depth clear"},
            .input_assembly = Input_assembly_state::triangle,
            .rasterization  = Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(m_y_flip),
            .depth_stencil  = Depth_stencil_state::depth_test_always_stencil_test_disabled,
            .color_blend    = &Color_blend_state::color_writes_disabled
        }
    }
    , m_texture_read_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::transfer_dst,
        "Id_renderer::m_texture_read_buffer"
    }
    , m_region_read_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::transfer_dst,
        "Id_renderer::m_region_read_buffer"
    }
    , m_scan_input_buffer  {graphics_device, erhe::graphics::Buffer_target::storage,      "Id_renderer::m_scan_input_buffer"  }
    , m_scan_bitmask_buffer{graphics_device, erhe::graphics::Buffer_target::storage,      "Id_renderer::m_scan_bitmask_buffer"}
    , m_scan_output_buffer {graphics_device, erhe::graphics::Buffer_target::storage,      "Id_renderer::m_scan_output_buffer" }
    , m_scan_params_buffer {graphics_device, erhe::graphics::Buffer_target::uniform,      "Id_renderer::m_scan_params_buffer" }
    , m_scan_result_buffer {graphics_device, erhe::graphics::Buffer_target::transfer_dst, "Id_renderer::m_scan_result_buffer" }
{
    enabled = id_renderer_config.enabled;
}

void Id_renderer::request_scan(const Scan_request& request)
{
    m_pending_scan = request;
}

auto Id_renderer::has_pending_scan() const -> bool
{
    return m_pending_scan.has_value();
}

Id_renderer::~Id_renderer() noexcept
{
}

auto Id_renderer::get_current_transfer_entry() -> Transfer_entry&
{
    return m_transfer_entries[m_current_transfer_entry_slot];
}

void Id_renderer::next_frame()
{
    if (!m_enabled) {
        return;
    }

    m_current_transfer_entry_slot = (m_current_transfer_entry_slot + 1) % s_transfer_entry_count;
}

void Id_renderer::update_framebuffer(const erhe::math::Viewport viewport)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_enabled) {
        return;
    }

    if (
        !m_color_texture ||
        (m_color_texture->get_width()  != viewport.width) ||
        (m_color_texture->get_height() != viewport.height)
    ) {
        // Order matters: the timer is registered with the render pass; it
        // must be destroyed before the render pass it points at.
        m_gpu_timer.reset();
        m_color_texture.reset();
        m_depth_texture.reset();
        m_render_pass.reset();
        m_color_texture = std::make_unique<Texture>(
            m_graphics_device,
            erhe::graphics::Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  =
                    erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                    erhe::graphics::Image_usage_flag_bit_mask::transfer_src,
                .type        = erhe::graphics::Texture_type::texture_2d,
                .pixelformat = erhe::dataformat::Format::format_8_vec4_unorm,
                .use_mipmaps = false,
                .width       = viewport.width,
                .height      = viewport.height,
                .debug_label = "ID Render color"
            }
        );
        m_depth_texture = std::make_unique<Texture>(
            m_graphics_device,
            erhe::graphics::Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  =
                    erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment |
                    erhe::graphics::Image_usage_flag_bit_mask::transfer_src,
                .type        = erhe::graphics::Texture_type::texture_2d,
                .pixelformat = erhe::dataformat::Format::format_d32_sfloat,
                .use_mipmaps = false,
                .width       = viewport.width,
                .height      = viewport.height,
                .debug_label = "ID Render depth"
            }
        );
        erhe::graphics::Render_pass_descriptor render_pass_descriptor;
        render_pass_descriptor.color_attachments[0].texture      = m_color_texture.get();
        render_pass_descriptor.color_attachments[0].load_action  = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.color_attachments[0].store_action = erhe::graphics::Store_action::Store;
        render_pass_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        render_pass_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        render_pass_descriptor.color_attachments[0].usage_after  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        render_pass_descriptor.color_attachments[0].layout_after = erhe::graphics::Image_layout::transfer_src_optimal;
        render_pass_descriptor.depth_attachment.texture          = m_depth_texture.get();
        render_pass_descriptor.depth_attachment.load_action      = erhe::graphics::Load_action::Clear;
        // Clear to the convention's far value (reverse-Z 0.0, forward-Z 1.0); the
        // default 0.0 is the near plane under forward-Z, so all geometry fails the
        // depth test and nothing is written to the ID buffer (picking stops working).
        render_pass_descriptor.depth_attachment.clear_value[0]   = m_graphics_device.get_reverse_depth() ? 0.0 : 1.0;
        render_pass_descriptor.depth_attachment.store_action     = erhe::graphics::Store_action::Store;
        render_pass_descriptor.depth_attachment.usage_before     = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        render_pass_descriptor.depth_attachment.layout_before    = erhe::graphics::Image_layout::transfer_src_optimal;
        render_pass_descriptor.depth_attachment.usage_after      = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        render_pass_descriptor.depth_attachment.layout_after     = erhe::graphics::Image_layout::transfer_src_optimal;
        render_pass_descriptor.render_target_width               = viewport.width;
        render_pass_descriptor.render_target_height              = viewport.height;
        render_pass_descriptor.debug_label                       = "ID";
        m_render_pass = std::make_unique<Render_pass>(m_graphics_device, render_pass_descriptor);
        m_gpu_timer   = std::make_unique<erhe::graphics::Gpu_timer>(*m_render_pass.get(), "Id_renderer");
        constexpr float clear_value[4] = {1.0f, 0.0f, 0.0f, 1.0f };

        const std::size_t color_image_size = s_extent * s_extent * erhe::dataformat::get_format_size_bytes(m_color_texture->get_pixelformat());
        const std::size_t depth_image_size = s_extent * s_extent * erhe::dataformat::get_format_size_bytes(m_depth_texture->get_pixelformat());
        for (Transfer_entry& entry : m_transfer_entries) {
            entry.data.resize(color_image_size + depth_image_size);
        }
    }
}

void Id_renderer::update_edge_id_framebuffer(const erhe::math::Viewport viewport)
{
    ERHE_PROFILE_FUNCTION();

    if (
        m_edge_id_color_texture &&
        (m_edge_id_color_texture->get_width()  == viewport.width) &&
        (m_edge_id_color_texture->get_height() == viewport.height)
    ) {
        return;
    }

    m_edge_id_color_texture.reset();
    m_edge_id_depth_texture.reset();
    m_edge_id_render_pass.reset();

    m_edge_id_color_texture = std::make_unique<Texture>(
        m_graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = m_graphics_device,
            .usage_mask  =
                erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                erhe::graphics::Image_usage_flag_bit_mask::sampled,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = erhe::dataformat::Format::format_8_vec4_unorm,
            .use_mipmaps = false,
            .width       = viewport.width,
            .height      = viewport.height,
            .debug_label = "Content edge ID color"
        }
    );
    m_edge_id_depth_texture = std::make_unique<Texture>(
        m_graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = m_graphics_device,
            .usage_mask  = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = erhe::dataformat::Format::format_d32_sfloat,
            .use_mipmaps = false,
            .width       = viewport.width,
            .height      = viewport.height,
            .debug_label = "Content edge ID depth"
        }
    );

    erhe::graphics::Render_pass_descriptor render_pass_descriptor;
    // Color: cleared to 0 (the background / "no edge" id), stored, and left in a
    // shader-read state so the polygon-fill pass can sample it in the same frame.
    render_pass_descriptor.color_attachments[0].texture       = m_edge_id_color_texture.get();
    render_pass_descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    render_pass_descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    render_pass_descriptor.color_attachments[0].clear_value   = {0.0, 0.0, 0.0, 0.0};
    render_pass_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::sampled;
    render_pass_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::shader_read_only_optimal;
    render_pass_descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::sampled;
    render_pass_descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::shader_read_only_optimal;
    // Depth: cleared to the convention's far value, used only to resolve which
    // edge wins per pixel, then discarded (store dont_care, never sampled).
    render_pass_descriptor.depth_attachment.texture           = m_edge_id_depth_texture.get();
    render_pass_descriptor.depth_attachment.load_action       = erhe::graphics::Load_action::Clear;
    render_pass_descriptor.depth_attachment.clear_value[0]    = m_graphics_device.get_reverse_depth() ? 0.0 : 1.0;
    render_pass_descriptor.depth_attachment.store_action      = erhe::graphics::Store_action::Dont_care;
    render_pass_descriptor.depth_attachment.usage_before      = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    render_pass_descriptor.depth_attachment.layout_before     = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    render_pass_descriptor.depth_attachment.usage_after       = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    render_pass_descriptor.depth_attachment.layout_after      = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    render_pass_descriptor.render_target_width                = viewport.width;
    render_pass_descriptor.render_target_height               = viewport.height;
    render_pass_descriptor.debug_label                        = "Content edge ID";
    m_edge_id_render_pass = std::make_unique<Render_pass>(m_graphics_device, render_pass_descriptor);
}

void Id_renderer::update_seed_framebuffer(const erhe::math::Viewport viewport)
{
    ERHE_PROFILE_FUNCTION();

    // Nearest sampler, created once (size-independent). The edge-id pass
    // texelFetches the seed by integer pixel coordinate, so filtering never
    // applies; a plain nearest sampler keeps the dedicated sampler descriptor
    // valid for the seed-mask graphics variant.
    if (!m_seed_sampler) {
        m_seed_sampler = std::make_unique<erhe::graphics::Sampler>(
            m_graphics_device,
            erhe::graphics::Sampler_create_info{
                .min_filter  = erhe::graphics::Filter::nearest,
                .mag_filter  = erhe::graphics::Filter::nearest,
                .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
                .debug_label = "Id_renderer seed sampler"
            }
        );
    }

    if (
        m_seed_color_texture &&
        (m_seed_color_texture->get_width()  == viewport.width) &&
        (m_seed_color_texture->get_height() == viewport.height)
    ) {
        return;
    }

    m_seed_color_texture.reset();
    m_seed_depth_texture.reset();
    m_seed_render_pass.reset();

    m_seed_color_texture = std::make_unique<Texture>(
        m_graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = m_graphics_device,
            .usage_mask  =
                erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                erhe::graphics::Image_usage_flag_bit_mask::sampled,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = erhe::dataformat::Format::format_8_vec4_unorm,
            .use_mipmaps = false,
            .width       = viewport.width,
            .height      = viewport.height,
            .debug_label = "Content seed face ID color"
        }
    );
    m_seed_depth_texture = std::make_unique<Texture>(
        m_graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = m_graphics_device,
            .usage_mask  = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = erhe::dataformat::Format::format_d32_sfloat,
            .use_mipmaps = false,
            .width       = viewport.width,
            .height      = viewport.height,
            .debug_label = "Content seed face ID depth"
        }
    );

    erhe::graphics::Render_pass_descriptor render_pass_descriptor;
    // Color: cleared to 0 (id 0 = "no visible face" / background, never a real
    // face id), stored, and left shader-readable so the edge-id pass can sample
    // it in the same frame (the post-pass barrier makes the writes visible).
    render_pass_descriptor.color_attachments[0].texture       = m_seed_color_texture.get();
    render_pass_descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    render_pass_descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    render_pass_descriptor.color_attachments[0].clear_value   = {0.0, 0.0, 0.0, 0.0};
    render_pass_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::sampled;
    render_pass_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::shader_read_only_optimal;
    render_pass_descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::sampled;
    render_pass_descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::shader_read_only_optimal;
    // Depth: cleared to the convention's far value, used only to keep the
    // frontmost face per pixel, then discarded (store dont_care, never sampled).
    render_pass_descriptor.depth_attachment.texture           = m_seed_depth_texture.get();
    render_pass_descriptor.depth_attachment.load_action       = erhe::graphics::Load_action::Clear;
    render_pass_descriptor.depth_attachment.clear_value[0]    = m_graphics_device.get_reverse_depth() ? 0.0 : 1.0;
    render_pass_descriptor.depth_attachment.store_action      = erhe::graphics::Store_action::Dont_care;
    render_pass_descriptor.depth_attachment.usage_before      = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    render_pass_descriptor.depth_attachment.layout_before     = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    render_pass_descriptor.depth_attachment.usage_after       = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    render_pass_descriptor.depth_attachment.layout_after      = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    render_pass_descriptor.render_target_width                = viewport.width;
    render_pass_descriptor.render_target_height               = viewport.height;
    render_pass_descriptor.debug_label                        = "Content seed face ID";
    m_seed_render_pass = std::make_unique<Render_pass>(m_graphics_device, render_pass_descriptor);
}

void Id_renderer::render_content_edge_id(
    erhe::graphics::Command_buffer&                   command_buffer,
    const erhe::math::Viewport&                       viewport,
    erhe::scene_renderer::Content_wide_line_renderer& wide_line_renderer
)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_enabled) {
        return;
    }
    if ((viewport.width == 0) || (viewport.height == 0)) {
        return;
    }

    update_edge_id_framebuffer(viewport);

    erhe::graphics::Scoped_debug_group debug_group{command_buffer, "Id_renderer::render_content_edge_id()"};

    erhe::graphics::Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(command_buffer);
    erhe::graphics::Scoped_render_pass scoped_render_pass{*m_edge_id_render_pass.get(), command_buffer};
    encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
    encoder.set_scissor_rect (viewport.x, viewport.y, viewport.width, viewport.height);

    // The wide-line renderer overrides its own shader stages / bind group layout
    // / topology; from the pipeline we hand it, it reads only depth_stencil +
    // multisample + rasterization + viewport_depth_range. m_pipeline already
    // enables the depth test (with depth writes), which is exactly what the
    // edge-id pass needs so a nearer edge wins per pixel. Blending is disabled so
    // the face id is written exactly. group 0: the id pass carries every content
    // edge (selection coloring is handled later by the fill composition passes).
    //
    // Pass the seed face-ID buffer (rendered by render_content_seed() earlier this
    // frame) + its sampler: the wide-line renderer draws with the seed-masked
    // variant, discarding any edge fragment whose own face id does not equal the
    // seed's frontmost-visible-face id there. So cap / overspray fragments landing
    // on another face or the background never write the edge-id color or win the
    // edge-id depth test -- the edge-id buffer is correct by construction.
    wide_line_renderer.render(
        encoder,
        m_pipeline,
        &erhe::graphics::Color_blend_state::color_blend_disabled,
        0u,
        false,
        m_seed_color_texture.get(),
        m_seed_sampler.get()
    );
}

auto Id_renderer::get_edge_id_texture() const -> erhe::graphics::Texture*
{
    return m_edge_id_color_texture.get();
}

auto Id_renderer::get_seed_id_texture() const -> erhe::graphics::Texture*
{
    return m_seed_color_texture.get();
}

auto Id_renderer::get_seed_sampler() const -> const erhe::graphics::Sampler*
{
    return m_seed_sampler.get();
}

void Id_renderer::render_content_seed(const Seed_render_parameters& parameters)
{
    using namespace erhe::graphics;

    ERHE_PROFILE_FUNCTION();

    if (!m_enabled) {
        return;
    }
    const erhe::math::Viewport& viewport = parameters.viewport;
    if ((viewport.width == 0) || (viewport.height == 0)) {
        return;
    }

    update_seed_framebuffer(viewport);

    Scoped_debug_group debug_group{parameters.command_buffer, "Id_renderer::render_content_seed()"};

    // Same camera UBO the picking pass uses; update() acquires a disjoint ring
    // range, so calling it again this frame is fine.
    Ring_buffer_range camera_range = m_camera_buffers.update(
        *parameters.camera.projection(),
        *parameters.camera.get_node(),
        viewport,
        1.0f,
        erhe::scene_renderer::Grid_parameters{}, // unused by the seed shader
        erhe::scene_renderer::Sky_parameters{},  // unused by the seed shader
        0,
        parameters.reverse_depth,
        parameters.depth_range,
        parameters.conventions // match the fill's clip_from_world (y-flip etc.)
    );

    // Joint UBO/SSBO so skinned content is posed in the seed exactly as in the
    // fill (otherwise the seed surface would not match for skinned meshes).
    Ring_buffer_range joint_range{};
    const bool        bind_joint_buffer = (parameters.joint_buffer != nullptr);
    if (bind_joint_buffer) {
        joint_range = parameters.joint_buffer->update(
            glm::uvec4{0u, 0u, 0u, 0u},
            {},
            parameters.skins
        );
    }

    {
        Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(parameters.command_buffer);
        erhe::graphics::Scoped_render_pass scoped_render_pass{*m_seed_render_pass.get(), parameters.command_buffer};
        encoder.set_bind_group_layout(m_bind_group_layout);
        m_camera_buffers.bind(encoder, camera_range);
        if (bind_joint_buffer) {
            parameters.joint_buffer->bind(encoder, joint_range);
        }
        // Full viewport (no pointer-rect scissor): the seed must cover every pixel
        // an edge could touch, not just the picking readback rect.
        encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
        encoder.set_scissor_rect (viewport.x, viewport.y, viewport.width, viewport.height);

        // NOTE: deliberately NOT reset_id_ranges() here -- the picking pass
        // populated m_primitive_buffers.id_ranges() earlier this frame and its
        // async readback still needs them. The seed uses use_id_ranges = false
        // (and the face-id-base provider for color), so update() touches neither
        // m_id_ranges nor the running id_offset count.
        const erhe::Item_filter content_filter{
            .require_all_bits_set           = erhe::Item_flags::visible,
            .require_at_least_one_bit_set   = 0u,
            .require_all_bits_clear         = 0u,
            .require_at_least_one_bit_clear = 0u
        };
        const erhe::scene_renderer::Primitive_interface_settings primitive_settings{
            .face_id_base_provider = parameters.face_id_base_provider
        };

        render_buckets(
            encoder,
            m_pipeline,
            *m_seed_render_pass.get(),
            primitive_settings,
            erhe::scene_renderer::make_shader_bool_mask(erhe::scene_renderer::Shader_bool::VARIANT_FACE_ID_SEED),
            false, // use_id_ranges: seed color is sampled in-frame, never read back
            content_filter,
            parameters.meshes
        );
    }

    camera_range.release();
    if (bind_joint_buffer) {
        joint_range.release();
    }
}

void Id_renderer::render_meshes(
    const Render_parameters&                                   parameters,
    erhe::graphics::Render_command_encoder&                    render_encoder,
    erhe::graphics::Base_render_pipeline&                      pipeline,
    const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes
)
{
    ERHE_PROFILE_FUNCTION();

    const erhe::Item_filter id_filter{
        .require_all_bits_set           = erhe::Item_flags::visible | erhe::Item_flags::id,
        .require_at_least_one_bit_set   = 0u,
        .require_all_bits_clear         = 0u,
        .require_at_least_one_bit_clear = 0u
    };

    const erhe::scene_renderer::Primitive_interface_settings primitive_settings{
        .color_source = erhe::scene_renderer::Primitive_color_source::id_offset
    };

    // Pre-filter the input mesh span when the caller asked for skinned-only
    // (the hybrid picker delegates static meshes to the raytrace path).
    // A pointer-vector copy is cheap; doing it here keeps the
    // bucket_primitives() / Item_filter API surface unchanged.
    std::vector<std::shared_ptr<erhe::scene::Mesh>> filtered_meshes;
    std::span<const std::shared_ptr<erhe::scene::Mesh>> meshes_to_render = meshes;
    if (parameters.skinning_filter == Skinning_filter::skinned_only) {
        filtered_meshes.reserve(meshes.size());
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
            if (mesh && mesh->skin) {
                filtered_meshes.push_back(mesh);
            }
        }
        meshes_to_render = filtered_meshes;
    }
    if (meshes_to_render.empty()) {
        return;
    }

    render_buckets(
        render_encoder,
        pipeline,
        *m_render_pass.get(),
        primitive_settings,
        erhe::scene_renderer::make_shader_bool_mask(erhe::scene_renderer::Shader_bool::VARIANT_ID_RENDER),
        true, // use_id_ranges: record the id_offset->mesh table the readback walks
        id_filter,
        meshes_to_render
    );
}

void Id_renderer::render_buckets(
    erhe::graphics::Render_command_encoder&                    render_encoder,
    erhe::graphics::Base_render_pipeline&                      pipeline,
    const erhe::graphics::Render_pass&                         render_pass,
    const erhe::scene_renderer::Primitive_interface_settings&  primitive_settings,
    const uint32_t                                             force_enable_mask,
    const bool                                                 use_id_ranges,
    const erhe::Item_filter&                                   filter,
    const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes
)
{
    ERHE_PROFILE_FUNCTION();

    if (meshes.empty()) {
        return;
    }

    using namespace erhe::scene_renderer;
    const erhe::primitive::Primitive_mode primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
    std::vector<Render_bucket> buckets;
    const uint32_t boolean_mask_force_disable = 0; // TODO: Maybe disable some features for ID rendering?
    bucket_primitives(
        buckets,
        force_enable_mask,
        boolean_mask_force_disable,
        m_mesh_memory,
        Shader_key{},
        meshes,
        filter,
        primitive_mode,
        Blending_mode_policy::override_with_base_render_pipeline // TODO
    );

    for (std::size_t bucket_index = 0, end = buckets.size(); bucket_index < end; ++bucket_index) {
        const Render_bucket&      bucket       = buckets[bucket_index];
        const Vertex_input_entry& vertex_input = m_mesh_memory.get_vertex_input(bucket.buffer_set.vertex_input_key);

        ERHE_VERIFY(!bucket.entries.empty());

        const erhe::graphics::Reloadable_shader_stages* reloadable_shader_stages = m_shader_variant_cache.get(
            bucket.shader_key,
            &vertex_input.vertex_format
        );
        if (reloadable_shader_stages == nullptr) {
            log_draw->warn(
                "No shader variant for bucket {} ({} primitives, {} vertex streams): {}",
                bucket_index,
                bucket.entries.size(),
                bucket.buffer_set.vertex_buffers.size(),
                bucket.shader_key.describe()
            );
            continue;
        }
        const erhe::graphics::Shader_stages* shader_stages = &reloadable_shader_stages->shader_stages;

        // The pipeline format must match the supplied render pass (the picking
        // pass for render_meshes, the seed pass for render_content_seed); the
        // caller has no knowledge of these internal framebuffers. Mirrored
        // (negative determinant) buckets get the front-face-flipped variant so
        // back-face culling removes the same faces the forward pass culls.
        erhe::graphics::Render_pipeline* render_pipeline = pipeline.get_pipeline_for(
            render_pass.get_descriptor(),
            nullptr,
            shader_stages,
            vertex_input.vertex_input.get(),
            &vertex_input.vertex_format,
            bucket.negative_determinant
        );
        if (render_pipeline == nullptr) {
            log_draw->warn(
                "No render pipeline for bucket {} ({} primitives, {} vertex streams): {}",
                bucket_index,
                bucket.entries.size(),
                bucket.buffer_set.vertex_buffers.size(),
                bucket.shader_key.describe()
            );
            continue;
        }

        erhe::graphics::Scoped_debug_group bucket_scope{
            render_encoder.get_command_buffer(),
            erhe::utility::Debug_label{
                fmt::format(
                    "bucket {}/{} prims={} streams={} {}",
                    bucket_index,
                    buckets.size(),
                    bucket.entries.size(),
                    bucket.buffer_set.vertex_buffers.size(),
                    bucket.shader_key.describe()
                )
            }
        };

        // Bind the resolved pipeline and the bucket's geometry before the
        // draw. Without these the encoder has no program / vertex input /
        // index buffer bound and records no actual draw (the bucket shows
        // up in a capture but produces zero draw calls). Mirrors
        // Forward_renderer::render().
        render_encoder.set_render_pipeline(*render_pipeline);

        erhe::graphics::Buffer* index_buffer = m_mesh_memory.get_index_buffer(bucket.buffer_set.index_buffer);
        render_encoder.set_index_buffer(index_buffer);
        for (std::size_t stream_index = 0; stream_index < bucket.buffer_set.vertex_buffers.size(); ++stream_index) {
            erhe::graphics::Buffer* vertex_buffer = m_mesh_memory.get_vertex_buffer(bucket.buffer_set.vertex_buffers[stream_index]);
            render_encoder.set_vertex_buffer(
                vertex_buffer,
                0,
                static_cast<uint32_t>(stream_index)
            );
        }

        // use_id_ranges = true records the (id_offset .. id_offset+count,
        // mesh, primitive) table that Id_renderer::get() walks to turn the
        // GPU-written id back into a (mesh, primitive, triangle) hit (picking).
        // The seed pass passes false: its color is sampled in-frame, not read back.
        erhe::graphics::Ring_buffer_range primitive_range = m_primitive_buffers.update(bucket, primitive_mode, primitive_settings, use_id_ranges);
        erhe::scene_renderer::Draw_indirect_buffer_range draw_indirect_buffer_range = m_draw_indirect_buffers.update(bucket, primitive_mode);
        ERHE_VERIFY(draw_indirect_buffer_range.draw_indirect_count == bucket.entries.size());

        m_primitive_buffers    .bind(render_encoder, primitive_range);
        m_draw_indirect_buffers.bind(render_encoder, draw_indirect_buffer_range.range);
        render_encoder.multi_draw_indexed_primitives_indirect(
            pipeline.data.input_assembly.primitive_topology,
            m_mesh_memory.get_index_format(bucket.buffer_set.index_buffer),
            draw_indirect_buffer_range.range.get_byte_start_offset_in_buffer(),
            draw_indirect_buffer_range.draw_indirect_count,
            sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command)
        );

        primitive_range.release();
        draw_indirect_buffer_range.range.release();
    }
}

void Id_renderer::ensure_scan_compute()
{
    if (m_scan_compute_attempted) {
        return;
    }
    m_scan_compute_attempted = true;

    // The two scan passes are SSBO-backed compute. On devices without compute
    // shaders or shader storage buffers (e.g. macOS OpenGL 4.1)
    // m_scan_compute_available stays false and the CPU region-scan path is used.
    const erhe::graphics::Device_info& info = m_graphics_device.get_info();
    if (!info.use_compute_shader || !info.use_shader_storage_buffers) {
        return;
    }

    using erhe::graphics::Shader_resource;

    constexpr int c_input_binding   = 0;
    constexpr int c_bitmask_binding = 1;
    constexpr int c_output_binding  = 2;
    constexpr int c_params_binding  = 3;

    // Binding 0: region pixel buffer (readonly SSBO). One uint per rgba8 texel
    // (std430 => tight 4-byte stride, matching the texture->buffer blit layout).
    m_scan_input_block = std::make_unique<Shader_resource>(
        m_graphics_device,
        Shader_resource::Block_create_info{
            .name          = "id_scan_input",
            .binding_point = c_input_binding,
            .type          = Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    );
    m_scan_input_block->add_uint("pixels", Shader_resource::unsized_array);

    // Binding 1: dedup bitmask (read-write SSBO). One bit per id; atomicOr in pass 1.
    m_scan_bitmask_block = std::make_unique<Shader_resource>(
        m_graphics_device,
        Shader_resource::Block_create_info{
            .name          = "id_scan_bitmask",
            .binding_point = c_bitmask_binding,
            .type          = Shader_resource::Type::shader_storage_block
        }
    );
    m_scan_bitmask_block->add_uint("words", Shader_resource::unsized_array);

    // Binding 2: compacted output (read-write SSBO): { uint count; uint ids[]; }.
    m_scan_output_block = std::make_unique<Shader_resource>(
        m_graphics_device,
        Shader_resource::Block_create_info{
            .name          = "id_scan_output",
            .binding_point = c_output_binding,
            .type          = Shader_resource::Type::shader_storage_block
        }
    );
    m_scan_output_block->add_uint("count");
    m_scan_output_block->add_uint("ids", Shader_resource::unsized_array);

    // Binding 3: scan params (UBO). All scalar uint/float fields. Capture each
    // field's std140 offset so submit_scan_compute() writes them without
    // hardcoding the layout.
    m_scan_params_block = std::make_unique<Shader_resource>(
        m_graphics_device,
        "id_scan_params",
        c_params_binding,
        Shader_resource::Type::uniform_block
    );
    m_scan_param_offsets.width              = m_scan_params_block->add_uint ("width"             )->get_offset_in_parent();
    m_scan_param_offsets.height             = m_scan_params_block->add_uint ("height"            )->get_offset_in_parent();
    m_scan_param_offsets.pixel_count        = m_scan_params_block->add_uint ("pixel_count"       )->get_offset_in_parent();
    m_scan_param_offsets.row_stride_uints   = m_scan_params_block->add_uint ("row_stride_uints"  )->get_offset_in_parent();
    m_scan_param_offsets.region_x           = m_scan_params_block->add_uint ("region_x"          )->get_offset_in_parent();
    m_scan_param_offsets.region_y           = m_scan_params_block->add_uint ("region_y"          )->get_offset_in_parent();
    m_scan_param_offsets.is_brush           = m_scan_params_block->add_uint ("is_brush"          )->get_offset_in_parent();
    m_scan_param_offsets.brush_center_x     = m_scan_params_block->add_float("brush_center_x"    )->get_offset_in_parent();
    m_scan_param_offsets.brush_center_y     = m_scan_params_block->add_float("brush_center_y"    )->get_offset_in_parent();
    m_scan_param_offsets.brush_radius       = m_scan_params_block->add_float("brush_radius"      )->get_offset_in_parent();
    m_scan_param_offsets.max_id             = m_scan_params_block->add_uint ("max_id"            )->get_offset_in_parent();
    m_scan_param_offsets.bitmask_word_count = m_scan_params_block->add_uint ("bitmask_word_count")->get_offset_in_parent();
    m_scan_param_offsets.max_output         = m_scan_params_block->add_uint ("max_output"        )->get_offset_in_parent();

    using erhe::graphics::Bind_group_layout;
    using erhe::graphics::Bind_group_layout_binding;
    using erhe::graphics::Bind_group_layout_create_info;
    using erhe::graphics::Binding_type;
    using erhe::graphics::Shader_stage_flags;

    m_scan_gather_bind_group_layout = std::make_unique<Bind_group_layout>(
        m_graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                Bind_group_layout_binding{.binding_point = c_input_binding,   .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute},
                Bind_group_layout_binding{.binding_point = c_bitmask_binding, .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute},
                Bind_group_layout_binding{.binding_point = c_params_binding,  .type = Binding_type::uniform_buffer, .stage_flags = Shader_stage_flags::compute}
            },
            .debug_label       = "Id_renderer scan gather",
            .uses_texture_heap = false
        }
    );
    m_scan_compact_bind_group_layout = std::make_unique<Bind_group_layout>(
        m_graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                Bind_group_layout_binding{.binding_point = c_bitmask_binding, .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute},
                Bind_group_layout_binding{.binding_point = c_output_binding,  .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute},
                Bind_group_layout_binding{.binding_point = c_params_binding,  .type = Binding_type::uniform_buffer, .stage_flags = Shader_stage_flags::compute}
            },
            .debug_label       = "Id_renderer scan compact",
            .uses_texture_heap = false
        }
    );

    const std::filesystem::path shader_path = std::filesystem::path{"res"} / std::filesystem::path{"shaders"};

    // Pass 1 (gather): reads id_scan_input, atomicOrs into id_scan_bitmask, reads id_scan_params.
    {
        erhe::graphics::Shader_stages_create_info create_info{
            .name             = "id_scan_gather",
            .interface_blocks = {
                m_scan_input_block.get(),
                m_scan_bitmask_block.get(),
                m_scan_params_block.get()
            },
            .shaders           = { { erhe::graphics::Shader_type::compute_shader, shader_path / "id_scan_gather.comp" } },
            .bind_group_layout = m_scan_gather_bind_group_layout.get()
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(m_graphics_device, std::move(create_info));
        if (!prototype.is_valid()) {
            return;
        }
        m_scan_gather_stages = std::make_unique<erhe::graphics::Shader_stages>(m_graphics_device, std::move(prototype));
    }

    // Pass 2 (compact): reads id_scan_bitmask, atomicAdds + writes id_scan_output, reads id_scan_params.
    {
        erhe::graphics::Shader_stages_create_info create_info{
            .name             = "id_scan_compact",
            .interface_blocks = {
                m_scan_bitmask_block.get(),
                m_scan_output_block.get(),
                m_scan_params_block.get()
            },
            .shaders           = { { erhe::graphics::Shader_type::compute_shader, shader_path / "id_scan_compact.comp" } },
            .bind_group_layout = m_scan_compact_bind_group_layout.get()
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(m_graphics_device, std::move(create_info));
        if (!prototype.is_valid()) {
            return;
        }
        m_scan_compact_stages = std::make_unique<erhe::graphics::Shader_stages>(m_graphics_device, std::move(prototype));
    }

    m_scan_gather_pipeline.emplace(
        m_graphics_device,
        erhe::graphics::Compute_pipeline_data{
            .name              = "id_scan_gather",
            .shader_stages     = m_scan_gather_stages.get(),
            .bind_group_layout = m_scan_gather_bind_group_layout.get()
        }
    );
    m_scan_compact_pipeline.emplace(
        m_graphics_device,
        erhe::graphics::Compute_pipeline_data{
            .name              = "id_scan_compact",
            .shader_stages     = m_scan_compact_stages.get(),
            .bind_group_layout = m_scan_compact_bind_group_layout.get()
        }
    );

    m_scan_compute_available =
        m_scan_gather_pipeline.has_value()  && m_scan_gather_pipeline->is_valid() &&
        m_scan_compact_pipeline.has_value() && m_scan_compact_pipeline->is_valid();
}

auto Id_renderer::submit_scan_compute(
    erhe::graphics::Command_buffer& command_buffer,
    Region_entry&                   region,
    const int scan_x, const int scan_y, const int scan_w, const int scan_h
) -> bool
{
    if (!m_scan_compute_available) {
        return false;
    }

    // max_id (exclusive id upper bound) and max_output (total facet count, the
    // distinct-id upper bound) from this scan's id-range snapshot.
    uint32_t max_id     = 0;
    uint32_t max_output = 0;
    for (const erhe::scene_renderer::Primitive_buffer::Id_range& range : region.id_ranges) {
        const uint32_t range_end = static_cast<uint32_t>(range.offset + range.length);
        max_id      = std::max(max_id, range_end);
        max_output += static_cast<uint32_t>(range.length);
    }
    if ((max_id == 0) || (max_output == 0)) {
        return false; // nothing drawn this scan; let the (empty) CPU path handle it
    }
    const uint32_t bitmask_word_count = (max_id + 31u) / 32u;

    const std::size_t bytes_per_pixel  = erhe::dataformat::get_format_size_bytes(m_color_texture->get_pixelformat());
    // 256-byte-aligned destination row stride, same constraint as the CPU
    // texture->buffer blit; padding uints past scan_w are never read.
    const std::size_t row_stride_bytes = (((static_cast<std::size_t>(scan_w) * bytes_per_pixel) + 255u) / 256u) * 256u;
    const std::size_t input_bytes      = row_stride_bytes * static_cast<std::size_t>(scan_h);
    const std::size_t row_stride_uints = row_stride_bytes / 4u;
    const std::size_t bitmask_bytes    = static_cast<std::size_t>(bitmask_word_count) * 4u;
    const std::size_t output_bytes     = (static_cast<std::size_t>(max_output) + 1u) * 4u; // count + ids
    const std::size_t params_bytes     = m_scan_params_block->get_size_bytes();

    region.used_compute = true;
    region.max_output   = max_output;
    region.x            = scan_x;
    region.y            = scan_y;
    region.width        = scan_w;
    region.height       = scan_h;
    region.row_stride   = static_cast<int>(row_stride_bytes);
    region.frame_number = m_graphics_device.get_frame_index();

    erhe::graphics::Ring_buffer_range input_range   = m_scan_input_buffer.acquire  (erhe::graphics::Ring_buffer_usage::GPU_access, input_bytes);
    erhe::graphics::Ring_buffer_range bitmask_range = m_scan_bitmask_buffer.acquire(erhe::graphics::Ring_buffer_usage::GPU_access, bitmask_bytes);
    erhe::graphics::Ring_buffer_range output_range  = m_scan_output_buffer.acquire (erhe::graphics::Ring_buffer_usage::GPU_access, output_bytes);
    erhe::graphics::Ring_buffer_range params_range  = m_scan_params_buffer.acquire (erhe::graphics::Ring_buffer_usage::CPU_write,  params_bytes);
    region.compact_range = m_scan_result_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_read, output_bytes);
    region.compact_data.resize(output_bytes);

    // Write the params UBO.
    {
        std::span<std::byte> p = params_range.get_span();
        const auto write_u = [&](const std::size_t off, const uint32_t v) { memcpy(p.data() + off, &v, sizeof(v)); };
        const auto write_f = [&](const std::size_t off, const float    v) { memcpy(p.data() + off, &v, sizeof(v)); };
        write_u(m_scan_param_offsets.width,              static_cast<uint32_t>(scan_w));
        write_u(m_scan_param_offsets.height,             static_cast<uint32_t>(scan_h));
        write_u(m_scan_param_offsets.pixel_count,        static_cast<uint32_t>(scan_w) * static_cast<uint32_t>(scan_h));
        write_u(m_scan_param_offsets.row_stride_uints,   static_cast<uint32_t>(row_stride_uints));
        write_u(m_scan_param_offsets.region_x,           static_cast<uint32_t>(scan_x));
        write_u(m_scan_param_offsets.region_y,           static_cast<uint32_t>(scan_y));
        write_u(m_scan_param_offsets.is_brush,           region.is_brush ? 1u : 0u);
        write_f(m_scan_param_offsets.brush_center_x,     region.brush_center.x);
        write_f(m_scan_param_offsets.brush_center_y,     region.brush_center.y);
        write_f(m_scan_param_offsets.brush_radius,       region.brush_radius);
        write_u(m_scan_param_offsets.max_id,             max_id);
        write_u(m_scan_param_offsets.bitmask_word_count, bitmask_word_count);
        write_u(m_scan_param_offsets.max_output,         max_output);
        params_range.bytes_written(params_bytes);
        params_range.close();
    }

    input_range.bytes_gpu_used(input_bytes);
    input_range.close();
    bitmask_range.bytes_gpu_used(bitmask_bytes);
    bitmask_range.close();
    output_range.bytes_gpu_used(output_bytes);
    output_range.close();

    const erhe::graphics::Buffer* input_buf   = input_range.get_buffer()->get_buffer();
    const erhe::graphics::Buffer* bitmask_buf = bitmask_range.get_buffer()->get_buffer();
    const erhe::graphics::Buffer* output_buf  = output_range.get_buffer()->get_buffer();
    const erhe::graphics::Buffer* params_buf  = params_range.get_buffer()->get_buffer();
    const erhe::graphics::Buffer* result_buf  = region.compact_range.get_buffer()->get_buffer();
    const std::uintptr_t input_off   = input_range.get_byte_start_offset_in_buffer();
    const std::uintptr_t bitmask_off = bitmask_range.get_byte_start_offset_in_buffer();
    const std::uintptr_t output_off  = output_range.get_byte_start_offset_in_buffer();
    const std::uintptr_t params_off  = params_range.get_byte_start_offset_in_buffer();
    const std::uintptr_t result_off  = region.compact_range.get_byte_start_offset_in_buffer();

    constexpr int      c_input_binding   = 0;
    constexpr int      c_bitmask_binding = 1;
    constexpr int      c_output_binding  = 2;
    constexpr int      c_params_binding  = 3;
    constexpr uint32_t c_local_size      = 64u; // matches layout(local_size_x = 64) in both .comp

    // 1) Blit the scan region into the input SSBO; clear bitmask + output to 0.
    {
        erhe::graphics::Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(
            m_color_texture.get(), 0, 0,
            glm::ivec3{scan_x, scan_y, 0},
            glm::ivec3{scan_w, scan_h, 1},
            input_buf, input_off, row_stride_bytes, input_bytes
        );
        blit.fill_buffer(bitmask_buf, bitmask_off, bitmask_bytes, 0);
        blit.fill_buffer(output_buf,  output_off,  output_bytes,  0);
    }

    // Transfer writes (blit + fills) -> compute SSBO reads/writes.
    command_buffer.memory_barrier(erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit);

    // 2) Pass 1 (gather): one thread per pixel.
    {
        erhe::graphics::Compute_command_encoder enc = m_graphics_device.make_compute_command_encoder(command_buffer);
        enc.set_bind_group_layout(m_scan_gather_bind_group_layout.get());
        enc.set_compute_pipeline(m_scan_gather_pipeline.value());
        enc.set_buffer(erhe::graphics::Buffer_target::storage, input_buf,   input_off,   input_bytes,   c_input_binding);
        enc.set_buffer(erhe::graphics::Buffer_target::storage, bitmask_buf, bitmask_off, bitmask_bytes, c_bitmask_binding);
        enc.set_buffer(erhe::graphics::Buffer_target::uniform, params_buf,  params_off,  params_bytes,  c_params_binding);
        const uint32_t pixel_count = static_cast<uint32_t>(scan_w) * static_cast<uint32_t>(scan_h);
        const uint32_t groups      = (pixel_count + c_local_size - 1u) / c_local_size;
        enc.dispatch_compute(groups, 1, 1);
    }

    // Pass 1 bitmask writes -> pass 2 bitmask reads.
    command_buffer.memory_barrier(erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit);

    // 3) Pass 2 (compact): one thread per bitmask word.
    {
        erhe::graphics::Compute_command_encoder enc = m_graphics_device.make_compute_command_encoder(command_buffer);
        enc.set_bind_group_layout(m_scan_compact_bind_group_layout.get());
        enc.set_compute_pipeline(m_scan_compact_pipeline.value());
        enc.set_buffer(erhe::graphics::Buffer_target::storage, bitmask_buf, bitmask_off, bitmask_bytes, c_bitmask_binding);
        enc.set_buffer(erhe::graphics::Buffer_target::storage, output_buf,  output_off,  output_bytes,  c_output_binding);
        enc.set_buffer(erhe::graphics::Buffer_target::uniform, params_buf,  params_off,  params_bytes,  c_params_binding);
        const uint32_t groups = (bitmask_word_count + c_local_size - 1u) / c_local_size;
        enc.dispatch_compute(groups, 1, 1);
    }

    // Pass 2 output writes -> transfer read (the copy below).
    command_buffer.memory_barrier(erhe::graphics::Memory_barrier_mask::pixel_buffer_barrier_bit);

    // 4) Copy the compacted { count, ids } into the CPU-read result buffer.
    {
        erhe::graphics::Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_buffer(output_buf, output_off, result_buf, result_off, output_bytes);
    }

    region.state = Transfer_entry::State::Waiting_for_read;
    m_graphics_device.add_completion_handler(
        [&region]() {
            std::span<std::byte> gpu_data = region.compact_range.get_span();
            memcpy(region.compact_data.data(), gpu_data.data(), gpu_data.size_bytes());
            region.compact_range.bytes_gpu_used(gpu_data.size_bytes());
            region.compact_range.close();
            region.compact_range.release();
            region.state = Transfer_entry::State::Read_complete;
        }
    );

    // The GPU_access / CPU_write ranges are returned to their rings now; ring
    // frame-fencing keeps the backing memory alive until the GPU finishes this
    // frame's dispatches (same pattern as the camera/joint ranges in render()).
    input_range.release();
    bitmask_range.release();
    output_range.release();
    params_range.release();

    return true;
}

void Id_renderer::render(const Render_parameters& parameters)
{
    using namespace erhe::graphics;

    ERHE_PROFILE_FUNCTION();

    if (!m_enabled) {
        return;
    }

    const auto& viewport           = parameters.viewport;
    const auto& camera             = parameters.camera;
    const auto& content_mesh_spans = parameters.content_mesh_spans;
    const auto& tool_mesh_spans    = parameters.tool_mesh_spans;
    const auto  x                  = parameters.x;
    const auto  y                  = parameters.y;

    m_ranges.clear();

    if ((viewport.width == 0) || (viewport.height == 0)) {
        return;
    }

    update_framebuffer(viewport);

    const std::size_t color_image_size_bytes = s_extent * s_extent * erhe::dataformat::get_format_size_bytes(m_color_texture->get_pixelformat());
    const std::size_t depth_image_size_bytes = s_extent * s_extent * erhe::dataformat::get_format_size_bytes(m_depth_texture->get_pixelformat());

    Scoped_debug_group debug_group{parameters.command_buffer, "Id_renderer::render()"};

    const auto projection_transforms = camera.projection_transforms(viewport, parameters.reverse_depth, parameters.depth_range, parameters.conventions);
    const mat4 clip_from_world       = projection_transforms.clip_from_world.get_matrix();

    Transfer_entry& entry = get_current_transfer_entry();
    if (entry.state == Transfer_entry::State::Waiting_for_read) {
        log_id_render->warn("Id_renderer::render(): Transfer_entry slot busy");
        return;
    }
    // Keep the s_extent readback rect fully inside the framebuffer. The low end is clamped to 0
    // (pointer left/above the viewport); the high end must be clamped too. Otherwise a pointer to
    // the right/below the viewport (e.g. a gizmo drag carried past the window edge) pushes the
    // offset past the framebuffer and the copy region below computes a negative extent -> invalid
    // GPU copy -> VK_ERROR_DEVICE_LOST. get() still validates the query against this rect, so a
    // pointer outside the rect correctly returns "no hit".
    const int half_extent  = static_cast<int>(s_extent / 2);
    const int max_x_offset = std::max(static_cast<int>(viewport.width)  - static_cast<int>(s_extent), 0);
    const int max_y_offset = std::max(static_cast<int>(viewport.height) - static_cast<int>(s_extent), 0);
    entry.x_offset        = std::clamp(x - half_extent, 0, max_x_offset);
    entry.y_offset        = std::clamp(y - half_extent, 0, max_y_offset);
    // Size of the readback rect that actually lies inside the framebuffer. Equals s_extent in the
    // common case, but shrinks to the viewport when the viewport is smaller than s_extent (so the
    // scissor and the GPU copy below never exceed the framebuffer). Both are >= 1 here because the
    // width == 0 / height == 0 viewport is rejected above.
    const int read_width  = std::min(static_cast<int>(s_extent), static_cast<int>(viewport.width)  - entry.x_offset);
    const int read_height = std::min(static_cast<int>(s_extent), static_cast<int>(viewport.height) - entry.y_offset);
    entry.clip_from_world = clip_from_world;

    // Region selection scan (box / paint): if a gesture requested a scan this
    // frame, clamp its rectangle to the viewport (same discipline as the pointer
    // rect above -- an unclamped rect yields a negative copy extent ->
    // VK_ERROR_DEVICE_LOST) and pick a free region slot. The scissor below then
    // covers the union of the pointer rect and the scan rect (so the per-frame
    // hover pick keeps working during a drag), and the transfer block blits the
    // scan rect. If all region slots are busy the scan is skipped this frame; the
    // gesture re-requests next frame.
    int  scan_x      = 0;
    int  scan_y      = 0;
    int  scan_w      = 0;
    int  scan_h      = 0;
    int  region_slot = -1;
    bool do_scan     = false;
    if (m_pending_scan.has_value()) {
        const Scan_request& req = m_pending_scan.value();
        const int rx0 = std::clamp(req.x,              0, static_cast<int>(viewport.width));
        const int ry0 = std::clamp(req.y,              0, static_cast<int>(viewport.height));
        const int rx1 = std::clamp(req.x + req.width,  0, static_cast<int>(viewport.width));
        const int ry1 = std::clamp(req.y + req.height, 0, static_cast<int>(viewport.height));
        scan_x = rx0;
        scan_y = ry0;
        scan_w = rx1 - rx0;
        scan_h = ry1 - ry0;
        for (int i = 0; i < s_region_entry_count; ++i) {
            if (m_region_entries[i].state == Transfer_entry::State::Unused) {
                region_slot = i;
                break;
            }
        }
        do_scan = (scan_w > 0) && (scan_h > 0) && (region_slot >= 0);
    }

    // log_id_render->info(
    //     "render(): pointer=({},{}) offset=({},{}) slot={} frame={} viewport={}x{}",
    //     x, y, entry.x_offset, entry.y_offset,
    //     m_current_transfer_entry_slot, m_graphics_device.get_frame_index(),
    //     viewport.width, viewport.height
    // );

    Ring_buffer_range camera_range = m_camera_buffers.update(
        *camera.projection(),
        *camera.get_node(),
        viewport,
        1.0f,
        erhe::scene_renderer::Grid_parameters{}, // unused by id pass shaders
        erhe::scene_renderer::Sky_parameters{},  // unused by id pass shaders
        0,
        parameters.reverse_depth,
        parameters.depth_range
    );

    // Joint UBO/SSBO upload. Required for the ID variant of standard.vert
    // when any rasterized mesh is skinned, because the shader takes the
    // GPU skinning branch off the per-primitive `skinning_factor`. Static
    // scenes pass joint_buffer = nullptr and the bind is skipped. Allocates
    // a disjoint ring range from the one Forward_renderer's later
    // render() will request, the same way Content_wide_line_renderer does
    // (see Viewport_scene_view::execute_rendergraph_node).
    Ring_buffer_range joint_range{};
    const bool        bind_joint_buffer = (parameters.joint_buffer != nullptr);
    if (bind_joint_buffer) {
        joint_range = parameters.joint_buffer->update(
            glm::uvec4{0u, 0u, 0u, 0u},
            {},
            parameters.skins
        );
    }

    // Render
    {
        Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(parameters.command_buffer);
        erhe::graphics::Scoped_render_pass scoped_render_pass{*m_render_pass.get(), parameters.command_buffer};
        // The bind group layout (Vulkan pipeline layout) must be set before
        // any buffer bind; set_buffer() asserts on it. On OpenGL this is a
        // no-op. Mirrors Forward_renderer::render().
        encoder.set_bind_group_layout(m_bind_group_layout);
        m_camera_buffers.bind(encoder, camera_range);
        if (bind_joint_buffer) {
            parameters.joint_buffer->bind(encoder, joint_range);
        }

        // Viewport AND scissor are dynamic state that must be set before
        // the first draw (Vulkan leaves them undefined otherwise). The
        // ID framebuffer is the full viewport size, so the geometry must
        // be transformed through the full viewport to match the on-screen
        // image; the scissor is the only thing that restricts rasterization
        // to the pointer rect. This mirrors Forward_renderer::render(),
        // which always sets both. The content loop previously set neither,
        // so it rendered with stale/undefined viewport state.
        encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
        if (m_use_scissor) {
            // Optimization: only rasterize the readback rect around the
            // pointer (the only region the readback blit copies). Use the
            // framebuffer-clamped extent so a viewport smaller than s_extent
            // does not produce a scissor larger than the framebuffer. When a
            // region scan is active this frame, widen the scissor to also cover
            // the scan rect so its facets are rasterized for the scan blit.
            if (do_scan) {
                const int ux0 = std::min(entry.x_offset, scan_x);
                const int uy0 = std::min(entry.y_offset, scan_y);
                const int ux1 = std::max(entry.x_offset + read_width,  scan_x + scan_w);
                const int uy1 = std::max(entry.y_offset + read_height, scan_y + scan_h);
                encoder.set_scissor_rect(ux0, uy0, ux1 - ux0, uy1 - uy0);
            } else {
                encoder.set_scissor_rect(entry.x_offset, entry.y_offset, read_width, read_height);
            }
        } else {
            encoder.set_scissor_rect(viewport.x, viewport.y, viewport.width, viewport.height);
        }

        //// TODO Abstraction for partial clear
        m_primitive_buffers.reset_id_ranges();

        //const Render_pass_descriptor& render_pass_desc = m_render_pass->get_descriptor();

        for (auto meshes : content_mesh_spans) {
            render_meshes(parameters, encoder, m_pipeline, meshes);
        }

        // Clear depth for tool pixels
        {
            encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
            const float far_depth = parameters.reverse_depth ? 0.0f : 1.0f;
            encoder.set_viewport_depth_range(far_depth, far_depth);
            for (auto mesh_spans : tool_mesh_spans) {
                render_meshes(parameters, encoder, m_selective_depth_clear_pipeline, mesh_spans);
            }
        }

        // Resume normal depth usage
        {
            encoder.set_viewport_depth_range(0.0f, 1.0f);
            for (auto meshes : tool_mesh_spans) {
                render_meshes(parameters, encoder, m_pipeline, meshes);
            }
        }
    }

    // Transfer pixel data from GPU to CPU
    {
        entry.buffer_range = m_texture_read_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_read, color_image_size_bytes + depth_image_size_bytes);
        Blit_command_encoder encoder = m_graphics_device.make_blit_command_encoder(parameters.command_buffer);

        const Texture* source_texture = m_color_texture.get();
        std::uintptr_t source_slice   = 0;
        std::uintptr_t source_level   = 0;
        glm::ivec3     source_origin  = glm::ivec3{entry.x_offset, entry.y_offset, 0};
        glm::ivec3     source_size    = glm::ivec3{read_width, read_height, 1};
        const Buffer*  destination_buffer          = entry.buffer_range.get_buffer()->get_buffer();
        std::uintptr_t destination_offset          = entry.buffer_range.get_byte_start_offset_in_buffer();
        std::uintptr_t destination_bytes_per_row   = s_extent * erhe::dataformat::get_format_size_bytes(m_color_texture->get_pixelformat());
        std::uintptr_t destination_bytes_per_image = s_extent * destination_bytes_per_row;

        encoder.copy_from_texture(
            source_texture             ,
            source_slice               ,
            source_level               ,
            source_origin              ,
            source_size                ,
            destination_buffer         ,
            destination_offset         ,
            destination_bytes_per_row  ,
            destination_bytes_per_image
        );

        source_texture     = m_depth_texture.get();
        destination_offset += destination_bytes_per_image;

        encoder.copy_from_texture(
            source_texture             ,
            source_slice               ,
            source_level               ,
            source_origin              ,
            source_size                ,
            destination_buffer         ,
            destination_offset         ,
            destination_bytes_per_row  ,
            destination_bytes_per_image
        );

        entry.state        = Transfer_entry::State::Waiting_for_read;
        entry.frame_number = m_graphics_device.get_frame_index();
        entry.slot         = m_current_transfer_entry_slot;
        // log_id_render->info("submit draw & read: slot={} frame={}", entry.slot, entry.frame_number);
        m_graphics_device.add_completion_handler(
            [&entry]() {
                std::span<std::byte> gpu_data = entry.buffer_range.get_span();
                memcpy(&entry.data[0], gpu_data.data(), gpu_data.size_bytes());
                entry.buffer_range.bytes_gpu_used(gpu_data.size_bytes());
                entry.buffer_range.close();
                entry.buffer_range.release();
                entry.state = Transfer_entry::State::Read_complete;
                // Diagnostic: sample the center texel (the pointer position).
                // x_/y_ = s_extent/2 maps to the pointer when scissor / full
                // viewport rendering is correct. s_extent is static constexpr
                // and log_id_render is a global, so no [this] capture is needed.
                // const uint32_t stride = s_extent * 4;
                // const int      cx     = static_cast<int>(s_extent / 2);
                // const int      cy     = static_cast<int>(s_extent / 2);
                // const uint8_t  r      = entry.data[cx * 4 + cy * stride + 0];
                // const uint8_t  g      = entry.data[cx * 4 + cy * stride + 1];
                // const uint8_t  b      = entry.data[cx * 4 + cy * stride + 2];
                // log_id_render->info(
                //     "completed: slot={} frame={} bytes={} center_rgb=({},{},{})",
                //     entry.slot, entry.frame_number, gpu_data.size_bytes(), r, g, b
                // );
            }
        );
    }

    // Region selection scan readback (box / paint). Two paths produce the same
    // deduplicated id set that take_scan_result() resolves to facet hits a few
    // frames later:
    //   - GPU compute (preferred): two compute passes scan the id buffer into a
    //     bitmask then compact it to a small { count, ids } vector (see
    //     submit_scan_compute()).
    //   - CPU fallback (no compute, e.g. macOS GL 4.1): blit the scan rectangle
    //     of the color texture into a CPU-read buffer and dedup per texel on the
    //     main thread.
    if (do_scan) {
        Region_entry& region = m_region_entries[region_slot];
        region.brush_center = m_pending_scan->brush_center;
        region.brush_radius = m_pending_scan->brush_radius;
        region.is_brush     = m_pending_scan->is_brush;

        // Snapshot the id-range table built by the render_meshes loop above (this
        // scan frame, Skinning_filter::all). take_scan_result() resolves against
        // this snapshot, not the live table which a later frame may have rebuilt
        // with a different mesh set. Clear-and-fill to keep the vector's capacity.
        region.id_ranges.clear();
        const std::vector<erhe::scene_renderer::Primitive_buffer::Id_range>& live_ranges = m_primitive_buffers.id_ranges();
        region.id_ranges.insert(region.id_ranges.end(), live_ranges.begin(), live_ranges.end());

        ensure_scan_compute();
        bool submitted = false;
        if (m_scan_compute_available) {
            submitted = submit_scan_compute(parameters.command_buffer, region, scan_x, scan_y, scan_w, scan_h);
        }

        if (!submitted) {
            // CPU fallback.
            region.used_compute = false;
            const std::size_t bytes_per_pixel = erhe::dataformat::get_format_size_bytes(m_color_texture->get_pixelformat());
            // The texture->buffer blit requires a 256-byte-aligned destination row
            // stride (the pointer pick readback above always uses s_extent*4 == 1024,
            // which is aligned; an arbitrary scan_w*4 such as 5600 is not and yields a
            // sheared / corrupted readback). Round the row stride up to 256 bytes; the
            // padding bytes past scan_w*bytes_per_pixel are skipped on decode.
            const std::size_t row_stride_bytes = (((static_cast<std::size_t>(scan_w) * bytes_per_pixel) + 255u) / 256u) * 256u;
            const std::size_t region_bytes     = row_stride_bytes * static_cast<std::size_t>(scan_h);
            region.data.resize(region_bytes);
            region.x            = scan_x;
            region.y            = scan_y;
            region.width        = scan_w;
            region.height       = scan_h;
            region.row_stride   = static_cast<int>(row_stride_bytes);
            region.frame_number = m_graphics_device.get_frame_index();
            region.buffer_range = m_region_read_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_read, region_bytes);

            Blit_command_encoder encoder           = m_graphics_device.make_blit_command_encoder(parameters.command_buffer);
            const Buffer*        dst_buffer        = region.buffer_range.get_buffer()->get_buffer();
            const std::uintptr_t dst_offset        = region.buffer_range.get_byte_start_offset_in_buffer();
            const std::uintptr_t dst_bytes_per_row = row_stride_bytes;
            const std::uintptr_t dst_bytes_per_img = row_stride_bytes * static_cast<std::uintptr_t>(scan_h);
            encoder.copy_from_texture(
                m_color_texture.get(),
                0,
                0,
                glm::ivec3{scan_x, scan_y, 0},
                glm::ivec3{scan_w, scan_h, 1},
                dst_buffer,
                dst_offset,
                dst_bytes_per_row,
                dst_bytes_per_img
            );
            region.state = Transfer_entry::State::Waiting_for_read;
            m_graphics_device.add_completion_handler(
                [&region]() {
                    std::span<std::byte> gpu_data = region.buffer_range.get_span();
                    memcpy(region.data.data(), gpu_data.data(), gpu_data.size_bytes());
                    region.buffer_range.bytes_gpu_used(gpu_data.size_bytes());
                    region.buffer_range.close();
                    region.buffer_range.release();
                    region.state = Transfer_entry::State::Read_complete;
                }
            );
        }
    }
    // Consume the request whether or not a slot was free; the gesture re-requests
    // every frame it wants a scan.
    m_pending_scan.reset();

    camera_range.release();
    if (bind_joint_buffer) {
        joint_range.release();
    }
}

template<typename T>
inline T read_as(uint8_t const* raw_memory)
{
    static_assert(std::is_trivially_copyable<T>());
    T result;
    memcpy(&result, raw_memory, sizeof(T));
    return result;
}

auto Id_renderer::get(const int x, const int y, uint32_t& out_id, float& out_depth, uint64_t& out_frame_number) -> bool
{
    int slot = static_cast<int>(m_current_transfer_entry_slot);
    int used_slot = -1;

    out_frame_number = 0;
    for (size_t i = 0; i < s_transfer_entry_count; ++i) {
        --slot;
        if (slot < 0) {
            slot = s_transfer_entry_count - 1;
        }

        Transfer_entry& entry = m_transfer_entries[slot];
        if (entry.state == Transfer_entry::State::Read_complete) {
            // const int rel_x = x - entry.x_offset;
            // const int rel_y = y - entry.y_offset;
            // log_id_render->info(
            //     "get(): slot={} READ_COMPLETE frame={} query=({},{}) offset=({},{}) rel=({},{}) in_rect={}",
            //     slot, entry.frame_number, x, y, entry.x_offset, entry.y_offset, rel_x, rel_y,
            //     (x >= entry.x_offset) && (y >= entry.y_offset) &&
            //     (static_cast<size_t>(rel_x) < s_extent) && (static_cast<size_t>(rel_y) < s_extent)
            // );
            if ((x >= entry.x_offset) && (y >= entry.y_offset)) {
                const int x_ = x - entry.x_offset;
                const int y_ = y - entry.y_offset;
                if ((static_cast<size_t>(x_) < s_extent) && (static_cast<size_t>(y_) < s_extent)) {
                    const uint32_t stride = s_extent * 4;
                    const uint8_t  r      = entry.data[x_ * 4 + y_ * stride + 0];
                    const uint8_t  g      = entry.data[x_ * 4 + y_ * stride + 1];
                    const uint8_t  b      = entry.data[x_ * 4 + y_ * stride + 2];
                    // if ((r == 255u) && (g == 255u) && (b == 255u)) { // overflow detected in standard.frag ID variant
                    //     static int counter = 0;
                    //     ++counter; // breakpoint placeholder
                    // }
                    const uint8_t* const depth_ptr = &entry.data[s_extent * s_extent * 4 + x_ * 4 + y_ * stride];
                    if (entry.frame_number > out_frame_number) {
                        out_id           = (r << 16u) | (g << 8u) | b;
                        out_depth        = read_as<float>(depth_ptr);
                        out_frame_number = entry.frame_number;
                        used_slot = slot;
                    }
                    ERHE_VERIFY(entry.slot == slot);
                    // log_id_render->info(
                    //     "get() candidate: slot={} rgb=({},{},{}) id={} depth={} frame={}",
                    //     slot, r, g, b, (r << 16u) | (g << 8u) | b, read_as<float>(depth_ptr), entry.frame_number
                    // );
                }
            }
        }
    }
    // log_id_render->info(
    //     "get() result: id={} depth={} used_slot={} frame={} ok={}",
    //     out_id, out_depth, used_slot, out_frame_number, out_frame_number > 0
    // );
    return out_frame_number > 0;
}

auto Id_renderer::get(const int x, const int y) -> Id_query_result
{
    Id_query_result result{};
    const bool ok = get(x, y, result.id, result.depth, result.frame_number);
    if (!ok) {
        return result;
    }

    // id 0 is the background sentinel (color attachment clear value). Valid
    // ranges start at 1 (see Primitive_buffer::reset_id_ranges), so id 0 is
    // never a real hit.
    if (result.id == 0) {
        return result;
    }

    for (const erhe::scene_renderer::Primitive_buffer::Id_range& r : m_primitive_buffers.id_ranges()) {
        // log_id_render->info(
        //     "  id_range: offset={} length={} mesh={}",
        //     r.offset, r.length, (r.mesh != nullptr) ? r.mesh->get_name() : std::string{"(null)"}
        // );
        if (
            (result.id >= r.offset) &&
            (result.id < (r.offset + r.length))
        ) {
            result.valid                           = true;
            result.mesh                            = std::dynamic_pointer_cast<erhe::scene::Mesh>(r.mesh->shared_from_this());
            result.index_of_gltf_primitive_in_mesh = r.index_of_gltf_primitive_in_mesh;
            result.facet_id                        = result.id - r.offset;
            return result;
        }
    }

    return result;
}

auto Id_renderer::take_scan_result() -> const Scan_result&
{
    // Find the newest completed region readback we have not resolved yet.
    int      best_slot  = -1;
    uint64_t best_frame = m_scan_result.frame_number;
    for (int i = 0; i < s_region_entry_count; ++i) {
        const Region_entry& region = m_region_entries[i];
        if ((region.state == Transfer_entry::State::Read_complete) && (region.frame_number > best_frame)) {
            best_frame = region.frame_number;
            best_slot  = i;
        }
    }
    if (best_slot < 0) {
        // No newer result. Free any completed entries we have already resolved
        // (or that are stale) so their slots can be reused by render().
        for (Region_entry& region : m_region_entries) {
            if (
                (region.state == Transfer_entry::State::Read_complete) &&
                (region.frame_number <= m_scan_result.frame_number)
            ) {
                region.state = Transfer_entry::State::Unused;
            }
        }
        return m_scan_result;
    }

    Region_entry& region = m_region_entries[best_slot];

    // Collect the scan's deduplicated id set into m_scan_id_scratch, either from
    // the compute compaction result or by looping texels on the CPU.
    m_scan_id_scratch.clear();
    if (region.used_compute) {
        // compact_data is { uint count; uint ids[max_output]; } (already
        // brush-masked and deduplicated by the two compute passes). Read count,
        // clamp to the output capacity, and gather the ids.
        uint32_t count = 0;
        if (region.compact_data.size() >= sizeof(uint32_t)) {
            memcpy(&count, region.compact_data.data(), sizeof(uint32_t));
        }
        if (count > region.max_output) {
            // Pass 2 found more distinct ids than the output buffer holds; the
            // surplus ids were not written. This is not expected (max_output is
            // the total facet count, an upper bound on distinct ids), so warn.
            log_id_render->warn(
                "Id_renderer scan compute output overflow: count={} max_output={} (truncating)",
                count, region.max_output
            );
            count = region.max_output;
        }
        const uint8_t* id_bytes = region.compact_data.data() + sizeof(uint32_t);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t id = 0;
            memcpy(&id, id_bytes + (static_cast<std::size_t>(i) * sizeof(uint32_t)), sizeof(uint32_t));
            if (id != 0) {
                m_scan_id_scratch.insert(id);
            }
        }
    } else {
        // CPU path: decode the region's texels. For a brush scan, skip texels
        // outside the disk. id 0 is the background sentinel.
        const int stride = region.row_stride;
        for (int ry = 0; ry < region.height; ++ry) {
            for (int rx = 0; rx < region.width; ++rx) {
                if (region.is_brush) {
                    const float dx = static_cast<float>(region.x + rx) - region.brush_center.x;
                    const float dy = static_cast<float>(region.y + ry) - region.brush_center.y;
                    if ((dx * dx + dy * dy) > (region.brush_radius * region.brush_radius)) {
                        continue;
                    }
                }
                const uint8_t  r  = region.data[rx * 4 + ry * stride + 0];
                const uint8_t  g  = region.data[rx * 4 + ry * stride + 1];
                const uint8_t  b  = region.data[rx * 4 + ry * stride + 2];
                const uint32_t id = (static_cast<uint32_t>(r) << 16u) | (static_cast<uint32_t>(g) << 8u) | static_cast<uint32_t>(b);
                if (id != 0) {
                    m_scan_id_scratch.insert(id);
                }
            }
        }
    }

    // Resolve each unique id to (mesh, primitive, triangle) via the id-range
    // snapshot captured when this scan was submitted (the scan frame, drawn with
    // Skinning_filter::all). Resolving against the live table would be wrong
    // because a later frame may have rebuilt it with a different mesh set and thus
    // different id_offsets.
    m_scan_result.hits.clear();
    const std::vector<erhe::scene_renderer::Primitive_buffer::Id_range>& ranges = region.id_ranges;
    for (const uint32_t id : m_scan_id_scratch) {
        for (const erhe::scene_renderer::Primitive_buffer::Id_range& range : ranges) {
            if ((id >= range.offset) && (id < (range.offset + range.length))) {
                Scan_hit hit;
                hit.mesh            = std::dynamic_pointer_cast<erhe::scene::Mesh>(range.mesh->shared_from_this());
                hit.primitive_index = range.index_of_gltf_primitive_in_mesh;
                hit.facet_id        = id - range.offset;
                m_scan_result.hits.push_back(std::move(hit));
                break;
            }
        }
    }

    m_scan_result.ready        = true;
    m_scan_result.frame_number = region.frame_number;

    // Keep the resolved scan's id-range snapshot for the MCP id-range-mapping
    // query (clear-and-fill to retain capacity).
    m_last_scan_id_ranges.clear();
    m_last_scan_id_ranges.insert(m_last_scan_id_ranges.end(), ranges.begin(), ranges.end());
    region.state = Transfer_entry::State::Unused;
    return m_scan_result;
}

auto Id_renderer::get_last_scan_id_ranges() const -> const std::vector<erhe::scene_renderer::Primitive_buffer::Id_range>&
{
    return m_last_scan_id_ranges;
}

}
