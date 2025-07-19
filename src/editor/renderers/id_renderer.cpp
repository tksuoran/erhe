#include "renderers/id_renderer.hpp"

#include "editor_log.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/renderbuffer.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_verify/verify.hpp"

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
Id_renderer::Id_frame_resources::Id_frame_resources(erhe::graphics::Device& graphics_device, const std::size_t /*slot*/)
    : pixel_pack_buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count = s_id_buffer_size,
            .usage               = erhe::graphics::Buffer_usage::pixel,
            .direction           = erhe::graphics::Buffer_direction::gpu_to_cpu,
            .cache_mode          = erhe::graphics::Buffer_cache_mode::default_,
            .mapping             = erhe::graphics::Buffer_mapping::persistent,
            .coherency           = erhe::graphics::Buffer_coherency::on,
            .debug_label         = "ID"
        }
    }
{
}

Id_renderer::Id_frame_resources::Id_frame_resources(Id_frame_resources&& other) noexcept = default;

auto Id_renderer::Id_frame_resources::operator=(Id_frame_resources&& other) noexcept -> Id_frame_resources& = default;

static constexpr std::string_view c_id_renderer_initialize_component{"Id_renderer::initialize_component()"};

[[nodiscard]] auto get_max_draw_count() -> std::size_t
{
    int max_draw_count = 1000;
    const auto& ini = erhe::configuration::get_ini_file_section(erhe::c_erhe_config_file_path, "renderer");
    ini.get("max_draw_count", max_draw_count);
    return max_draw_count;
}

using Vertex_input_state   = erhe::graphics::Vertex_input_state;
using Input_assembly_state = erhe::graphics::Input_assembly_state;
using Rasterization_state  = erhe::graphics::Rasterization_state;
using Depth_stencil_state  = erhe::graphics::Depth_stencil_state;
using Color_blend_state    = erhe::graphics::Color_blend_state;

Id_renderer::Id_renderer(
    erhe::graphics::Device&                  graphics_device,
    erhe::scene_renderer::Program_interface& program_interface,
    Mesh_memory&                             mesh_memory,
    Programs&                                programs
)
    : m_graphics_device      {graphics_device}
    , m_mesh_memory          {mesh_memory}
    , m_camera_buffers       {graphics_device, program_interface.camera_interface}
    , m_draw_indirect_buffers{graphics_device}
    , m_primitive_buffers    {graphics_device, program_interface.primitive_interface}
    , m_pipeline{erhe::graphics::Render_pipeline_data{
        .name           = "ID Renderer",
        .shader_stages  = &programs.id.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangle,
        .rasterization  = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
        .color_blend    = Color_blend_state::color_blend_disabled
    }}

    , m_selective_depth_clear_pipeline{erhe::graphics::Render_pipeline_data{
        .name           = "ID Renderer selective depth clear",
        .shader_stages  = &programs.id.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangle,
        .rasterization  = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil  = Depth_stencil_state::depth_test_always_stencil_test_disabled,
        .color_blend    = Color_blend_state::color_writes_disabled,
    }}
    , m_gpu_timer{graphics_device, "Id_renderer"}
{
    create_id_frame_resources();

    const auto& ini = erhe::configuration::get_ini_file_section(erhe::c_erhe_config_file_path, "id_renderer");
    ini.get("enabled", enabled);
}

Id_renderer::~Id_renderer()
{
}

void Id_renderer::create_id_frame_resources()
{
    ERHE_PROFILE_FUNCTION();

    for (size_t slot = 0; slot < s_frame_resources_count; ++slot) {
        m_id_frame_resources.emplace_back(m_graphics_device, slot);
    }
}

auto Id_renderer::current_id_frame_resources() -> Id_frame_resources&
{
    return m_id_frame_resources[m_current_id_frame_resource_slot];
}

void Id_renderer::next_frame()
{
    if (!m_enabled) {
        return;
    }

    m_current_id_frame_resource_slot = (m_current_id_frame_resource_slot + 1) % s_frame_resources_count;
}

void Id_renderer::update_framebuffer(const erhe::math::Viewport viewport)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_enabled) {
        return;
    }

    ERHE_VERIFY(m_use_renderbuffers != m_use_textures);

    if (m_use_renderbuffers) {
        if (
            !m_color_renderbuffer ||
            (m_color_renderbuffer->get_width()  != viewport.width) ||
            (m_color_renderbuffer->get_height() != viewport.height)
        ) {
            m_color_renderbuffer.reset();
            m_depth_renderbuffer.reset();
            m_render_pass.reset();
            m_color_renderbuffer = std::make_unique<Renderbuffer>(
                m_graphics_device,
                erhe::dataformat::Format::format_8_vec4_unorm,
                viewport.width,
                viewport.height
            );
            m_depth_renderbuffer = std::make_unique<Renderbuffer>(
                m_graphics_device,
                erhe::dataformat::Format::format_d32_sfloat,
                viewport.width,
                viewport.height
            );
            m_color_renderbuffer->set_debug_label("ID Color");
            m_depth_renderbuffer->set_debug_label("ID Depth");
            erhe::graphics::Render_pass_descriptor render_pass_descriptor;
            render_pass_descriptor.color_attachments[0].renderbuffer = m_color_renderbuffer.get();
            render_pass_descriptor.color_attachments[0].load_action  = erhe::graphics::Load_action::Clear;
            render_pass_descriptor.color_attachments[0].store_action = erhe::graphics::Store_action::Store;
            render_pass_descriptor.depth_attachment.renderbuffer     = m_depth_renderbuffer.get();
            render_pass_descriptor.depth_attachment.load_action      = erhe::graphics::Load_action::Clear;
            render_pass_descriptor.depth_attachment.store_action     = erhe::graphics::Store_action::Store;
            render_pass_descriptor.render_target_width               = viewport.width;
            render_pass_descriptor.render_target_height              = viewport.height;
            render_pass_descriptor.debug_label                       = "ID";
            m_render_pass = std::make_unique<Render_pass>(m_graphics_device, render_pass_descriptor);
        }
    }

    if (m_use_textures) {
        if (
            !m_color_texture ||
            (m_color_texture->get_width()  != viewport.width) ||
            (m_color_texture->get_height() != viewport.height)
        ) {
            m_color_texture.reset();
            m_depth_texture.reset();
            m_render_pass.reset();
            m_color_texture = std::make_unique<Texture>(
                m_graphics_device,
                Texture::Create_info{
                    .device      = m_graphics_device,
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
                Texture::Create_info{
                    .device      = m_graphics_device,
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
            render_pass_descriptor.depth_attachment.texture          = m_depth_texture.get();
            render_pass_descriptor.depth_attachment.load_action      = erhe::graphics::Load_action::Clear;
            render_pass_descriptor.depth_attachment.store_action     = erhe::graphics::Store_action::Store;
            render_pass_descriptor.render_target_width               = viewport.width;
            render_pass_descriptor.render_target_height              = viewport.height;
            render_pass_descriptor.debug_label                       = "ID";
            m_render_pass = std::make_unique<Render_pass>(m_graphics_device, render_pass_descriptor);
            constexpr float clear_value[4] = {1.0f, 0.0f, 0.0f, 1.0f };
        }
    }
}

void Id_renderer::render(erhe::graphics::Render_command_encoder& render_encoder, const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes)
{
    ERHE_PROFILE_FUNCTION();

    const erhe::Item_filter id_filter{
        .require_all_bits_set           = erhe::Item_flags::visible | erhe::Item_flags::id,
        .require_at_least_one_bit_set   = 0u,
        .require_all_bits_clear         = 0u,
        .require_at_least_one_bit_clear = 0u
    };

    const erhe::scene_renderer::Primitive_interface_settings settings{
        .color_source = erhe::scene_renderer::Primitive_color_source::id_offset
    };

    const erhe::primitive::Primitive_mode primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
    std::size_t primitive_count{0};
    erhe::graphics::Buffer_range               primitive_range            = m_primitive_buffers.update(meshes, primitive_mode, id_filter, settings, primitive_count, true);
    erhe::renderer::Draw_indirect_buffer_range draw_indirect_buffer_range = m_draw_indirect_buffers.update(meshes, primitive_mode, id_filter);
    if (draw_indirect_buffer_range.draw_indirect_count == 0) {
        return;
    }
    if (primitive_count != draw_indirect_buffer_range.draw_indirect_count) {
        log_render->warn("primitive_range != draw_indirect_buffer_range.draw_indirect_count");
    }

    m_primitive_buffers    .bind(render_encoder, primitive_range);
    m_draw_indirect_buffers.bind(render_encoder, draw_indirect_buffer_range.range);
    {
        static constexpr std::string_view c_draw{"draw"};

        ERHE_PROFILE_SCOPE("mdi");
        render_encoder.multi_draw_indexed_primitives_indirect(
            m_pipeline.data.input_assembly.primitive_topology,
            m_mesh_memory.buffer_info.index_type,
            draw_indirect_buffer_range.range.get_byte_start_offset_in_buffer(),
            draw_indirect_buffer_range.draw_indirect_count,
            sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command)
        );
    }

    primitive_range.release();
    draw_indirect_buffer_range.range.release();
}

void Id_renderer::render(const Render_parameters& parameters)
{
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

    erhe::graphics::Scoped_debug_group debug_group{"Id_renderer::render()"};
    erhe::graphics::Scoped_gpu_timer   timer      {m_gpu_timer};

    update_framebuffer(viewport);

    const auto projection_transforms = camera.projection_transforms(viewport);
    const mat4 clip_from_world       = projection_transforms.clip_from_world.get_matrix();

    auto& idr = current_id_frame_resources();
    idr.x_offset        = std::max(x - (static_cast<int>(s_extent / 2)), 0);
    idr.y_offset        = std::max(y - (static_cast<int>(s_extent / 2)), 0);
    idr.clip_from_world = clip_from_world;

    erhe::graphics::Buffer_range camera_range = m_camera_buffers.update(
        *camera.projection(),
        *camera.get_node(),
        viewport,
        1.0f,
        glm::vec4{0.0f},
        glm::vec4{0.0f},
        0
    );

    erhe::graphics::Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(*m_render_pass.get());
    m_camera_buffers.bind(encoder, camera_range);

    gl::viewport   (viewport.x, viewport.y, viewport.width, viewport.height);
    if (m_use_scissor) {
        gl::scissor(idr.x_offset, idr.y_offset, s_extent, s_extent);
        gl::enable (gl::Enable_cap::scissor_test);
    }

    //// TODO Abstraction for partial clear
    //gl::clear_color(1.0f, 1.0f, 1.0f, 0.1f);
    //gl::clear      (gl::Clear_buffer_mask::color_buffer_bit | gl::Clear_buffer_mask::depth_buffer_bit);

    m_primitive_buffers.reset_id_ranges();

    encoder.set_render_pipeline_state(m_pipeline);

    for (auto meshes : content_mesh_spans) {
        render(encoder, meshes);
    }

    // Clear depth for tool pixels
    {
        encoder.set_render_pipeline_state(m_selective_depth_clear_pipeline);
        gl::depth_range(0.0f, 0.0f); // TODO move to render pipeline
        for (auto mesh_spans : tool_mesh_spans) {
            render(encoder, mesh_spans);
        }
    }

    // Resume normal depth usage
    {
        //ERHE_PROFILE_GPU_SCOPE(c_id_renderer_render_tool)

        encoder.set_render_pipeline_state(m_pipeline);
        gl::depth_range(0.0f, 1.0f); // TODO move to render pipeline
        for (auto meshes : tool_mesh_spans) {
            render(encoder, meshes);
        }
    }

    {
        if (m_use_scissor) {
            gl::disable(gl::Enable_cap::scissor_test);
        }
        gl::bind_buffer(gl::Buffer_target::pixel_pack_buffer, idr.pixel_pack_buffer.gl_name());
        void* const color_offset = nullptr;
        void* const depth_offset = reinterpret_cast<void*>(s_extent * s_extent * 4);
        const unsigned int fbo = m_render_pass->gl_name();
        gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, fbo);
        gl::named_framebuffer_read_buffer(fbo, gl::Color_buffer::color_attachment0);
        gl::read_pixels(
            idr.x_offset,
            idr.y_offset,
            s_extent,
            s_extent,
            gl::Pixel_format::rgba,
            gl::Pixel_type::unsigned_byte,
            color_offset
        );
        gl::read_pixels(
            idr.x_offset,
            idr.y_offset,
            s_extent,
            s_extent,
            gl::Pixel_format::depth_component,
            gl::Pixel_type::float_,
            depth_offset
        );
        gl::bind_buffer(gl::Buffer_target::pixel_pack_buffer, 0);
        idr.sync         = gl::fence_sync(gl::Sync_condition::sync_gpu_commands_complete, 0);
        idr.state        = Id_frame_resources::State::Waiting_for_read;
        idr.frame_number = m_graphics_device.get_frame_number();
    }

    camera_range.release();
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
    if (m_id_frame_resources.empty()) {
        return false;
    }
    int slot = static_cast<int>(m_current_id_frame_resource_slot);

    for (size_t i = 0; i < s_frame_resources_count; ++i) {
        --slot;
        if (slot < 0) {
            slot = s_frame_resources_count - 1;
        }

        auto& idr = m_id_frame_resources[slot];

        if (idr.state == Id_frame_resources::State::Waiting_for_read) {
            GLint sync_status = GL_UNSIGNALED;
            gl::get_sync_iv(idr.sync, gl::Sync_parameter_name::sync_status, 4, nullptr, &sync_status);

            if (sync_status == GL_SIGNALED) {
                gl::bind_buffer(gl::Buffer_target::pixel_pack_buffer, idr.pixel_pack_buffer.gl_name());

                std::span<std::byte> gpu_data = idr.pixel_pack_buffer.get_map();

                memcpy(&idr.data[0], gpu_data.data(), gpu_data.size_bytes());
                idr.state = Id_frame_resources::State::Read_complete;
            }
        }

        if (idr.state == Id_frame_resources::State::Read_complete) {
            if ((x >= idr.x_offset) && (y >= idr.y_offset)) {
                const int x_ = x - idr.x_offset;
                const int y_ = y - idr.y_offset;
                if ((static_cast<size_t>(x_) < s_extent) && (static_cast<size_t>(y_) < s_extent)) {
                    const uint32_t stride = s_extent * 4;
                    const uint8_t  r      = idr.data[x_ * 4 + y_ * stride + 0];
                    const uint8_t  g      = idr.data[x_ * 4 + y_ * stride + 1];
                    const uint8_t  b      = idr.data[x_ * 4 + y_ * stride + 2];
                    // if ((r == 255u) && (g == 255u) && (b == 255u)) { // overflow detected in id.vert
                    //     static int counter = 0;
                    //     ++counter; // breakpoint placeholder
                    // }
                    const uint8_t* const depth_ptr = &idr.data[s_extent * s_extent * 4 + x_ * 4 + y_ * stride];
                    out_id           = (r << 16u) | (g << 8u) | b;
                    out_depth        = read_as<float>(depth_ptr);
                    out_frame_number = idr.frame_number;
                    // log_id_render->debug("id: r = {:>3} g = {:>3} b = {:>3} id = {} depth = {} frame = {}", r, g, b, out_id, out_depth, out_frame_number);
                    return true;
                }
            }
        }
    }
    return false;
}

auto Id_renderer::get(const int x, const int y) -> Id_query_result
{
    Id_query_result result{};
    const bool ok = get(x, y, result.id, result.depth, result.frame_number);
    if (!ok) {
        return result;
    }

    for (auto& r : m_primitive_buffers.id_ranges()) {
        if (
            (result.id >= r.offset) &&
            (result.id < (r.offset + r.length))
        ) {
            result.valid                           = true;
            result.mesh                            = std::dynamic_pointer_cast<erhe::scene::Mesh>(r.mesh->shared_from_this());
            result.index_of_gltf_primitive_in_mesh = r.index_of_gltf_primitive_in_mesh;
            result.triangle_id                     = result.id - r.offset;
            return result;
        }
    }

    return result;
}

}
