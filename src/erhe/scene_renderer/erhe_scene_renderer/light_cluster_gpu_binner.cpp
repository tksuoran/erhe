#include "erhe_scene_renderer/light_cluster_gpu_binner.hpp"
#include "erhe_scene_renderer/light_cluster.hpp"
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
#include <cstring>
#include <optional>

namespace erhe::scene_renderer {

auto c_str(const Light_cluster_binning binning) -> const char*
{
    switch (binning) {
        case Light_cluster_binning::cpu: return "cpu";
        case Light_cluster_binning::gpu: return "gpu";
        default:                         return "?";
    }
}

namespace {

[[nodiscard]] auto read_uint(const std::vector<std::byte>& data, const std::size_t uint_index) -> uint32_t
{
    uint32_t value = 0;
    if (((uint_index + 1) * sizeof(uint32_t)) <= data.size()) {
        std::memcpy(&value, data.data() + (uint_index * sizeof(uint32_t)), sizeof(uint32_t));
    }
    return value;
}

constexpr std::size_t c_max_reported_mismatches = 8;

} // anonymous namespace

// The check of one frame (request_check()): per view binned on the GPU in
// that frame, the CPU binner's lists of the same inputs and the GPU's lists
// and counters, read back once the frame retired. Shared with the
// completion handlers that copy the readbacks; each request starts a new
// generation, and a handler of an older generation only releases its
// readback. A diagnostic path: it allocates.
class Light_cluster_check_data
{
public:
    enum class Stage : unsigned int
    {
        idle,
        requested,
        recording,
        complete
    };

    class View
    {
    public:
        std::string            label;
        glm::uvec4             counts{0u};
        std::size_t            cluster_count     {0};
        std::size_t            binned_light_count{0};
        std::size_t            list_base         {0};
        std::size_t            list_stride       {0};
        std::size_t            cpu_overflow_count{0};
        std::size_t            cpu_max_list_length{0};
        std::vector<uint32_t>  cpu_offsets;          // per cluster + 1, into cpu_slots
        std::vector<uint32_t>  cpu_slots;
        std::vector<uint32_t>  cpu_category_counts;  // 4 per cluster
        std::vector<std::byte> gpu_clusters;
        std::vector<std::byte> gpu_counters;
    };

    std::mutex        mutex;
    uint64_t          generation   {0};
    Stage             stage        {Stage::idle};
    uint64_t          frame_index  {0};
    std::size_t       pending_reads{0};
    uint64_t          first_frame  {0}; // the earliest frame a request may record
    std::vector<View> views;
};

Light_cluster_view_state::Light_cluster_view_state() = default;

Light_cluster_view_state::~Light_cluster_view_state() noexcept = default;

auto Light_cluster_view_state::get_gpu_time_ns() const -> uint64_t
{
    return m_gpu_timer ? m_gpu_timer->last_result() : 0;
}

void Light_cluster_view_state::retire(const uint64_t frame_index)
{
    if (m_readback_slots.empty()) {
        return;
    }
    const std::size_t slot_index = frame_index % m_readback_slots.size();
    Readback_slot&    slot       = m_readback_slots[slot_index];
    if (!slot.pending) {
        return;
    }
    // This frame reuses the readback frame's slot: that frame retired
    // (Device::acquire_frame_slot() waited for it).
    ERHE_VERIFY((slot.stats.frame_index + m_readback_slots.size()) <= frame_index);
    const std::size_t          offset = slot_index * readback_slot_stride;
    const std::span<std::byte> data   = m_readback_buffer->map_bytes(offset, readback_slot_stride);
    m_readback_buffer->invalidate(offset, readback_slot_stride);
    ERHE_VERIFY(data.size_bytes() >= (2 * sizeof(uint32_t)));
    uint32_t counters[2]{0u, 0u};
    std::memcpy(counters, data.data(), sizeof(counters));
    m_readback_buffer->unmap();
    m_stats = slot.stats;
    m_stats.overflow_cluster_count = counters[0];
    m_stats.max_list_length        = counters[1];
    m_stats.valid                  = true;
    slot.pending = false;
}

void Light_cluster_view_state::set_cpu_stats(const uint64_t frame_index, const Light_cluster_binner* const binner)
{
    m_stats = Light_cluster_stats{
        .valid                  = true,
        .frame_index            = frame_index,
        .binning                = Light_cluster_binning::cpu,
        .counts                 = (binner != nullptr) ? binner->get_grid().counts : glm::uvec4{1u, 1u, 1u, 0u},
        .cluster_count          = (binner != nullptr) ? binner->get_grid().get_cluster_count() : 1,
        .binned_light_count     = (binner != nullptr) ? binner->get_binned_light_count() : 0,
        .capacity               = (binner != nullptr) ? binner->get_capacity() : 0,
        .overflow_cluster_count = (binner != nullptr) ? binner->get_overflow_cluster_count() : 0,
        .max_list_length        = (binner != nullptr) ? binner->get_max_list_length() : 0
    };
}

Light_cluster_gpu_binner::Light_cluster_gpu_binner(erhe::graphics::Device& graphics_device, const std::filesystem::path& shader_path)
    : m_graphics_device{graphics_device}
    , m_input_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "light_cluster_bin_input",
            .binding_point = static_cast<int>(input_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_output_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "light_cluster_bin_output",
            .binding_point = static_cast<int>(output_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    }
    , m_counters_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "light_cluster_bin_counters",
            .binding_point = static_cast<int>(counters_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    }
    , m_input_buffer   {graphics_device, erhe::graphics::Buffer_target::storage,      "Light_cluster_gpu_binner inputs",   input_binding_point}
    , m_counter_buffer {graphics_device, erhe::graphics::Buffer_target::storage,      "Light_cluster_gpu_binner counters", counters_binding_point}
    , m_readback_buffer{graphics_device, erhe::graphics::Buffer_target::transfer_dst, "Light_cluster_gpu_binner readback"}
    , m_check          {std::make_shared<Light_cluster_check_data>()}
{
    using namespace erhe::graphics;

    // std430: the uvec4 headers and the vec4 data (16 byte strides), the
    // uint arrays (4 byte strides). Light_cluster_binner::write_inputs()
    // writes the input block in this layout.
    const std::size_t header_offset = m_input_block.add_uvec4("header", c_light_cluster_bin_header_uvec4_count)->get_offset_in_parent();
    const std::size_t data_offset   = m_input_block.add_vec4 ("data",   Shader_resource::unsized_array        )->get_offset_in_parent();
    ERHE_VERIFY(header_offset == 0);
    ERHE_VERIFY(data_offset == (c_light_cluster_bin_header_uvec4_count * sizeof(glm::uvec4)));
    m_output_block  .add_uint("data",     Shader_resource::unsized_array);
    m_counters_block.add_uint("counters", Shader_resource::unsized_array);

    m_bind_group_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                { .binding_point = input_binding_point,    .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = output_binding_point,   .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = counters_binding_point, .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute }
            },
            .debug_label       = "Light_cluster_gpu_binner",
            .uses_texture_heap = false
        }
    );

    m_shader_stages = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name              = "light_cluster_bin",
            .defines           = {
                { "ERHE_LIGHT_CLUSTER_BIN_GROUP_SIZE",    fmt::format("{}", workgroup_size) },
                { "ERHE_LIGHT_CLUSTER_HEADER_UINT_COUNT", fmt::format("{}u", c_light_cluster_header_uint_count) }
            },
            .interface_blocks  = { &m_input_block, &m_output_block, &m_counters_block },
            .shaders           = { { Shader_type::compute_shader, shader_path / std::filesystem::path{"light_cluster_bin.comp"} } },
            .bind_group_layout = m_bind_group_layout.get()
        }
    );
    graphics_device.get_shader_monitor().add(*m_shader_stages);
    if (!m_shader_stages->shader_stages.is_valid()) {
        log_startup->error("Light cluster binning: light_cluster_bin.comp failed to build; views are binned on the CPU");
        return;
    }
    m_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "light_cluster_bin",
            .shader_stages     = &m_shader_stages->shader_stages,
            .bind_group_layout = m_bind_group_layout.get()
        }
    );
    if (!m_pipeline->is_valid()) {
        log_startup->error("Light cluster binning: the light_cluster_bin compute pipeline is not valid; views are binned on the CPU");
        m_pipeline.reset();
    }
}

Light_cluster_gpu_binner::~Light_cluster_gpu_binner() noexcept = default;

auto Light_cluster_gpu_binner::is_supported() const -> bool
{
    return m_pipeline != nullptr;
}

void Light_cluster_gpu_binner::dispatch(
    erhe::graphics::Command_buffer& command_buffer,
    Light_cluster_binner&           binner,
    const erhe::graphics::Buffer&   clusters,
    const std::size_t               byte_offset,
    const std::size_t               byte_count,
    Light_cluster_view_state&       view_state,
    const uint64_t                  frame_index,
    const std::string_view          label
)
{
    using namespace erhe::graphics;

    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(is_supported());
    ERHE_VERIFY(byte_count >= binner.get_fixed_byte_count());

    const std::size_t cluster_count = binner.get_grid().get_cluster_count();
    const std::size_t group_count   = (cluster_count + workgroup_size - 1) / workgroup_size;
    ERHE_VERIFY(group_count <= static_cast<std::size_t>(m_graphics_device.get_info().max_compute_workgroup_count[0]));

    // A requested check records this frame (the first one binned on the GPU
    // after the request); its generation, else none.
    std::optional<uint64_t> check_generation{};
    {
        const std::lock_guard<std::mutex> lock{m_check->mutex};
        if ((m_check->stage == Light_cluster_check_data::Stage::requested) && (frame_index >= m_check->first_frame)) {
            m_check->stage       = Light_cluster_check_data::Stage::recording;
            m_check->frame_index = frame_index;
        }
        if ((m_check->stage == Light_cluster_check_data::Stage::recording) && (m_check->frame_index == frame_index)) {
            check_generation = m_check->generation;
        }
    }

    const std::size_t input_bytes = binner.get_input_byte_count();
    Ring_buffer_range input_range = m_input_buffer.acquire(Ring_buffer_usage::CPU_write, input_bytes);
    binner.write_inputs(input_range.get_span());
    input_range.bytes_written(input_bytes);
    input_range.close();

    // Zeroed by the host; the dispatch accumulates into it.
    Ring_buffer_range counter_range = m_counter_buffer.acquire(Ring_buffer_usage::CPU_write, counter_bytes);
    std::memset(counter_range.get_span().data(), 0, counter_bytes);
    counter_range.bytes_written(counter_bytes);
    counter_range.close();

    if (!view_state.m_gpu_timer) {
        // Named after the view the first dispatch bins (one timer per view).
        view_state.m_gpu_timer_label = fmt::format("Light cluster binning {}", label);
        view_state.m_gpu_timer       = std::make_unique<Gpu_timer>(m_graphics_device, view_state.m_gpu_timer_label.c_str());
    }
    {
        const Scoped_gpu_timer timer_scope{*view_state.m_gpu_timer, command_buffer};
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_bind_group_layout.get());
        encoder.set_compute_pipeline(*m_pipeline);
        m_input_buffer.bind(encoder, input_range);
        encoder.set_buffer(Buffer_target::storage, &clusters, byte_offset, byte_count, output_binding_point);
        m_counter_buffer.bind(encoder, counter_range);
        encoder.dispatch_compute(static_cast<std::uintptr_t>(group_count), 1, 1);
    }
    // Shader writes -> the frame's fragment reads of the lists, and the
    // copies below.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_storage_barrier_bit | Memory_barrier_mask::pixel_buffer_barrier_bit);

    // The overflow counters of this frame, decoded by the frame that reuses
    // this frame's slot (Light_cluster_view_state::retire()).
    {
        constexpr std::size_t stride     = Light_cluster_view_state::readback_slot_stride;
        static_assert(stride >= counter_bytes);
        const std::size_t     slot_count = m_graphics_device.get_number_of_frames_in_flight();
        if (!view_state.m_readback_buffer) {
            // Created once: the device's frames in flight, one slot each.
            view_state.m_readback_slots.resize(slot_count);
            view_state.m_readback_buffer = std::make_unique<Buffer>(
                m_graphics_device,
                Buffer_create_info{
                    .capacity_byte_count                    = slot_count * stride,
                    .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
                    .usage                                  = Buffer_usage::transfer_dst,
                    .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read | Memory_property_flag_bit_mask::host_write,
                    .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_coherent | Memory_property_flag_bit_mask::host_persistent,
                    .debug_label                            = erhe::utility::Debug_label{"Light_cluster_view_state counter readback"}
                }
            );
        }
        ERHE_VERIFY(view_state.m_readback_slots.size() == slot_count);
        const std::size_t slot_index = frame_index % slot_count;
        Light_cluster_view_state::Readback_slot& readback = view_state.m_readback_slots[slot_index];
        // The slot's previous readback was decoded by retire() of this frame.
        ERHE_VERIFY(!readback.pending);
        {
            Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
            blit.copy_from_buffer(
                counter_range.get_buffer()->get_buffer(), counter_range.get_byte_start_offset_in_buffer(),
                view_state.m_readback_buffer.get(), slot_index * stride,
                counter_bytes
            );
        }
        readback.stats = Light_cluster_stats{
            .valid              = false,
            .frame_index        = frame_index,
            .binning            = Light_cluster_binning::gpu,
            .counts             = binner.get_grid().counts,
            .cluster_count      = cluster_count,
            .binned_light_count = binner.get_binned_light_count(),
            .capacity           = binner.get_capacity()
        };
        readback.pending = true;
    }

    if (check_generation.has_value()) {
        // The CPU reference of the same inputs, and the GPU lists and
        // counters read back once the frame retired.
        binner.bin_prepared();
        Light_cluster_check_data::View view{};
        view.label               = std::string{label};
        view.counts              = binner.get_grid().counts;
        view.cluster_count       = cluster_count;
        view.binned_light_count  = binner.get_binned_light_count();
        view.list_base           = c_light_cluster_header_uint_count * cluster_count;
        view.list_stride         = binner.get_fixed_list_stride();
        view.cpu_overflow_count  = binner.get_overflow_cluster_count();
        view.cpu_max_list_length = binner.get_max_list_length();
        view.cpu_offsets.resize(cluster_count + 1);
        view.cpu_category_counts.resize(4 * cluster_count);
        for (std::size_t cluster = 0; cluster < cluster_count; ++cluster) {
            const std::span<const uint32_t> slots  = binner.get_cluster_slots(cluster);
            const std::span<const uint32_t> counts = binner.get_cluster_category_counts(cluster);
            view.cpu_offsets[cluster] = static_cast<uint32_t>(view.cpu_slots.size());
            view.cpu_slots.insert(view.cpu_slots.end(), slots.begin(), slots.end());
            std::copy(counts.begin(), counts.end(), view.cpu_category_counts.begin() + (4 * cluster));
        }
        view.cpu_offsets[cluster_count] = static_cast<uint32_t>(view.cpu_slots.size());

        std::size_t view_index = 0;
        {
            const std::lock_guard<std::mutex> lock{m_check->mutex};
            if ((m_check->generation != check_generation.value()) || (m_check->stage != Light_cluster_check_data::Stage::recording)) {
                check_generation.reset();
            } else {
                view_index = m_check->views.size();
                m_check->views.push_back(std::move(view));
                m_check->pending_reads += 2;
            }
        }
        if (check_generation.has_value()) {
            const auto read_back = [&](const Buffer* source, const std::size_t source_offset, const std::size_t read_bytes, std::vector<std::byte> Light_cluster_check_data::View::* target) {
                std::shared_ptr<Ring_buffer_range> range = std::make_shared<Ring_buffer_range>(
                    m_readback_buffer.acquire(Ring_buffer_usage::CPU_read, read_bytes)
                );
                {
                    Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
                    blit.copy_from_buffer(
                        source, source_offset,
                        range->get_buffer()->get_buffer(), range->get_byte_start_offset_in_buffer(),
                        read_bytes
                    );
                }
                m_graphics_device.add_completion_handler(
                    [check = m_check, range, generation = check_generation.value(), view_index, target]() {
                        const std::span<std::byte> gpu_data = range->get_span();
                        {
                            const std::lock_guard<std::mutex> lock{check->mutex};
                            if ((check->generation == generation) && (check->stage == Light_cluster_check_data::Stage::recording)) {
                                ((check->views[view_index]).*target).assign(gpu_data.begin(), gpu_data.end());
                                --check->pending_reads;
                                if (check->pending_reads == 0) {
                                    check->stage = Light_cluster_check_data::Stage::complete;
                                }
                            }
                        }
                        range->bytes_gpu_used(gpu_data.size_bytes());
                        range->close();
                        range->release();
                    }
                );
            };
            read_back(&clusters, byte_offset, binner.get_fixed_byte_count(), &Light_cluster_check_data::View::gpu_clusters);
            read_back(counter_range.get_buffer()->get_buffer(), counter_range.get_byte_start_offset_in_buffer(), counter_bytes, &Light_cluster_check_data::View::gpu_counters);
        }
    }

    input_range.release();
    counter_range.release();
}

void Light_cluster_gpu_binner::request_check()
{
    // A new generation: pending readbacks of an earlier request find a newer
    // generation and are dropped.
    const std::lock_guard<std::mutex> lock{m_check->mutex};
    ++m_check->generation;
    m_check->stage         = Light_cluster_check_data::Stage::requested;
    m_check->frame_index   = 0;
    m_check->pending_reads = 0;
    m_check->first_frame   = m_graphics_device.get_frame_index() + 1;
    m_check->views.clear();
}

auto Light_cluster_gpu_binner::poll_check(Light_cluster_check& out) -> Light_cluster_check_state
{
    const std::lock_guard<std::mutex> lock{m_check->mutex};
    Light_cluster_check_data& check = *m_check;
    switch (check.stage) {
        case Light_cluster_check_data::Stage::idle:      return Light_cluster_check_state::idle;
        case Light_cluster_check_data::Stage::requested: return Light_cluster_check_state::pending;
        case Light_cluster_check_data::Stage::recording: return Light_cluster_check_state::pending;
        case Light_cluster_check_data::Stage::complete:  break;
        default:                                         return Light_cluster_check_state::idle;
    }

    out = Light_cluster_check{};
    out.frame_index = check.frame_index;
    for (const Light_cluster_check_data::View& view : check.views) {
        Light_cluster_view_check view_check{};
        view_check.label               = view.label;
        view_check.counts              = view.counts;
        view_check.cluster_count       = view.cluster_count;
        view_check.binned_light_count  = view.binned_light_count;
        view_check.listed_count        = view.cpu_slots.size();
        view_check.cpu_overflow_count  = view.cpu_overflow_count;
        view_check.cpu_max_list_length = view.cpu_max_list_length;
        view_check.gpu_overflow_count  = read_uint(view.gpu_counters, 0);
        view_check.gpu_max_list_length = read_uint(view.gpu_counters, 1);
        for (std::size_t cluster = 0; cluster < view.cluster_count; ++cluster) {
            const std::size_t header      = c_light_cluster_header_uint_count * cluster;
            const uint32_t    list_offset = read_uint(view.gpu_clusters, header + 0);
            const uint32_t    spot        = read_uint(view.gpu_clusters, header + 1);
            const uint32_t    point       = read_uint(view.gpu_clusters, header + 2);
            Light_cluster_mismatch mismatch{};
            mismatch.cluster          = static_cast<uint32_t>(cluster);
            mismatch.gpu_counts       = { spot & 0xffffu, spot >> 16u, point & 0xffffu, point >> 16u };
            mismatch.gpu_offset_wrong = (list_offset != (view.list_base + (cluster * view.list_stride)));
            for (std::size_t category = 0; category < 4; ++category) {
                mismatch.cpu_counts[category] = view.cpu_category_counts[(4 * cluster) + category];
            }
            const std::size_t gpu_count = mismatch.gpu_counts[0] + mismatch.gpu_counts[1] + mismatch.gpu_counts[2] + mismatch.gpu_counts[3];
            bool differs = mismatch.gpu_offset_wrong || (mismatch.gpu_counts != mismatch.cpu_counts);
            const std::size_t cpu_begin = view.cpu_offsets[cluster];
            const std::size_t cpu_end   = view.cpu_offsets[cluster + 1];
            if (!differs) {
                for (std::size_t i = 0; i < gpu_count; ++i) {
                    const uint32_t packed   = read_uint(view.gpu_clusters, list_offset + (i >> 1u));
                    const uint32_t gpu_slot = ((i & 1u) == 0u) ? (packed & 0xffffu) : (packed >> 16u);
                    if (((cpu_begin + i) >= cpu_end) || (view.cpu_slots[cpu_begin + i] != gpu_slot)) {
                        differs = true;
                        break;
                    }
                }
            }
            if (!differs) {
                continue;
            }
            ++view_check.mismatch_count;
            if (view_check.mismatches.size() < c_max_reported_mismatches) {
                mismatch.cpu_slots.assign(view.cpu_slots.begin() + cpu_begin, view.cpu_slots.begin() + cpu_end);
                if (!mismatch.gpu_offset_wrong) {
                    for (std::size_t i = 0; i < std::min(gpu_count, std::size_t{0xffff}); ++i) {
                        const uint32_t packed = read_uint(view.gpu_clusters, list_offset + (i >> 1u));
                        mismatch.gpu_slots.push_back(((i & 1u) == 0u) ? (packed & 0xffffu) : (packed >> 16u));
                    }
                }
                view_check.mismatches.push_back(std::move(mismatch));
            }
        }
        out.mismatch_count += view_check.mismatch_count;
        if (
            (view_check.cpu_overflow_count  != view_check.gpu_overflow_count) ||
            (view_check.cpu_max_list_length != view_check.gpu_max_list_length)
        ) {
            ++out.mismatch_count;
        }
        out.views.push_back(std::move(view_check));
    }
    return Light_cluster_check_state::complete;
}

} // namespace erhe::scene_renderer
