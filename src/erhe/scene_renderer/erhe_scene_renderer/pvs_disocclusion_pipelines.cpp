#include "erhe_scene_renderer/pvs_disocclusion_pipelines.hpp"
#include "erhe_scene_renderer/pvs_disocclusion.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <optional>
#include <vector>
#include <string>

namespace erhe::scene_renderer {

namespace {

// Workgroups of a layer's cones pass (one workgroup per cone): enough to
// fill the GPU, the rest is walked grid-stride.
constexpr uint32_t c_item_workgroups = 4096;

[[nodiscard]] auto make_work_buffer(erhe::graphics::Device& device, const std::size_t byte_count, const char* label) -> std::unique_ptr<erhe::graphics::Buffer>
{
    using namespace erhe::graphics;
    return std::make_unique<Buffer>(
        device,
        Buffer_create_info{
            .capacity_byte_count               = byte_count,
            .usage                             = Buffer_usage::storage | Buffer_usage::transfer_dst | Buffer_usage::transfer_src,
            .required_memory_property_bit_mask = Memory_property_flag_bit_mask::device_local,
            .debug_label                       = erhe::utility::Debug_label{label}
        }
    );
}

} // anonymous namespace

Pvs_disocclusion_buffers::Pvs_disocclusion_buffers(
    erhe::graphics::Device& graphics_device,
    const uint32_t          resolution,
    const uint32_t          tile_size,
    const uint32_t          layer_count
)
{
    ERHE_VERIFY((tile_size >= 1) && (resolution >= tile_size) && ((resolution % tile_size) == 0));
    ERHE_VERIFY((layer_count >= 1) && (layer_count <= Pvs_disocclusion_parameters::max_layer_count));
    const uint32_t tiles = resolution / tile_size;
    m_tiles_per_layer = tiles * tiles;
    m_tile_count      = m_tiles_per_layer * layer_count;
    m_closure      = make_work_buffer(graphics_device, get_closure_bytes(),      "PVS disocclusion closed tiles");
    m_column_masks = make_work_buffer(graphics_device, get_column_masks_bytes(), "PVS disocclusion column masks");
    m_tile_masks   = make_work_buffer(graphics_device, get_tile_masks_bytes(),   "PVS disocclusion tile masks");
    m_lists        = make_work_buffer(graphics_device, get_lists_bytes(),        "PVS disocclusion lists");
    // The disoccluded map: per level, layer_count * size^2 words.
    uint32_t map_words = 0;
    for (uint32_t level = 0; ; ++level) {
        const uint32_t size = pvs_map_level_size(tiles, level);
        map_words += layer_count * size * size;
        if (size == 1u) {
            break;
        }
    }
    m_map_bytes = static_cast<std::size_t>(map_words) * sizeof(uint32_t);
    m_map       = make_work_buffer(graphics_device, m_map_bytes, "PVS disocclusion map");
    m_runs_bytes = static_cast<std::size_t>(m_tiles_per_layer) * sizeof(uint32_t);
    m_runs       = make_work_buffer(graphics_device, m_runs_bytes, "PVS disocclusion runs");
}

Pvs_disocclusion_buffers::~Pvs_disocclusion_buffers() noexcept = default;

auto Pvs_disocclusion_buffers::get_closure_bytes() const -> std::size_t
{
    return static_cast<std::size_t>(m_tile_count) * sizeof(uint32_t);
}

auto Pvs_disocclusion_buffers::get_column_masks_bytes() const -> std::size_t
{
    return static_cast<std::size_t>(m_tiles_per_layer) * 2u * sizeof(uint32_t);
}

auto Pvs_disocclusion_buffers::get_tile_masks_bytes() const -> std::size_t
{
    return static_cast<std::size_t>(m_tile_count) * 2u * sizeof(uint32_t);
}

auto Pvs_disocclusion_buffers::get_lists_bytes() const -> std::size_t
{
    return (lists_header_words + (static_cast<std::size_t>(m_tile_count) * 2u)) * sizeof(uint32_t);
}

Pvs_disocclusion_pipelines::Pvs_disocclusion_pipelines(erhe::graphics::Device& graphics_device, const std::filesystem::path& shader_path)
    : m_graphics_device{graphics_device}
    , m_parameters_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "pvs_disocclusion_parameters",
            .binding_point = static_cast<int>(parameters_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_layered_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "pvs_layered_samples",
            .binding_point = static_cast<int>(layered_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_closure_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "pvs_closure",
            .binding_point = static_cast<int>(closure_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    }
    , m_column_masks_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "pvs_column_masks",
            .binding_point = static_cast<int>(column_masks_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    }
    , m_tile_masks_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "pvs_tile_masks",
            .binding_point = static_cast<int>(tile_masks_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    }
    , m_lists_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "pvs_lists",
            .binding_point = static_cast<int>(lists_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    }
    , m_column_words_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "pvs_column_words",
            .binding_point = static_cast<int>(column_words_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    }
    , m_trace_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "pvs_trace",
            .binding_point = static_cast<int>(trace_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    }
    , m_column_bounds_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "pvs_column_bounds",
            .binding_point = static_cast<int>(column_bounds_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_map_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "pvs_map",
            .binding_point = static_cast<int>(map_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    }
    , m_runs_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "pvs_runs",
            .binding_point = static_cast<int>(runs_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    }
    , m_parameter_buffer{graphics_device, erhe::graphics::Buffer_target::storage, "Pvs_disocclusion_pipelines parameters", parameters_binding_point}
{
    using namespace erhe::graphics;

    // std430: the uvec4 / vec4 header and layer array (16 byte strides),
    // the uint arrays (4 byte strides). Pvs_disocclusion_parameters::write()
    // writes the parameter block in this layout, Pvs_layered_interface
    // the layered buffer's.
    const std::size_t sizes_offset  = m_parameters_block.add_uvec4("sizes"     )->get_offset_in_parent();
    m_parameters_block.add_uvec4("counts"    );
    m_parameters_block.add_uvec4("core"      );
    m_parameters_block.add_uvec4("pass"      );
    m_parameters_block.add_uvec4("columns"   );
    m_parameters_block.add_uvec4("view_axes" );
    m_parameters_block.add_vec4 ("sample_point");
    m_parameters_block.add_vec4 ("tangents"  );
    m_parameters_block.add_vec4 ("tile_scale");
    m_parameters_block.add_vec4 ("cell_min"  );
    m_parameters_block.add_vec4 ("cell_max"  );
    const std::size_t layers_offset = m_parameters_block.add_vec4("layers", Shader_resource::unsized_array)->get_offset_in_parent();
    ERHE_VERIFY(sizes_offset == 0);
    ERHE_VERIFY(layers_offset == (Pvs_disocclusion_parameters::header_vec4_count * sizeof(glm::vec4)));

    const std::size_t info_offset  = m_layered_block.add_uvec4("info"       )->get_offset_in_parent();
    const std::size_t range_offset = m_layered_block.add_uvec4("trace"      )->get_offset_in_parent();
    const std::size_t words_offset = m_layered_block.add_uint ("words", Shader_resource::unsized_array)->get_offset_in_parent();
    ERHE_VERIFY((info_offset == 0) && (range_offset == 16) && (words_offset == 32));

    m_closure_block     .add_uint("closed", Shader_resource::unsized_array);
    m_column_masks_block.add_uint("words",  Shader_resource::unsized_array);
    m_tile_masks_block  .add_uint("words",  Shader_resource::unsized_array);
    m_lists_block       .add_uint("words",  Shader_resource::unsized_array);
    m_column_words_block.add_uint("words",  Shader_resource::unsized_array);
    m_trace_block       .add_uint("words",  Shader_resource::unsized_array);
    m_column_bounds_block.add_vec4("bounds", Shader_resource::unsized_array);
    m_map_block         .add_uint("words",  Shader_resource::unsized_array);
    m_runs_block        .add_uint("words",  Shader_resource::unsized_array);

    m_bind_group_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                { .binding_point = parameters_binding_point,   .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = layered_binding_point,      .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = closure_binding_point,      .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = column_masks_binding_point, .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = tile_masks_binding_point,   .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = lists_binding_point,        .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = column_words_binding_point,  .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = trace_binding_point,        .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = column_bounds_binding_point,.type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = map_binding_point,          .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = runs_binding_point,         .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute }
            },
            .debug_label       = "Pvs_disocclusion_pipelines",
            .uses_texture_heap = false
        }
    );

    class Pass_source
    {
    public:
        Pass        pass;
        const char* name;
        const char* file;
    };
    const std::array<Pass_source, static_cast<std::size_t>(Pass::count)> sources{
        Pass_source{Pass::tiles,                   "pvs_tiles",                   "pvs_tiles.comp"},
        Pass_source{Pass::stencil,                 "pvs_stencil",                 "pvs_stencil.comp"},
        Pass_source{Pass::cones,                   "pvs_cones",                   "pvs_cones.comp"},
        Pass_source{Pass::map_level_0,             "pvs_map_level_0",             "pvs_map.comp"},
        Pass_source{Pass::map_reduce,              "pvs_map_reduce",              "pvs_map.comp"},
        Pass_source{Pass::vote,                    "pvs_vote",                    "pvs_vote.comp"},
        Pass_source{Pass::trace,                   "pvs_trace",                   "pvs_trace.comp"}
    };
    for (const Pass_source& source : sources) {
        const std::size_t index = static_cast<std::size_t>(source.pass);
        Shader_stages_create_info create_info{
            .name              = source.name,
            .interface_blocks  = {
                &m_parameters_block, &m_layered_block, &m_closure_block, &m_column_masks_block,
                &m_tile_masks_block, &m_lists_block, &m_column_words_block, &m_trace_block,
                &m_column_bounds_block, &m_map_block, &m_runs_block
            },
            .shaders           = { { Shader_type::compute_shader, shader_path / std::filesystem::path{source.file} } },
            .bind_group_layout = m_bind_group_layout.get()
        };
        if (source.pass == Pass::map_level_0) {
            create_info.defines.emplace_back("ERHE_PVS_MAP_LEVEL_0", "1");
        }
        // The gap test's constants, the C++ reference's values: the shortest
        // decimal that reads back as the same float (GLSL parses exponent
        // forms too), with a decimal point so the literal stays a float.
        const auto float_literal = [](const float value) -> std::string {
            std::string text = fmt::format("{}", value);
            if (text.find_first_of(".eE") == std::string::npos) {
                text += ".0";
            }
            return text;
        };
        create_info.defines.emplace_back("ERHE_PVS_GAP_SLOPE_TOLERANCE", float_literal(pvs_gap_slope_tolerance));
        create_info.defines.emplace_back("ERHE_PVS_GAP_DEPTH_EPSILON",   float_literal(pvs_gap_depth_epsilon));
        m_shader_stages[index] = std::make_unique<Reloadable_shader_stages>(graphics_device, std::move(create_info));
        graphics_device.get_shader_monitor().add(*m_shader_stages[index]);
        if (!m_shader_stages[index]->shader_stages.is_valid()) {
            log_startup->error("PVS disocclusion passes: {} failed to build", source.file);
            continue;
        }
        m_pipelines[index] = std::make_unique<Compute_pipeline>(
            graphics_device,
            Compute_pipeline_data{
                .name              = source.name,
                .shader_stages     = &m_shader_stages[index]->shader_stages,
                .bind_group_layout = m_bind_group_layout.get()
            }
        );
        if (!m_pipelines[index]->is_valid()) {
            log_startup->error("PVS disocclusion passes: the {} compute pipeline is not valid", source.name);
            m_pipelines[index].reset();
        }
    }
}

Pvs_disocclusion_pipelines::~Pvs_disocclusion_pipelines() noexcept = default;

auto Pvs_disocclusion_pipelines::is_supported() const -> bool
{
    for (const std::unique_ptr<erhe::graphics::Compute_pipeline>& pipeline : m_pipelines) {
        if (!pipeline) {
            return false;
        }
    }
    return true;
}

void Pvs_disocclusion_pipelines::record_face(
    erhe::graphics::Command_buffer&    command_buffer,
    Pvs_disocclusion_buffers&          buffers,
    const erhe::graphics::Buffer&      layered_buffer,
    const std::size_t                  layered_byte_count,
    const Pvs_disocclusion_parameters& parameters,
    const erhe::graphics::Buffer&      column_bounds,
    const std::size_t                  column_bounds_byte_count,
    const erhe::graphics::Buffer&      column_words,
    const Pvs_disocclusion_timers*     timers
)
{
    using namespace erhe::graphics;
    using Usage = Buffer_usage_flag_bit_mask;
    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(is_supported());
    ERHE_VERIFY(parameters.get_tile_count() == buffers.get_tile_count());
    ERHE_VERIFY(parameters.get_tiles_per_layer() == buffers.get_tiles_per_layer());
    ERHE_VERIFY(parameters.get_column_word_count() > 0);

    constexpr uint64_t compute_access = Usage::storage_read | Usage::storage_write | Usage::compute_shader;
    constexpr uint64_t compute_write  = Usage::storage_write | Usage::compute_shader;
    constexpr uint64_t compute_read   = Usage::storage_read | Usage::compute_shader;

    const uint32_t tile_count      = buffers.get_tile_count();
    const uint32_t max_workgroups  = static_cast<uint32_t>(std::max(1, m_graphics_device.get_info().max_compute_workgroup_count[0]));
    const uint32_t tile_workgroups = std::min(std::min((tile_count + workgroup_size - 1) / workgroup_size, uint32_t{65535}), max_workgroups);
    const uint32_t item_workgroups = std::min(std::min(tile_count, c_item_workgroups), max_workgroups);

    // The work buffers of earlier faces and frames -> this face's clears;
    // the closed flags of an earlier face -> this face's; an
    // earlier face's entry word writes -> this face's.
    const Buffer& closure      = buffers.get_closure();
    const Buffer& column_masks = buffers.get_column_masks();
    const Buffer& tile_masks   = buffers.get_tile_masks();
    const Buffer& lists        = buffers.get_lists();
    // (transfer_src: a bake trace copies them after a face.)
    command_buffer.buffer_barrier(column_masks, compute_access | Usage::transfer_src, Usage::transfer_dst, Barrier_scope::earlier_submissions);
    command_buffer.buffer_barrier(tile_masks,   compute_access | Usage::transfer_src, Usage::transfer_dst, Barrier_scope::earlier_submissions);
    command_buffer.buffer_barrier(lists,        compute_access, Usage::transfer_dst, Barrier_scope::earlier_submissions);
    command_buffer.buffer_barrier(closure,      compute_access | Usage::transfer_src, compute_access, Barrier_scope::earlier_submissions);
    command_buffer.buffer_barrier(column_words, compute_access | Usage::transfer_src, compute_access, Barrier_scope::earlier_submissions);
    const Buffer& map = buffers.get_map();
    command_buffer.buffer_barrier(map,          compute_access, compute_access, Barrier_scope::earlier_submissions);
    const Buffer& runs = buffers.get_runs();
    command_buffer.buffer_barrier(runs,         compute_access, compute_access, Barrier_scope::earlier_submissions);
    {
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.fill_buffer(&column_masks, 0, buffers.get_column_masks_bytes(), 0x00u);
        blit.fill_buffer(&tile_masks,   0, buffers.get_tile_masks_bytes(),   0x00u);
        blit.fill_buffer(&lists,        0, Pvs_disocclusion_buffers::lists_header_words * sizeof(uint32_t), 0x00u);
    }
    command_buffer.buffer_barrier(column_masks, Usage::transfer_dst, compute_access);
    command_buffer.buffer_barrier(tile_masks,   Usage::transfer_dst, compute_access);
    command_buffer.buffer_barrier(lists,        Usage::transfer_dst, compute_access);

    // The parameter block per dispatch layer (pass.x): the tiles and vote
    // passes read layer 0's copy. Ring ranges: a frame never reads another
    // frame's values.
    const uint32_t    layer_count     = parameters.get_layer_count();
    const uint32_t    map_levels      = parameters.get_map_level_count();
    const uint32_t    pass_count      = std::max(layer_count, map_levels);
    const std::size_t parameter_bytes = parameters.get_byte_count();
    // Member scratch (cleared, capacity kept): no allocation per face once
    // the capacities reach their high-water marks.
    std::vector<Ring_buffer_range>& parameter_ranges = m_parameter_ranges;
    parameter_ranges.clear();
    Pvs_disocclusion_parameters& layer_parameters = m_layer_parameters;
    layer_parameters = parameters;
    for (uint32_t layer = 0; layer < pass_count; ++layer) {
        layer_parameters.pass = glm::uvec4{layer, 0u, 0u, 0u};
        Ring_buffer_range range = m_parameter_buffer.acquire(Ring_buffer_usage::CPU_write, parameter_bytes);
        layer_parameters.write(range.get_span());
        range.bytes_written(parameter_bytes);
        range.close();
        parameter_ranges.push_back(std::move(range));
    }

    const std::size_t column_bytes = parameters.get_column_word_count() * sizeof(uint32_t);
    const auto run = [&](const Pass pass, const uint32_t layer, const uint32_t workgroups, Gpu_timer* timer) {
        // The trace binding holds the column words outside a trace (no
        // pass but pvs_trace.comp reads it).
        dispatch(command_buffer, pass, workgroups, parameter_ranges[layer], buffers, layered_buffer, layered_byte_count, column_bounds, column_bounds_byte_count, column_words, column_bytes, column_words, column_bytes, timer);
    };
    const uint32_t tiles_per_layer = parameters.get_tiles_per_layer();
    const uint32_t layer_workgroups = std::min(std::min((tiles_per_layer + workgroup_size - 1) / workgroup_size, uint32_t{65535}), max_workgroups);

    // tiles (coverage and the contiguous-run closure, every layer)
    run(Pass::tiles, 0, layer_workgroups, (timers != nullptr) ? timers->tiles : nullptr);
    command_buffer.buffer_barrier(closure,      compute_write, compute_access);
    command_buffer.buffer_barrier(runs,         compute_write, compute_read);
    // The tiles pass atomicOrs the runs' bits into the column masks; the
    // first classify dispatch reads and atomicOrs them too.
    command_buffer.buffer_barrier(column_masks, compute_write, compute_access);

    // per layer, front to back: classify the layer's windows (gated by the
    // bits of the layers in front), then scatter its cones
    {
        std::optional<Scoped_gpu_timer> timer_scope{};
        if ((timers != nullptr) && (timers->layer_loop != nullptr)) {
            timer_scope.emplace(*timers->layer_loop, command_buffer);
        }
        for (uint32_t layer = 0; (layer + 1u) < layer_count; ++layer) {
            run(Pass::stencil, layer, layer_workgroups, nullptr);
            command_buffer.buffer_barrier(lists,        compute_write, compute_access);
            command_buffer.buffer_barrier(column_masks, compute_write, compute_access);
            run(Pass::cones, layer, item_workgroups, nullptr);
            command_buffer.buffer_barrier(tile_masks, compute_write, compute_access);
        }
    }

    // the disoccluded map: level 0 per chunk, then each level the "any" of
    // the level below
    {
        std::optional<Scoped_gpu_timer> timer_scope{};
        if ((timers != nullptr) && (timers->map != nullptr)) {
            timer_scope.emplace(*timers->map, command_buffer);
        }
        command_buffer.buffer_barrier(tile_masks, compute_write, compute_read);
        run(Pass::map_level_0, 0, tile_workgroups, nullptr);
        command_buffer.buffer_barrier(map, compute_write, compute_access);
        for (uint32_t level = 1; level < map_levels; ++level) {
            run(Pass::map_reduce, level, tile_workgroups, nullptr);
            command_buffer.buffer_barrier(map, compute_write, compute_access);
        }
    }

    // the bounds vote, one invocation per column
    const uint32_t column_count      = std::max(1u, parameters.get_column_count());
    const uint32_t column_workgroups = std::min(std::min((column_count + workgroup_size - 1) / workgroup_size, uint32_t{65535}), max_workgroups);
    run(Pass::vote, 0, column_workgroups, (timers != nullptr) ? timers->vote : nullptr);

    for (Ring_buffer_range& range : parameter_ranges) {
        range.release();
    }
    parameter_ranges.clear();
}

void Pvs_disocclusion_pipelines::dispatch(
    erhe::graphics::Command_buffer&          command_buffer,
    const Pass                               pass,
    const uint32_t                           workgroups,
    const erhe::graphics::Ring_buffer_range& parameter_range,
    Pvs_disocclusion_buffers&                buffers,
    const erhe::graphics::Buffer&            layered_buffer,
    const std::size_t                        layered_byte_count,
    const erhe::graphics::Buffer&            column_bounds,
    const std::size_t                        column_bounds_byte_count,
    const erhe::graphics::Buffer&            column_words,
    const std::size_t                        column_words_byte_count,
    const erhe::graphics::Buffer&            trace_buffer,
    const std::size_t                        trace_byte_count,
    erhe::graphics::Gpu_timer*               timer
)
{
    using namespace erhe::graphics;
    std::optional<Scoped_gpu_timer> timer_scope{};
    if (timer != nullptr) {
        timer_scope.emplace(*timer, command_buffer);
    }
    Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
    encoder.set_bind_group_layout(m_bind_group_layout.get());
    encoder.set_compute_pipeline(*m_pipelines[static_cast<std::size_t>(pass)]);
    m_parameter_buffer.bind(encoder, parameter_range);
    encoder.set_buffer(Buffer_target::storage, &layered_buffer,             0, layered_byte_count,               layered_binding_point);
    encoder.set_buffer(Buffer_target::storage, &buffers.get_closure(),      0, buffers.get_closure_bytes(),      closure_binding_point);
    encoder.set_buffer(Buffer_target::storage, &buffers.get_column_masks(), 0, buffers.get_column_masks_bytes(), column_masks_binding_point);
    encoder.set_buffer(Buffer_target::storage, &buffers.get_tile_masks(),   0, buffers.get_tile_masks_bytes(),   tile_masks_binding_point);
    encoder.set_buffer(Buffer_target::storage, &buffers.get_lists(),        0, buffers.get_lists_bytes(),        lists_binding_point);
    encoder.set_buffer(Buffer_target::storage, &column_words,                0, column_words_byte_count,           column_words_binding_point);
    encoder.set_buffer(Buffer_target::storage, &trace_buffer,               0, trace_byte_count,                 trace_binding_point);
    encoder.set_buffer(Buffer_target::storage, &column_bounds,              0, column_bounds_byte_count,         column_bounds_binding_point);
    encoder.set_buffer(Buffer_target::storage, &buffers.get_map(),          0, buffers.get_map_bytes(),          map_binding_point);
    encoder.set_buffer(Buffer_target::storage, &buffers.get_runs(),  0, buffers.get_runs_bytes(),  runs_binding_point);
    encoder.dispatch_compute(workgroups, 1, 1);
}

void Pvs_disocclusion_pipelines::record_trace(
    erhe::graphics::Command_buffer&    command_buffer,
    Pvs_disocclusion_buffers&          buffers,
    const erhe::graphics::Buffer&      layered_buffer,
    const std::size_t                  layered_byte_count,
    const Pvs_disocclusion_parameters& parameters,
    const erhe::graphics::Buffer&      trace_buffer,
    const std::size_t                  trace_byte_count
)
{
    using namespace erhe::graphics;
    using Usage = Buffer_usage_flag_bit_mask;
    ERHE_VERIFY(is_supported());
    // The vote's tile and column mask writes -> the trace's reads (the
    // caller ordered the trace buffer's writes).
    command_buffer.buffer_barrier(buffers.get_tile_masks(),   Usage::storage_write | Usage::compute_shader, Usage::storage_read | Usage::compute_shader);
    command_buffer.buffer_barrier(buffers.get_column_masks(), Usage::storage_write | Usage::compute_shader, Usage::storage_read | Usage::compute_shader);
    const std::size_t parameter_bytes = parameters.get_byte_count();
    Ring_buffer_range parameter_range = m_parameter_buffer.acquire(Ring_buffer_usage::CPU_write, parameter_bytes);
    parameters.write(parameter_range.get_span());
    parameter_range.bytes_written(parameter_bytes);
    parameter_range.close();
    const uint32_t max_workgroups = static_cast<uint32_t>(std::max(1, m_graphics_device.get_info().max_compute_workgroup_count[0]));
    const uint32_t workgroups     = std::min(uint32_t{16384}, max_workgroups);
    dispatch(command_buffer, Pass::trace, workgroups, parameter_range, buffers, layered_buffer, layered_byte_count, trace_buffer, trace_byte_count, trace_buffer, trace_byte_count, trace_buffer, trace_byte_count, nullptr);
    parameter_range.release();
}

} // namespace erhe::scene_renderer
