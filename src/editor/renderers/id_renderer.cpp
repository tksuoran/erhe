#include "renderers/id_renderer.hpp"

#include "editor_log.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

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
    , m_texture_read_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::transfer_dst,
        "Id_renderer::m_texture_read_buffer"
    }
    , m_gpu_timer{graphics_device, "Id_renderer"}
{
    const auto& ini = erhe::configuration::get_ini_file_section(erhe::c_erhe_config_file_path, "id_renderer");
    ini.get("enabled", enabled);
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
        render_pass_descriptor.depth_attachment.texture          = m_depth_texture.get();
        render_pass_descriptor.depth_attachment.load_action      = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.depth_attachment.store_action     = erhe::graphics::Store_action::Store;
        render_pass_descriptor.render_target_width               = viewport.width;
        render_pass_descriptor.render_target_height              = viewport.height;
        render_pass_descriptor.debug_label                       = "ID";
        m_render_pass = std::make_unique<Render_pass>(m_graphics_device, render_pass_descriptor);
        constexpr float clear_value[4] = {1.0f, 0.0f, 0.0f, 1.0f };

        const std::size_t color_image_size = s_extent * s_extent * erhe::dataformat::get_format_size(m_color_texture->get_pixelformat());
        const std::size_t depth_image_size = s_extent * s_extent * erhe::dataformat::get_format_size(m_depth_texture->get_pixelformat());
        for (Transfer_entry& entry : m_transfer_entries) {
            entry.data.resize(color_image_size + depth_image_size);
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
    erhe::graphics::Ring_buffer_range          primitive_range            = m_primitive_buffers.update(meshes, primitive_mode, id_filter, settings, primitive_count, true);
    erhe::renderer::Draw_indirect_buffer_range draw_indirect_buffer_range = m_draw_indirect_buffers.update(meshes, primitive_mode, id_filter);
    if (draw_indirect_buffer_range.draw_indirect_count == 0) {
        return;
    }
    if (primitive_count != draw_indirect_buffer_range.draw_indirect_count) {
        log_render->warn("primitive_range != draw_indirect_buffer_range.draw_indirect_count");
    }

    m_primitive_buffers    .bind(render_encoder, primitive_range);
    m_draw_indirect_buffers.bind(render_encoder, draw_indirect_buffer_range.range);
    render_encoder.multi_draw_indexed_primitives_indirect(
        m_pipeline.data.input_assembly.primitive_topology,
        m_mesh_memory.buffer_info.index_type,
        draw_indirect_buffer_range.range.get_byte_start_offset_in_buffer(),
        draw_indirect_buffer_range.draw_indirect_count,
        sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command)
    );

    primitive_range.release();
    draw_indirect_buffer_range.range.release();
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

    const std::size_t color_image_size = s_extent * s_extent * erhe::dataformat::get_format_size(m_color_texture->get_pixelformat());
    const std::size_t depth_image_size = s_extent * s_extent * erhe::dataformat::get_format_size(m_depth_texture->get_pixelformat());

    Scoped_debug_group debug_group{"Id_renderer::render()"};
    Scoped_gpu_timer   timer      {m_gpu_timer};

    const auto projection_transforms = camera.projection_transforms(viewport);
    const mat4 clip_from_world       = projection_transforms.clip_from_world.get_matrix();

    Transfer_entry& entry = get_current_transfer_entry();
    if (entry.state == Transfer_entry::State::Waiting_for_read) {
        log_id_render->warn("Id_renderer::render(): Transfer_entry slot busy");
        return;
    }
    entry.x_offset        = std::max(x - (static_cast<int>(s_extent / 2)), 0);
    entry.y_offset        = std::max(y - (static_cast<int>(s_extent / 2)), 0);
    entry.clip_from_world = clip_from_world;

    Ring_buffer_range camera_range = m_camera_buffers.update(
        *camera.projection(),
        *camera.get_node(),
        viewport,
        1.0f,
        glm::vec4{0.0f},
        glm::vec4{0.0f},
        0
    );

    // Render
    {
        Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(*m_render_pass.get());
        m_camera_buffers.bind(encoder, camera_range);

        if (m_use_scissor) {
            encoder.set_scissor_rect(entry.x_offset, entry.y_offset, s_extent, s_extent);
        }

        //// TODO Abstraction for partial clear
        m_primitive_buffers.reset_id_ranges();

        encoder.set_render_pipeline_state(m_pipeline);

        for (auto meshes : content_mesh_spans) {
            render(encoder, meshes);
        }

        // Clear depth for tool pixels
        {
            encoder.set_render_pipeline_state(m_selective_depth_clear_pipeline);
            encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
            encoder.set_viewport_depth_range(0.0f, 0.0f); // Reverse Z far
            for (auto mesh_spans : tool_mesh_spans) {
                render(encoder, mesh_spans);
            }
        }

        // Resume normal depth usage
        {
            encoder.set_render_pipeline_state(m_pipeline);
            encoder.set_viewport_depth_range(0.0f, 1.0f);
            for (auto meshes : tool_mesh_spans) {
                render(encoder, meshes);
            }
        }
    }

    // Transfer pixel data from GPU to CPU
    {
        entry.buffer_range = m_texture_read_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_read, color_image_size + depth_image_size);
        Blit_command_encoder encoder = m_graphics_device.make_blit_command_encoder();

        const Texture* source_texture = m_color_texture.get();
        std::uintptr_t source_slice   = 0;
        std::uintptr_t source_level   = 0;
        glm::ivec3     source_origin  = glm::ivec3{entry.x_offset, entry.y_offset, 0};
        glm::ivec3     source_min     = source_origin;
        glm::ivec3     source_max     = glm::ivec3{
            std::min(entry.x_offset + s_extent, parameters.viewport.width),
            std::min(entry.y_offset + s_extent, parameters.viewport.height),
            1
        };
        glm::ivec3     source_size                 = source_max - source_min;
        const Buffer*  destination_buffer          = entry.buffer_range.get_buffer()->get_buffer();
        std::uintptr_t destination_offset          = entry.buffer_range.get_byte_start_offset_in_buffer();
        std::uintptr_t destination_bytes_per_row   = s_extent * erhe::dataformat::get_format_size(m_color_texture->get_pixelformat());
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
        // log_id_render->debug("id submit draw & read slot = {}, frame = {}", entry.slot, entry.frame_number);
        m_graphics_device.add_completion_handler(
            [&entry]() {
                std::span<std::byte> gpu_data = entry.buffer_range.get_span();
                memcpy(&entry.data[0], gpu_data.data(), gpu_data.size_bytes());
                entry.buffer_range.bytes_gpu_used(gpu_data.size_bytes());
                entry.buffer_range.close();
                entry.buffer_range.release();
                entry.state = Transfer_entry::State::Read_complete;
                // log_id_render->debug(
                //     "completed id: slot = {}, frame = {} size = {} dst = {}",
                //     entry.slot, entry.frame_number, gpu_data.size_bytes(), fmt::ptr(&entry.data[0])
                // );
            }
        );
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
            if ((x >= entry.x_offset) && (y >= entry.y_offset)) {
                const int x_ = x - entry.x_offset;
                const int y_ = y - entry.y_offset;
                if ((static_cast<size_t>(x_) < s_extent) && (static_cast<size_t>(y_) < s_extent)) {
                    const uint32_t stride = s_extent * 4;
                    const uint8_t  r      = entry.data[x_ * 4 + y_ * stride + 0];
                    const uint8_t  g      = entry.data[x_ * 4 + y_ * stride + 1];
                    const uint8_t  b      = entry.data[x_ * 4 + y_ * stride + 2];
                    // if ((r == 255u) && (g == 255u) && (b == 255u)) { // overflow detected in id.vert
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
                    // log_id_render->debug("id get candidate: r = {:>3} g = {:>3} b = {:>3} id = {} depth = {} slot {} frame = {}", r, g, b, out_id, out_depth, slot, out_frame_number);
                }
            }
        }
    }
    // log_id_render->debug("id get: id = {} depth = {} slot {} frame = {}", out_id, out_depth, used_slot, out_frame_number);
    return out_frame_number > 0;
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
