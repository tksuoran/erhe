#include "erhe_graphics/null/null_device.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_graphics/null/null_command_buffer.hpp"
#include "erhe_graphics/null/null_surface.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_graphics/texture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <iterator>

namespace erhe::graphics {

Device_impl::Device_impl(Device& device, const Surface_create_info& surface_create_info, const Graphics_config& graphics_config)
    : m_device         {device}
    , m_graphics_config{graphics_config}
    , m_shader_monitor {device}
{

    // Initialize Device_info with reasonable defaults for headless/server mode
    m_info.glsl_version                             = 460;
    m_info.api_info                                 = "Null";
    m_info.max_texture_size                         = 16384;
    m_info.sub_pixel_precision_bits                 = 8;
    m_info.max_samples                              = 8;
    m_info.max_color_texture_samples                = 8;
    m_info.max_depth_texture_samples                = 8;
    m_info.max_framebuffer_samples                  = 8;
    m_info.max_integer_samples                      = 8;
    m_info.max_vertex_attribs                       = 16;
    m_info.use_persistent_buffers                   = true;
    m_info.use_multi_draw_indirect_core             = true;
    m_info.uniform_buffer_offset_alignment          = 256;
    m_info.shader_storage_buffer_offset_alignment   = 256;
    m_info.max_uniform_buffer_bindings              = 84;
    m_info.max_shader_storage_buffer_bindings       = 16;
    m_info.max_combined_texture_image_units         = 32;
    m_info.max_3d_texture_size                      = 2048;
    m_info.max_cube_map_texture_size                = 16384;
    m_info.max_texture_buffer_size                  = 134217728;
    m_info.max_array_texture_layers                 = 2048;
    m_info.max_uniform_block_size                   = 65536;
    m_info.max_per_stage_descriptor_samplers        = 16;
    m_info.texture_heap_size = max_texture_heap_size;
    m_info.max_compute_workgroup_count[0]           = 65535;
    m_info.max_compute_workgroup_count[1]           = 65535;
    m_info.max_compute_workgroup_count[2]           = 65535;
    m_info.max_compute_workgroup_size[0]            = 1024;
    m_info.max_compute_workgroup_size[1]            = 1024;
    m_info.max_compute_workgroup_size[2]            = 64;
    m_info.max_compute_work_group_invocations       = 1024;
    m_info.max_compute_shared_memory_size           = 49152;
    m_info.max_vertex_shader_storage_blocks         = 16;
    m_info.max_vertex_uniform_blocks                = 14;
    m_info.max_vertex_uniform_vectors               = 4096;
    m_info.max_fragment_shader_storage_blocks       = 16;
    m_info.max_fragment_uniform_blocks              = 14;
    m_info.max_fragment_uniform_vectors             = 4096;
    m_info.max_geometry_shader_storage_blocks       = 16;
    m_info.max_geometry_uniform_blocks              = 14;
    m_info.max_tess_control_shader_storage_blocks   = 16;
    m_info.max_tess_control_uniform_blocks          = 14;
    m_info.max_tess_evaluation_shader_storage_blocks = 16;
    m_info.max_tess_evaluation_uniform_blocks       = 14;
    m_info.max_compute_shader_storage_blocks        = 16;
    m_info.max_compute_uniform_blocks               = 14;
    m_info.max_texture_max_anisotropy               = 16.0f;
    m_info.max_depth_layers                         = 4;
    m_info.max_depth_resolution                     = 4096;

    if (surface_create_info.context_window != nullptr) {
        m_surface = std::make_unique<Surface>(
            std::make_unique<Surface_impl>(*this, surface_create_info)
        );
    }
}

Device_impl::~Device_impl() noexcept = default;

void Device_impl::wait_for_frame_start(const uint64_t frame_id)
{
    static_cast<void>(frame_id);
}

auto Device_impl::get_frame_slot_index(const uint64_t frame_id) const -> std::size_t
{
    return static_cast<std::size_t>(frame_id % s_number_of_frames_in_flight);
}

auto Device_impl::acquire_frame_slot(const uint64_t frame_id) -> bool
{
    // Order rule (doc/erhe/graphics.md "Frame lifecycle"): frames are
    // acquired strictly in id order, at most N open.
    ERHE_VERIFY(frame_id == m_prepare_frame_id + 1);
    ERHE_VERIFY(frame_id >= m_render_frame_id);
    ERHE_VERIFY(frame_id - m_render_frame_id < s_number_of_frames_in_flight);
    const std::size_t  slot_index = get_frame_slot_index(frame_id);
    Device_frame_slot& slot       = m_frame_slots[slot_index];
    ERHE_VERIFY(!slot.open);
    m_command_buffers[slot_index].clear();
    m_prepare_frame_id = frame_id;
    slot.frame_id      = frame_id;
    slot.open          = true;
    return true;
}

auto Device_impl::begin_frame() -> bool
{
    return true;
}

auto Device_impl::begin_frame(const Frame_begin_info& frame_begin_info) -> bool
{
    static_cast<void>(frame_begin_info);
    return true;
}

auto Device_impl::end_frame() -> bool
{
    return end_frame(m_render_frame_id);
}

auto Device_impl::end_frame(const uint64_t frame_id) -> bool
{
    // Contract: end_frame only ends the oldest open frame. See
    // doc/erhe/graphics.md ("Frame lifecycle") and the matching
    // Vulkan implementation for the rationale.
    ERHE_VERIFY(frame_id == m_render_frame_id);
    Device_frame_slot& slot = m_frame_slots[get_frame_slot_index(frame_id)];
    ERHE_VERIFY(slot.open && (slot.frame_id == frame_id));
    slot.open = false;
    ++m_render_frame_id;
    // Nothing executes on a GPU: the frame just ended is complete.
    run_completion_handlers(frame_id);
    return true;
}

auto Device_impl::end_frame(const Frame_end_info& frame_end_info) -> bool
{
    // Legacy compat overload; Frame_end_info is no longer used.
    static_cast<void>(frame_end_info);
    return end_frame();
}


auto Device_impl::recreate_surface_for_new_window() -> bool
{
    return false;
}

void Device_impl::wait_idle()
{
    run_completion_handlers();
}

void Device_impl::clear_render_pipeline_cache()
{
    // No-op for null backend (no pipeline cache to invalidate).
}

auto Device_impl::is_in_swapchain_frame() const -> bool
{
    return false;
}

auto Device_impl::get_surface() -> Surface*
{
    return m_surface.get();
}

auto Device_impl::get_native_handles() const -> Native_device_handles
{
    return Native_device_handles{};
}

auto Device_impl::wait_for_displayed_frame(const std::int64_t frame_id, const uint64_t timeout_ns) -> Present_wait_result
{
    static_cast<void>(frame_id);
    static_cast<void>(timeout_ns);
    return Present_wait_result::unsupported;
}

void Device_impl::set_present_target_time(const std::int64_t frame_id, const double target_time_seconds, const double hold_until_seconds)
{
    static_cast<void>(frame_id);
    static_cast<void>(target_time_seconds);
    static_cast<void>(hold_until_seconds);
}

auto Device_impl::get_frame_pacing_tier() const -> Frame_pacing_tier
{
    return Frame_pacing_tier::off;
}

auto Device_impl::is_present_target_time_honored() const -> bool
{
    return false;
}

void Device_impl::memory_barrier(const Memory_barrier_mask barriers)
{
    static_cast<void>(barriers);
    // No-op for null backend
}

void Device_impl::clear_texture(const Texture& texture, const std::array<double, 4> clear_value)
{
    static_cast<void>(texture);
    static_cast<void>(clear_value);
    // No-op for null backend
}

void Device_impl::start_frame_capture() {}
void Device_impl::end_frame_capture() {}
void Device_impl::set_queue_annotation    (const char* key, const bool        value) { static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_queue_annotation    (const char* key, const int32_t     value) { static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_queue_annotation    (const char* key, const uint32_t    value) { static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_queue_annotation    (const char* key, const int64_t     value) { static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_queue_annotation    (const char* key, const uint64_t    value) { static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_queue_annotation    (const char* key, const float       value) { static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_queue_annotation    (const char* key, const double      value) { static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_queue_annotation    (const char* key, const char*       value) { static_cast<void>(key); static_cast<void>(value); }
void Device_impl::clear_queue_annotation  (const char* key)                          { static_cast<void>(key); }
void Device_impl::set_command_annotation  (const char* key, const bool        value) { static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_command_annotation  (const char* key, const int32_t     value) { static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_command_annotation  (const char* key, const uint32_t    value) { static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_command_annotation  (const char* key, const int64_t     value) { static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_command_annotation  (const char* key, const uint64_t    value) { static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_command_annotation  (const char* key, const float       value) { static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_command_annotation  (const char* key, const double      value) { static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_command_annotation  (const char* key, const char*       value) { static_cast<void>(key); static_cast<void>(value); }
void Device_impl::clear_command_annotation(const char* key)                          { static_cast<void>(key); }
void Device_impl::set_object_annotation   (const uint64_t object_handle, const char* key, const bool     value) { static_cast<void>(object_handle); static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_object_annotation   (const uint64_t object_handle, const char* key, const int32_t  value) { static_cast<void>(object_handle); static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_object_annotation   (const uint64_t object_handle, const char* key, const uint32_t value) { static_cast<void>(object_handle); static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_object_annotation   (const uint64_t object_handle, const char* key, const int64_t  value) { static_cast<void>(object_handle); static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_object_annotation   (const uint64_t object_handle, const char* key, const uint64_t value) { static_cast<void>(object_handle); static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_object_annotation   (const uint64_t object_handle, const char* key, const float    value) { static_cast<void>(object_handle); static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_object_annotation   (const uint64_t object_handle, const char* key, const double   value) { static_cast<void>(object_handle); static_cast<void>(key); static_cast<void>(value); }
void Device_impl::set_object_annotation   (const uint64_t object_handle, const char* key, const char*    value) { static_cast<void>(object_handle); static_cast<void>(key); static_cast<void>(value); }
void Device_impl::clear_object_annotation (const uint64_t object_handle, const char* key)                       { static_cast<void>(object_handle); static_cast<void>(key); }

void Device_impl::transition_texture_layout(const Texture& texture, Image_layout new_layout)
{
    static_cast<void>(texture);
    static_cast<void>(new_layout);
}

void Device_impl::cmd_texture_barrier(uint64_t usage_before, uint64_t usage_after)
{
    static_cast<void>(usage_before);
    static_cast<void>(usage_after);
}

auto Device_impl::get_command_buffer(unsigned int thread_slot) -> Command_buffer&
{
    // A recording handle whose recording methods are no-ops, kept in the
    // vector of the calling role's frame slot until that slot's next
    // acquire_frame_slot().
    const uint64_t    frame_id   = get_frame_index();
    const std::size_t slot_index = get_frame_slot_index(frame_id);
    std::vector<std::unique_ptr<Command_buffer>>& command_buffers = m_command_buffers[slot_index];
    auto command_buffer = std::make_unique<Command_buffer>(
        m_device,
        erhe::utility::Debug_label{
            fmt::format("Null cb (frame_slot={}, thread_slot={}, allocation_index={})", slot_index, thread_slot, command_buffers.size())
        }
    );
    command_buffer->get_impl().set_frame_id(frame_id);
    Command_buffer& reference = *command_buffer;
    command_buffers.push_back(std::move(command_buffer));
    return reference;
}

void Device_impl::submit_command_buffers(std::span<Command_buffer* const> command_buffers)
{
    static_cast<void>(command_buffers);
    // No-op for null backend; iteration target.
}

void Device_impl::submit_command_buffer_and_wait(Command_buffer& command_buffer)
{
    static_cast<void>(command_buffer);
    // No-op for null backend; there is no GPU work to wait for.
}

void Device_impl::upload_to_buffer(const Buffer& buffer, const size_t offset, const void* data, const size_t length)
{
    static_cast<void>(buffer);
    static_cast<void>(offset);
    static_cast<void>(data);
    static_cast<void>(length);
    // No-op for null backend
}

void Device_impl::add_completion_handler(std::function<void(Device_impl&)> callback)
{
    // Not dropped: the handler releases resources (staging buffers, ring
    // buffer ranges, Mesh_memory retired pool ranges) that would otherwise
    // leak. Fired by end_frame() of the stamped frame / wait_idle().
    m_completion_handlers.push_back(Completion_handler{.frame_number = get_frame_index(), .callback = std::move(callback)});
}

void Device_impl::run_completion_handlers(const uint64_t last_frame)
{
    // Handlers may add handlers; collect the due ones first. The scratch
    // vector keeps its capacity across frames.
    m_due_completion_handlers.clear();
    const std::vector<Completion_handler>::iterator keep_end = std::stable_partition(
        m_completion_handlers.begin(),
        m_completion_handlers.end(),
        [last_frame](const Completion_handler& entry) { return entry.frame_number > last_frame; }
    );
    std::move(keep_end, m_completion_handlers.end(), std::back_inserter(m_due_completion_handlers));
    m_completion_handlers.erase(keep_end, m_completion_handlers.end());
    for (const Completion_handler& entry : m_due_completion_handlers) {
        entry.callback(*this);
    }
}

void Device_impl::on_thread_enter()
{
    // No-op for null backend
}

auto Device_impl::get_handle(const Texture& texture, const Sampler& sampler) const -> uint64_t
{
    static_cast<void>(texture);
    static_cast<void>(sampler);
    return 0;
}

auto Device_impl::create_dummy_texture(Command_buffer& /*init_command_buffer*/, const erhe::dataformat::Format format) -> std::shared_ptr<Texture>
{
    const Texture_create_info create_info{
        .device       = m_device,
        .usage_mask   = Image_usage_flag_bit_mask::sampled | Image_usage_flag_bit_mask::transfer_dst,
        .type         = Texture_type::texture_2d,
        .pixelformat  = format,
        .use_mipmaps  = false,
        .sample_count = 0,
        .width        = 2,
        .height       = 2,
        .debug_label  = erhe::utility::Debug_label{"null dummy texture"}
    };
    return std::make_shared<Texture>(m_device, create_info);
}

auto Device_impl::get_buffer_alignment(const Buffer_target target) -> std::size_t
{
    switch (target) {
        case Buffer_target::storage: {
            return m_info.shader_storage_buffer_offset_alignment;
        }
        case Buffer_target::uniform: {
            return m_info.uniform_buffer_offset_alignment;
        }
        case Buffer_target::draw_indirect: {
            return sizeof(Draw_indexed_primitives_indirect_command);
        }
        default: {
            return 64;
        }
    }
}

// The render role (a dedicated render thread) gets the oldest open frame;
// every other caller the newest acquired frame while it is open, otherwise
// the next frame to be acquired.
auto Device_impl::get_frame_index() const -> uint64_t
{
    if (erhe::utility::Thread_roles::is_render_role()) {
        return m_render_frame_id;
    }
    return std::max(m_prepare_frame_id, m_render_frame_id);
}

auto Device_impl::get_number_of_frames_in_flight() const -> std::size_t
{
    return s_number_of_frames_in_flight;
}

// No GPU executes anything here, so a frame is retired as soon as the frame
// index has moved past it. Stated as "strictly before the current frame"
// rather than "always true" so that a consumer behaves the same way against
// this backend as against a real one: the open frame is never retired, which
// is what keeps a consumer from writing over memory the frame it is still
// recording will read.
auto Device_impl::is_frame_completed(const uint64_t frame) const -> bool
{
    return frame < m_render_frame_id;
}

auto Device_impl::get_debug_state() const -> Device_debug_state
{
    // No GPU and no ring buffers: frames retire when they end.
    Device_debug_state state{};
    state.oldest_open_frame_id     = m_render_frame_id;
    state.open_frame_count         = (m_prepare_frame_id >= m_render_frame_id) ? (m_prepare_frame_id - m_render_frame_id + 1) : 0;
    state.completion_handler_count = m_completion_handlers.size();
    return state;
}

auto Device_impl::allocate_ring_buffer_entry(
    const Buffer_target     buffer_target,
    const Ring_buffer_usage  usage,
    const std::size_t       byte_count
) -> Ring_buffer_range
{
    static_cast<void>(buffer_target);
    static_cast<void>(usage);
    static_cast<void>(byte_count);
    return Ring_buffer_range{};
}

auto Device_impl::make_blit_command_encoder(Command_buffer& command_buffer) -> Blit_command_encoder
{
    return Blit_command_encoder{m_device, command_buffer};
}

auto Device_impl::make_compute_command_encoder(Command_buffer& command_buffer) -> Compute_command_encoder
{
    return Compute_command_encoder{m_device, command_buffer};
}

auto Device_impl::make_render_command_encoder(Command_buffer& command_buffer) -> Render_command_encoder
{
    return Render_command_encoder{m_device, command_buffer};
}

auto Device_impl::get_format_properties(const erhe::dataformat::Format format) const -> Format_properties
{
    static_cast<void>(format);
    Format_properties properties{};
    properties.supported                = true;
    properties.texture_2d_sample_counts = {1};
    return properties;
}

auto Device_impl::probe_image_format_support(const erhe::dataformat::Format format, const uint64_t usage_mask) const -> bool
{
    static_cast<void>(format);
    static_cast<void>(usage_mask);
    return true;
}

auto Device_impl::get_supported_depth_stencil_formats() const -> std::vector<erhe::dataformat::Format>
{
    return std::vector<erhe::dataformat::Format>{
        erhe::dataformat::Format::format_d32_sfloat
    };
}

void Device_impl::sort_depth_stencil_formats(
    std::vector<erhe::dataformat::Format>& formats,
    const unsigned int                     sort_flags,
    const int                              requested_sample_count
) const
{
    static_cast<void>(formats);
    static_cast<void>(sort_flags);
    static_cast<void>(requested_sample_count);
    // No-op for null backend
}

auto Device_impl::choose_depth_stencil_format(const std::vector<erhe::dataformat::Format>& formats) const -> erhe::dataformat::Format
{
    if (!formats.empty()) {
        return formats.front();
    }
    return erhe::dataformat::Format::format_d32_sfloat;
}

auto Device_impl::choose_depth_stencil_format(const unsigned int sort_flags, const int requested_sample_count) const -> erhe::dataformat::Format
{
    static_cast<void>(sort_flags);
    static_cast<void>(requested_sample_count);
    return erhe::dataformat::Format::format_d32_sfloat;
}

auto Device_impl::get_shader_monitor() -> Shader_monitor&
{
    return m_shader_monitor;
}

auto Device_impl::get_info() const -> const Device_info&
{
    return m_info;
}

auto Device_impl::get_graphics_config() const -> const Graphics_config&
{
    return m_graphics_config;
}

void Device_impl::reset_shader_stages_state_tracker()
{
    // No-op for null backend
}

auto Device_impl::get_draw_id_uniform_location() const -> GLint
{
    return -1;
}

} // namespace erhe::graphics
