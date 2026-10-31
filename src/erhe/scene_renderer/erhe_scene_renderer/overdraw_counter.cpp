#include "erhe_scene_renderer/overdraw_counter.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstring>

namespace erhe::scene_renderer {

namespace {

// The result layout of overdraw_reduce.comp: total, max, covered, 0, then
// the histogram.
constexpr std::size_t c_result_header_uint_count = 4;
constexpr std::size_t c_result_uint_count        = c_result_header_uint_count + Overdraw_statistics::histogram_bin_count;
constexpr std::size_t c_result_byte_count        = c_result_uint_count * sizeof(uint32_t);

} // anonymous namespace

Overdraw_view_state::Overdraw_view_state() = default;

Overdraw_view_state::~Overdraw_view_state() noexcept = default;

auto Overdraw_view_state::get_statistics() const -> Overdraw_statistics
{
    const std::lock_guard<std::mutex> lock{m_statistics_mutex};
    return m_statistics;
}

auto Overdraw_view_state::get_count_buffer(const uint64_t frame_index) const -> const erhe::graphics::Buffer*
{
    return (m_count_frame_index == frame_index) ? m_count_buffer.get() : nullptr;
}

void Overdraw_view_state::retire(const uint64_t frame_index)
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
    ERHE_VERIFY((slot.frame_index + m_readback_slots.size()) <= frame_index);
    const std::size_t          offset = slot_index * readback_slot_stride;
    const std::span<std::byte> data   = m_readback_buffer->map_bytes(offset, readback_slot_stride);
    m_readback_buffer->invalidate(offset, readback_slot_stride);
    ERHE_VERIFY(data.size_bytes() >= c_result_byte_count);
    std::array<uint32_t, c_result_uint_count> values{};
    std::memcpy(values.data(), data.data(), c_result_byte_count);
    m_readback_buffer->unmap();
    slot.pending = false;

    Overdraw_statistics statistics{
        .valid               = true,
        .frame_index         = slot.frame_index,
        .overdraw            = slot.overdraw,
        .width               = slot.width,
        .height              = slot.height,
        .invocation_total    = values[0],
        .covered_pixel_count = values[2],
        .max_count           = values[1]
    };
    std::copy_n(values.begin() + c_result_header_uint_count, Overdraw_statistics::histogram_bin_count, statistics.histogram.begin());
    const std::lock_guard<std::mutex> lock{m_statistics_mutex};
    m_statistics = statistics;
}

void Overdraw_view_state::begin_frame(
    erhe::graphics::Device&         graphics_device,
    erhe::graphics::Command_buffer& command_buffer,
    const uint64_t                  frame_index,
    const Overdraw_count            overdraw,
    const uint32_t                  width,
    const uint32_t                  height
)
{
    using namespace erhe::graphics;

    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY((width > 0) && (height > 0));
    ERHE_VERIFY(overdraw != Overdraw_count::off);

    retire(frame_index);

    const std::size_t count_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * sizeof(uint32_t);
    const bool        reused      = m_count_buffer && (m_count_buffer->get_capacity_byte_count() >= count_bytes);
    if (!reused) {
        // Device memory: the fragment atomics of every counted fragment
        // land here. Replaced on growth; the old one is destroyed when the
        // frames that used it complete.
        m_count_buffer = std::make_unique<Buffer>(
            graphics_device,
            Buffer_create_info{
                .capacity_byte_count               = count_bytes,
                .usage                             = Buffer_usage::storage | Buffer_usage::transfer_dst,
                .required_memory_property_bit_mask = Memory_property_flag_bit_mask::device_local,
                .debug_label                       = erhe::utility::Debug_label{"Overdraw counts"}
            }
        );
    }
    m_width             = width;
    m_height            = height;
    m_count_frame_index = frame_index;
    m_overdraw          = overdraw;

    if (reused) {
        // The previous frame's reduction and visualization read the counts.
        command_buffer.buffer_barrier(
            *m_count_buffer,
            Buffer_usage_flag_bit_mask::storage_read | Buffer_usage_flag_bit_mask::compute_shader | Buffer_usage_flag_bit_mask::fragment_shader,
            Buffer_usage_flag_bit_mask::transfer_dst,
            Barrier_scope::earlier_submissions
        );
    }
    {
        Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);
        blit.fill_buffer(m_count_buffer.get(), 0, count_bytes, 0);
    }
    // The clear -> the counting draws' atomics.
    command_buffer.buffer_barrier(
        *m_count_buffer,
        Buffer_usage_flag_bit_mask::transfer_dst,
        Buffer_usage_flag_bit_mask::storage_read | Buffer_usage_flag_bit_mask::storage_write | Buffer_usage_flag_bit_mask::fragment_shader,
        Barrier_scope::command_buffer
    );
}

void Overdraw_view_state::release()
{
    m_count_buffer.reset();
    m_readback_buffer.reset();
    m_readback_slots.clear();
    m_width             = 0;
    m_height            = 0;
    m_count_frame_index = 0;
    const std::lock_guard<std::mutex> lock{m_statistics_mutex};
    m_statistics = Overdraw_statistics{};
}

Overdraw_counter::Overdraw_counter(erhe::graphics::Device& graphics_device, const std::filesystem::path& shader_path)
    : m_graphics_device{graphics_device}
    , m_input_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "overdraw_reduce_input",
            .binding_point = static_cast<int>(input_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_counts_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "overdraw_reduce_counts",
            .binding_point = static_cast<int>(counts_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_result_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "overdraw_reduce_result",
            .binding_point = static_cast<int>(result_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    }
    , m_input_buffer {graphics_device, erhe::graphics::Buffer_target::storage, "Overdraw_counter input",  input_binding_point}
    , m_result_buffer{graphics_device, erhe::graphics::Buffer_target::storage, "Overdraw_counter result", result_binding_point}
{
    using namespace erhe::graphics;

    m_input_block .add_uvec4("header");
    m_counts_block.add_uint ("counts", Shader_resource::unsized_array);
    m_result_block.add_uint ("values", Shader_resource::unsized_array);

    if (!graphics_device.get_info().fragment_stores_and_atomics) {
        log_startup->info("Overdraw counting: not supported on this device (needs fragment stage buffer atomics)");
        return;
    }

    m_bind_group_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                { .binding_point = input_binding_point,  .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = counts_binding_point, .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = result_binding_point, .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute }
            },
            .debug_label       = "Overdraw_counter",
            .uses_texture_heap = false
        }
    );

    m_shader_stages = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name              = "overdraw_reduce",
            .defines           = {
                { "ERHE_OVERDRAW_REDUCE_GROUP_SIZE",   fmt::format("{}", workgroup_size) },
                { "ERHE_OVERDRAW_HISTOGRAM_LAST_BIN",  fmt::format("{}u", Overdraw_statistics::histogram_last_bin) }
            },
            .interface_blocks  = { &m_input_block, &m_counts_block, &m_result_block },
            .shaders           = { { Shader_type::compute_shader, shader_path / std::filesystem::path{"overdraw_reduce.comp"} } },
            .bind_group_layout = m_bind_group_layout.get()
        }
    );
    graphics_device.get_shader_monitor().add(*m_shader_stages);
    if (!m_shader_stages->shader_stages.is_valid()) {
        log_startup->error("Overdraw counting: overdraw_reduce.comp failed to build; no overdraw statistics");
        return;
    }
    m_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "overdraw_reduce",
            .shader_stages     = &m_shader_stages->shader_stages,
            .bind_group_layout = m_bind_group_layout.get()
        }
    );
    if (!m_pipeline->is_valid()) {
        log_startup->error("Overdraw counting: the overdraw_reduce compute pipeline is not valid; no overdraw statistics");
        m_pipeline.reset();
    }
}

Overdraw_counter::~Overdraw_counter() noexcept = default;

auto Overdraw_counter::is_supported() const -> bool
{
    return m_pipeline != nullptr;
}

void Overdraw_counter::reduce(erhe::graphics::Command_buffer& command_buffer, Overdraw_view_state& view_state, const uint64_t frame_index)
{
    using namespace erhe::graphics;

    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(is_supported());
    ERHE_VERIFY(view_state.m_count_buffer);
    ERHE_VERIFY(view_state.m_count_frame_index == frame_index);

    const uint32_t    pixel_count = view_state.m_width * view_state.m_height;
    const std::size_t count_bytes = static_cast<std::size_t>(pixel_count) * sizeof(uint32_t);

    // The counting draws' atomics -> the reduction's reads.
    command_buffer.buffer_barrier(
        *view_state.m_count_buffer,
        Buffer_usage_flag_bit_mask::storage_read | Buffer_usage_flag_bit_mask::storage_write | Buffer_usage_flag_bit_mask::fragment_shader,
        Buffer_usage_flag_bit_mask::storage_read | Buffer_usage_flag_bit_mask::compute_shader,
        Barrier_scope::command_buffer
    );

    constexpr std::size_t input_bytes = 4 * sizeof(uint32_t);
    Ring_buffer_range input_range = m_input_buffer.acquire(Ring_buffer_usage::CPU_write, input_bytes);
    {
        const std::array<uint32_t, 4> header{pixel_count, 0u, 0u, 0u};
        std::memcpy(input_range.get_span().data(), header.data(), input_bytes);
    }
    input_range.bytes_written(input_bytes);
    input_range.close();

    // Zeroed by the host; the dispatch accumulates into it.
    Ring_buffer_range result_range = m_result_buffer.acquire(Ring_buffer_usage::CPU_write, c_result_byte_count);
    std::memset(result_range.get_span().data(), 0, c_result_byte_count);
    result_range.bytes_written(c_result_byte_count);
    result_range.close();

    const std::size_t group_count = std::clamp<std::size_t>((pixel_count + workgroup_size - 1) / workgroup_size, 1, max_group_count);
    {
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_bind_group_layout.get());
        encoder.set_compute_pipeline(*m_pipeline);
        m_input_buffer.bind(encoder, input_range);
        encoder.set_buffer(Buffer_target::storage, view_state.m_count_buffer.get(), 0, count_bytes, counts_binding_point);
        m_result_buffer.bind(encoder, result_range);
        encoder.dispatch_compute(static_cast<std::uintptr_t>(group_count), 1, 1);
    }
    // The reduction's writes -> the copy below.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_storage_barrier_bit | Memory_barrier_mask::pixel_buffer_barrier_bit);

    // The result, decoded by the frame that reuses this frame's slot
    // (Overdraw_view_state::retire()).
    constexpr std::size_t stride = Overdraw_view_state::readback_slot_stride;
    static_assert(stride >= c_result_byte_count);
    const std::size_t slot_count = m_graphics_device.get_number_of_frames_in_flight();
    if (!view_state.m_readback_buffer) {
        // Created once per mode-on period: the device's frames in flight,
        // one slot each.
        view_state.m_readback_slots.clear();
        view_state.m_readback_slots.resize(slot_count);
        view_state.m_readback_buffer = std::make_unique<Buffer>(
            m_graphics_device,
            Buffer_create_info{
                .capacity_byte_count                    = slot_count * stride,
                .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
                .usage                                  = Buffer_usage::transfer_dst,
                .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read | Memory_property_flag_bit_mask::host_write,
                .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_coherent | Memory_property_flag_bit_mask::host_persistent,
                .debug_label                            = erhe::utility::Debug_label{"Overdraw statistics readback"}
            }
        );
    }
    ERHE_VERIFY(view_state.m_readback_slots.size() == slot_count);
    const std::size_t                   slot_index = frame_index % slot_count;
    Overdraw_view_state::Readback_slot& readback   = view_state.m_readback_slots[slot_index];
    // The slot's previous readback was decoded by begin_frame() of this frame.
    ERHE_VERIFY(!readback.pending);
    {
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_buffer(
            result_range.get_buffer()->get_buffer(), result_range.get_byte_start_offset_in_buffer(),
            view_state.m_readback_buffer.get(), slot_index * stride,
            c_result_byte_count
        );
    }
    command_buffer.buffer_barrier(*view_state.m_readback_buffer, Buffer_usage_flag_bit_mask::transfer_dst, Buffer_usage_flag_bit_mask::host_read, Barrier_scope::command_buffer);
    readback.frame_index = frame_index;
    readback.overdraw    = view_state.m_overdraw;
    readback.width       = view_state.m_width;
    readback.height      = view_state.m_height;
    readback.pending     = true;

    input_range.release();
    result_range.release();
}

} // namespace erhe::scene_renderer
