#include "erhe_scene_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/draw_list.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_item/item.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::scene_renderer {

Draw_indirect_buffer::Draw_indirect_buffer(erhe::graphics::Device& graphics_device, const int max_draw_count)
    : Ring_buffer_client{
        graphics_device,
        erhe::graphics::Buffer_target::draw_indirect,
        "Draw_indirect_buffer"
    }
    , m_record_indexing{graphics_device.get_info().record_indexing}
    , m_max_draw_count {max_draw_count}
{
}

auto Draw_indirect_buffer::get_first_instance(const std::size_t record_index) const -> uint32_t
{
    return (m_record_indexing == erhe::graphics::Record_indexing::first_instance)
        ? static_cast<uint32_t>(record_index)
        : uint32_t{0};
}

auto Draw_indirect_buffer::update(
    const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes,
    erhe::primitive::Primitive_mode                            primitive_mode,
    const erhe::Item_filter&                                   filter
) -> Draw_indirect_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    SPDLOG_LOGGER_TRACE(
        log_render,
        "meshes.size() = {}, m_draw_indirect_writer.write_offset = {}",
        meshes.size(),
        m_writer.write_offset
    );

    // Conservative upper limit
    std::size_t primitive_count = 0;
    for (const auto& mesh : meshes) {
        if (!filter(mesh->get_flag_bits())) {
            continue;
        }
        primitive_count += mesh->get_primitives().size();
    }
    if (primitive_count == 0)
    {
        return {};
    }

    const std::size_t                 entry_size     = sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command);
    const std::size_t                 max_byte_count = primitive_count * entry_size;
    erhe::graphics::Ring_buffer_range buffer_range   = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, max_byte_count);
    const auto                        gpu_data       = buffer_range.get_span();
    size_t      write_offset       {0};
    uint32_t    instance_count     {1};
    std::size_t draw_indirect_count{0};
    
    for (const auto& mesh : meshes) {
        if (!filter(mesh->get_flag_bits())) {
            continue;
        }

        for (auto& mesh_primitive : mesh->get_primitives()) {
            const erhe::primitive::Primitive&   primitive   = *mesh_primitive.primitive.get();
            const erhe::primitive::Buffer_mesh* buffer_mesh_ptr =
                primitive.get_resolved_renderable_mesh(erhe::primitive::Mesh_variant::optimized, primitive_mode).second;
            ERHE_VERIFY(buffer_mesh_ptr != nullptr);
            const erhe::primitive::Buffer_mesh& buffer_mesh = *buffer_mesh_ptr;
            const erhe::primitive::Index_range  index_range = buffer_mesh.index_range(primitive_mode);
            if (index_range.index_count == 0) {
                continue;
            }

            uint32_t index_count = static_cast<uint32_t>(index_range.index_count);
            if (m_max_index_count_enable) {
                index_count = std::min(index_count, static_cast<uint32_t>(m_max_index_count));
            }

            const uint32_t base_index  = buffer_mesh.base_index();
            const uint32_t first_index = static_cast<uint32_t>(index_range.first_index + base_index);
            // Solid wireframe draws the expanded vertex stream(s); its sequential
            // indices are relative to that range, so use the expanded base_vertex.
            const uint32_t base_vertex = (primitive_mode == erhe::primitive::Primitive_mode::solid_wireframe)
                ? buffer_mesh.expanded_base_vertex()
                : buffer_mesh.base_vertex();

            const erhe::graphics::Draw_indexed_primitives_indirect_command draw_command{
                index_count,
                instance_count,
                first_index,
                base_vertex,
                get_first_instance(draw_indirect_count)
            };

            erhe::graphics::write(gpu_data, write_offset, erhe::graphics::as_span(draw_command));

            write_offset += entry_size;
            ++draw_indirect_count;
        }
    }

    buffer_range.bytes_written(write_offset);
    buffer_range.close();

    SPDLOG_LOGGER_TRACE(log_draw, "wrote {} entries to draw indirect buffer", draw_indirect_count);
    return Draw_indirect_buffer_range{
        std::move(buffer_range),
        draw_indirect_count
    };
}

auto Draw_indirect_buffer::update(
    const Render_bucket&                  bucket,
    const erhe::primitive::Primitive_mode primitive_mode
) -> Draw_indirect_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(!bucket.entries.empty());

    const std::size_t                 primitive_count = bucket.entries.size();
    const std::size_t                 entry_size      = sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command);
    const std::size_t                 max_byte_count  = primitive_count * entry_size;
    erhe::graphics::Ring_buffer_range buffer_range    = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, max_byte_count);
    const std::span<std::byte>        gpu_data        = buffer_range.get_span();
    std::size_t        write_offset       {0};
    constexpr uint32_t instance_count     {1};
    std::size_t        draw_indirect_count{0};

    for (const Mesh_primitive_entry& entry : bucket.entries) {
        const erhe::scene::Mesh* mesh = entry.mesh;
        ERHE_VERIFY(mesh != nullptr);
        const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
        ERHE_VERIFY(entry.mesh_primitive_index < mesh_primitives.size());
        const erhe::scene::Mesh_primitive& mesh_primitive = mesh_primitives[entry.mesh_primitive_index];
        const erhe::primitive::Primitive*  primitive_ptr  = mesh_primitive.primitive.get();
        ERHE_VERIFY(primitive_ptr != nullptr);
        // The variant bucketing chose - do not re-derive it here.
        const erhe::primitive::Buffer_mesh* buffer_mesh = entry.buffer_mesh;
        ERHE_VERIFY(buffer_mesh != nullptr);
        const erhe::primitive::Index_range index_range = buffer_mesh->index_range(primitive_mode);
        ERHE_VERIFY(index_range.index_count > 0);
        uint32_t index_count = static_cast<uint32_t>(index_range.index_count);
        if (m_max_index_count_enable) {
            index_count = std::min(index_count, static_cast<uint32_t>(m_max_index_count));
        }

        const uint32_t base_index  = buffer_mesh->base_index();
        const uint32_t first_index = static_cast<uint32_t>(index_range.first_index + base_index);
        const uint32_t base_vertex = (primitive_mode == erhe::primitive::Primitive_mode::solid_wireframe)
            ? buffer_mesh->expanded_base_vertex()
            : buffer_mesh->base_vertex();

        const erhe::graphics::Draw_indexed_primitives_indirect_command draw_command{
            index_count,
            instance_count,
            first_index,
            base_vertex,
            get_first_instance(draw_indirect_count)
        };
        erhe::graphics::write(gpu_data, write_offset, erhe::graphics::as_span(draw_command));
        write_offset += entry_size;
        ++draw_indirect_count;
    }

    buffer_range.bytes_written(write_offset);
    buffer_range.close();

    return Draw_indirect_buffer_range{
        std::move(buffer_range),
        draw_indirect_count
    };
}

void Draw_indirect_buffer::append_commands(
    const Render_bucket&                                                  bucket,
    const erhe::primitive::Primitive_mode                                 primitive_mode,
    std::vector<erhe::graphics::Draw_indexed_primitives_indirect_command>& out
) const
{
    ERHE_PROFILE_FUNCTION();

    constexpr uint32_t instance_count{1};
    // first_instance is the command's position in its multi-draw, written by
    // update(span) when the commands are copied for the draw.
    constexpr uint32_t base_instance {0};
    for (const Mesh_primitive_entry& entry : bucket.entries) {
        // The variant bucketing chose - do not re-derive it here.
        const erhe::primitive::Buffer_mesh* buffer_mesh = entry.buffer_mesh;
        ERHE_VERIFY(buffer_mesh != nullptr);
        const erhe::primitive::Index_range index_range = buffer_mesh->index_range(primitive_mode);
        ERHE_VERIFY(index_range.index_count > 0);
        const uint32_t base_index  = buffer_mesh->base_index();
        const uint32_t first_index = static_cast<uint32_t>(index_range.first_index + base_index);
        const uint32_t base_vertex = (primitive_mode == erhe::primitive::Primitive_mode::solid_wireframe)
            ? buffer_mesh->expanded_base_vertex()
            : buffer_mesh->base_vertex();
        out.push_back(
            erhe::graphics::Draw_indexed_primitives_indirect_command{
                static_cast<uint32_t>(index_range.index_count),
                instance_count,
                first_index,
                base_vertex,
                base_instance
            }
        );
    }
}

auto Draw_indirect_buffer::update(
    const std::span<const erhe::graphics::Draw_indexed_primitives_indirect_command> commands
) -> Draw_indirect_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(!commands.empty());
    const std::size_t                 entry_size     = sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command);
    const std::size_t                 max_byte_count = commands.size() * entry_size;
    erhe::graphics::Ring_buffer_range buffer_range   = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, max_byte_count);
    const std::span<std::byte>        gpu_data       = buffer_range.get_span();
    std::size_t write_offset{0};
    for (std::size_t command_index = 0, command_count = commands.size(); command_index < command_count; ++command_index) {
        erhe::graphics::Draw_indexed_primitives_indirect_command draw_command = commands[command_index];
        // The commands of one span are one multi-draw over the records the
        // caller binds with it, in the same order: the record index is the
        // position, which this writer owns (prepared commands carry none).
        ERHE_VERIFY(draw_command.base_instance == 0);
        draw_command.base_instance = get_first_instance(command_index);
        if (m_max_index_count_enable) {
            draw_command.index_count = std::min(draw_command.index_count, static_cast<uint32_t>(m_max_index_count));
        }
        erhe::graphics::write(gpu_data, write_offset, erhe::graphics::as_span(draw_command));
        write_offset += entry_size;
    }
    buffer_range.bytes_written(write_offset);
    buffer_range.close();
    return Draw_indirect_buffer_range{
        std::move(buffer_range),
        commands.size()
    };
}

auto Draw_indirect_buffer::update(
    const Draw_list&                    draw_list,
    const std::size_t                   begin,
    const std::size_t                   end,
    const std::span<const std::uint8_t> entry_passes,
    const Draw_record_layout            record_layout
) -> Draw_indirect_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(
        (record_layout == Draw_record_layout::packed) ||
        (m_record_indexing == erhe::graphics::Record_indexing::first_instance)
    );

    ERHE_VERIFY(begin <= end);
    ERHE_VERIFY(end <= draw_list.entries.size());
    ERHE_VERIFY(entry_passes.size() == draw_list.entries.size());
    const std::size_t                 max_draw_count = end - begin;
    const std::size_t                 entry_size     = sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command);
    const std::size_t                 max_byte_count = max_draw_count * entry_size;
    erhe::graphics::Ring_buffer_range buffer_range   = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, max_byte_count);
    const std::span<std::byte>        gpu_data       = buffer_range.get_span();
    std::size_t        write_offset       {0};
    constexpr uint32_t instance_count     {1};
    std::size_t        draw_indirect_count{0};

    for (std::size_t i = begin; i < end; ++i) {
        const Draw_list_entry& entry = draw_list.entries[i];
        if (entry_passes[i] == 0) {
            continue;
        }
        uint32_t index_count = entry.index_count;
        if (m_max_index_count_enable) {
            index_count = std::min(index_count, static_cast<uint32_t>(m_max_index_count));
        }
        // The record index travels only under Record_indexing::first_instance.
        uint32_t first_instance = 0;
        if (m_record_indexing == erhe::graphics::Record_indexing::first_instance) {
            first_instance = static_cast<uint32_t>((record_layout == Draw_record_layout::chunk_entries) ? (i - begin) : draw_indirect_count);
        }
        const erhe::graphics::Draw_indexed_primitives_indirect_command draw_command{
            index_count,
            instance_count,
            entry.first_index,
            entry.base_vertex,
            first_instance
        };
        erhe::graphics::write(gpu_data, write_offset, erhe::graphics::as_span(draw_command));
        write_offset += entry_size;
        ++draw_indirect_count;
    }

    buffer_range.bytes_written(write_offset);
    buffer_range.close();

    return Draw_indirect_buffer_range{
        std::move(buffer_range),
        draw_indirect_count
    };
}

} // namespace erhe::renderer
