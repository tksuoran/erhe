// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/device.hpp"

#include "erhe_utility/bit_helpers.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_window/window.hpp"

namespace erhe::graphics {

Device::Device(erhe::window::Context_window& context_window)
    : m_impl{std::make_unique<Device_impl>(*this, context_window)}
{
}
Device::~Device() = default;
auto Device::choose_depth_stencil_format(const unsigned int flags, int sample_count) const -> erhe::dataformat::Format
{
    return m_impl->choose_depth_stencil_format(flags, sample_count);
}
auto Device::get_handle(const Texture& texture, const Sampler& sampler) const -> uint64_t
{
    return m_impl->get_handle(texture, sampler);
}
auto Device::create_dummy_texture() -> std::shared_ptr<Texture>
{
    return m_impl->create_dummy_texture();
}
void Device::upload_to_buffer(Buffer& buffer, size_t offset, const void* data, size_t length)
{
    m_impl->upload_to_buffer(buffer, offset, data, length);
}
void Device::add_completion_handler(std::function<void()> callback)
{
    m_impl->add_completion_handler(callback);
}
void Device::on_thread_enter()
{
    m_impl->on_thread_enter();
}
auto Device::get_buffer_alignment(Buffer_target target) -> std::size_t
{
    return m_impl->get_buffer_alignment(target);
}
void Device::start_of_frame()
{
    m_impl->start_of_frame();
}
void Device::end_of_frame()
{
    m_impl->end_of_frame();
}
auto Device::get_frame_number() const -> uint64_t
{
    return m_impl->get_frame_number();
}
auto Device::allocate_ring_buffer_entry(Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range
{
    return m_impl->allocate_ring_buffer_entry(buffer_target, usage, byte_count);
}
void Device::memory_barrier(Memory_barrier_mask barriers)
{
    m_impl->memory_barrier(barriers);
}
auto Device::get_format_properties(erhe::dataformat::Format format) const -> Format_properties
{
    return m_impl->get_format_properties(format);
}
void Device::clear_texture(Texture& texture, std::array<double, 4> value)
{
    m_impl->clear_texture(texture, value);
}
auto Device::make_render_command_encoder(Render_pass& render_pass) -> Render_command_encoder
{
    return m_impl->make_render_command_encoder(render_pass);
}
auto Device::make_blit_command_encoder() -> Blit_command_encoder
{
    return m_impl->make_blit_command_encoder();
}
auto Device::make_compute_command_encoder() -> Compute_command_encoder
{
    return m_impl->make_compute_command_encoder();
}
auto Device::get_shader_monitor() -> Shader_monitor&
{
    return m_impl->get_shader_monitor();
}
auto Device::get_info() const -> const Device_info&
{
    return m_impl->get_info();
}
auto Device::get_impl() -> Device_impl&
{
    return *m_impl.get();
}
auto Device::get_impl() const -> const Device_impl&
{
    return *m_impl.get();
}

// // // // //

constexpr float c_float_zero_value{0.0f};
constexpr float c_float_one_value {1.0f};

auto get_depth_clear_value_pointer(const bool reverse_depth) -> const float*
{
    return reverse_depth ? &c_float_zero_value : &c_float_one_value;
}

auto get_depth_function(const Compare_operation depth_function, const bool reverse_depth) -> Compare_operation
{
    return reverse_depth ? reverse(depth_function) : depth_function;
}

//
//

// Ring buffer

Buffer_range::Buffer_range()
    : m_ring_buffer                     {nullptr}
    , m_span                            {}
    , m_wrap_count                      {0}
    , m_byte_span_start_offset_in_buffer{0}
    , m_byte_write_position_in_span     {0}
    , m_usage                           {Ring_buffer_usage::None}
{
    SPDLOG_LOGGER_TRACE(log_gpu_ring_buffer, "Buffer_range::Buffer_range()");
}

Buffer_range::Buffer_range(
    GPU_ring_buffer&     ring_buffer,
    Ring_buffer_usage    usage,
    std::span<std::byte> span,
    std::size_t          wrap_count,
    size_t               byte_start_offset_in_buffer
)
    : m_ring_buffer                     {&ring_buffer}
    , m_span                            {span}
    , m_wrap_count                      {wrap_count}
    , m_byte_span_start_offset_in_buffer{byte_start_offset_in_buffer}
    , m_byte_write_position_in_span     {0}
    , m_usage                           {usage}
{
    ERHE_VERIFY(usage != Ring_buffer_usage::None);
    SPDLOG_LOGGER_TRACE(
        log_gpu_ring_buffer,
        "Buffer_range::Buffer_range() buffer = {} start offset in buffer = {} span byte count = {}",
        ring_buffer.get_buffer().debug_label,
        byte_start_offset,
        span.size_bytes()
    );
}

Buffer_range::Buffer_range(Buffer_range&& old) noexcept
    : m_ring_buffer                     {std::exchange(old.m_ring_buffer,                      nullptr)}
    , m_span                            {std::exchange(old.m_span,                             {}     )}
    , m_wrap_count                      {std::exchange(old.m_wrap_count,                       0      )}
    , m_byte_span_start_offset_in_buffer{std::exchange(old.m_byte_span_start_offset_in_buffer, 0      )}
    , m_byte_write_position_in_span     {std::exchange(old.m_byte_write_position_in_span,      0      )}
    , m_byte_flush_position_in_span     {std::exchange(old.m_byte_flush_position_in_span,      0      )}
    , m_usage                           {std::exchange(old.m_usage,                            Ring_buffer_usage::None)}
    , m_is_closed                       {std::exchange(old.m_is_closed,                        true   )}
    , m_is_released                     {std::exchange(old.m_is_released ,                     false  )}
    , m_is_cancelled                    {std::exchange(old.m_is_cancelled,                     true   )}
{
}

Buffer_range& Buffer_range::operator=(Buffer_range&& old) noexcept
{
    m_ring_buffer                      = std::exchange(old.m_ring_buffer,                      nullptr);
    m_span                             = std::exchange(old.m_span,                             {}     );
    m_wrap_count                       = std::exchange(old.m_wrap_count,                       0      );
    m_byte_span_start_offset_in_buffer = std::exchange(old.m_byte_span_start_offset_in_buffer, 0      );
    m_byte_write_position_in_span      = std::exchange(old.m_byte_write_position_in_span,      0      );
    m_byte_flush_position_in_span      = std::exchange(old.m_byte_flush_position_in_span,      0      );
    m_usage                            = std::exchange(old.m_usage,                            Ring_buffer_usage::None);
    m_is_closed                        = std::exchange(old.m_is_closed,                        true   );
    m_is_released                      = std::exchange(old.m_is_released,                      false  );
    m_is_cancelled                     = std::exchange(old.m_is_cancelled,                     true   );
    return *this;
}

Buffer_range::~Buffer_range()
{
    ERHE_VERIFY((m_ring_buffer == nullptr) || m_is_released || m_is_cancelled);
}

void Buffer_range::close()
{
    ERHE_VERIFY(!is_closed());
    ERHE_VERIFY(!m_is_released);
    ERHE_VERIFY(m_ring_buffer != nullptr);
    m_ring_buffer->close(m_byte_span_start_offset_in_buffer, m_byte_write_position_in_span);
    m_is_closed = true;
}

void Buffer_range::bytes_written(std::size_t byte_count)
{
    //ERHE_VERIFY(m_usage == Ring_buffer_usage::CPU_write);
    ERHE_VERIFY(!is_closed());
    ERHE_VERIFY(!m_is_released);
    ERHE_VERIFY(m_byte_write_position_in_span + byte_count <= m_span.size_bytes());
    ERHE_VERIFY(m_ring_buffer != nullptr);
    m_byte_write_position_in_span += byte_count;
}

void Buffer_range::flush(std::size_t byte_write_position_in_span)
{
    ERHE_VERIFY(!is_closed());
    ERHE_VERIFY(!m_is_released);
    ERHE_VERIFY(byte_write_position_in_span >= m_byte_write_position_in_span);
    m_byte_write_position_in_span = byte_write_position_in_span;
    ERHE_VERIFY(m_usage == Ring_buffer_usage::CPU_write);
    ERHE_VERIFY(m_ring_buffer != nullptr);
    const size_t flush_byte_count = byte_write_position_in_span - m_byte_flush_position_in_span;
    if (flush_byte_count > 0) {
        m_ring_buffer->flush(m_byte_flush_position_in_span, flush_byte_count);
    }
    m_byte_flush_position_in_span = byte_write_position_in_span;
}

void Buffer_range::release()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(!m_is_released);
    m_is_released = true;

    if (m_ring_buffer == nullptr) {
        ERHE_VERIFY(m_byte_write_position_in_span == 0);
        return;
    }
    ERHE_VERIFY(is_closed());
    if (m_byte_write_position_in_span == 0) {
        return;
    }

    m_ring_buffer->make_sync_entry(
        m_wrap_count,
        m_byte_span_start_offset_in_buffer,
        m_byte_write_position_in_span
    );
}

void Buffer_range::cancel()
{
    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(!m_is_cancelled);
    m_is_cancelled = true;
    if (m_byte_write_position_in_span == 0) {
        return;
    }

    ERHE_VERIFY(m_ring_buffer != nullptr);
}

auto Buffer_range::get_span() const -> std::span<std::byte>
{
    return m_span;
}

auto Buffer_range::get_byte_start_offset_in_buffer() const -> std::size_t
{
    return m_byte_span_start_offset_in_buffer;
}

auto Buffer_range::get_writable_byte_count() const -> std::size_t
{
    ERHE_VERIFY(m_span.size_bytes() >= m_byte_write_position_in_span);
    return m_span.size_bytes() - m_byte_write_position_in_span;
}

auto Buffer_range::get_written_byte_count() const -> std::size_t
{
    return m_byte_write_position_in_span;
}

auto Buffer_range::is_closed() const -> bool
{
    return m_is_closed;
}

auto Buffer_range::get_buffer() const -> GPU_ring_buffer*
{
    return m_ring_buffer;
}

GPU_ring_buffer::GPU_ring_buffer(Device& device, const GPU_ring_buffer_create_info& create_info)
    : m_impl{std::make_unique<GPU_ring_buffer_impl>(device, *this, create_info)}
{
}
GPU_ring_buffer::~GPU_ring_buffer() = default;
void GPU_ring_buffer::get_size_available_for_write(
    std::size_t  required_alignment,
    std::size_t& out_alignment_byte_count_without_wrap,
    std::size_t& out_available_byte_count_without_wrap,
    std::size_t& out_available_byte_count_with_wrap
) const
{
    m_impl->get_size_available_for_write(
        required_alignment,
        out_alignment_byte_count_without_wrap,
        out_available_byte_count_without_wrap,
        out_available_byte_count_with_wrap
    );
}
auto GPU_ring_buffer::acquire(std::size_t required_alignment, Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range
{
    return m_impl->acquire(required_alignment, usage, byte_count);
}
auto GPU_ring_buffer::get_direction() const -> Buffer_direction
{
    return m_impl->get_direction();
}
void GPU_ring_buffer::flush(std::size_t byte_offset, std::size_t byte_count)
{
    return m_impl->flush(byte_offset, byte_count);
}
void GPU_ring_buffer::close(std::size_t byte_offset, std::size_t byte_write_count)
{
    m_impl->close(byte_offset, byte_write_count);
}
void GPU_ring_buffer::make_sync_entry(std::size_t wrap_count, std::size_t byte_offset, std::size_t byte_count)
{
    m_impl->make_sync_entry(wrap_count, byte_offset, byte_count);
}
auto GPU_ring_buffer::get_buffer() -> Buffer*
{
    return m_impl->get_buffer();
}
void GPU_ring_buffer::frame_completed(uint64_t frame)
{
    m_impl->frame_completed(frame);
}

GPU_ring_buffer_client::GPU_ring_buffer_client(
    Device&                     graphics_device,
    Buffer_target               buffer_target,
    std::string_view            debug_label,
    std::optional<unsigned int> binding_point
)
    : m_graphics_device{graphics_device}
    , m_buffer_target  {buffer_target}
    , m_debug_label    {debug_label}
    , m_binding_point  {binding_point}
{
}

auto GPU_ring_buffer_client::acquire(Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range
{
    ERHE_VERIFY(byte_count > 0);
    return m_graphics_device.allocate_ring_buffer_entry(m_buffer_target, usage, byte_count);
}

auto GPU_ring_buffer_client::bind(Command_encoder& command_encoder, const Buffer_range& range) -> bool
{
    ERHE_PROFILE_FUNCTION();

    // sanity_check();

    const size_t offset     = range.get_byte_start_offset_in_buffer();
    const size_t byte_count = range.get_written_byte_count();
    if (byte_count == 0) {
        return false;
    }

    GPU_ring_buffer* ring_buffer = range.get_buffer();
    ERHE_VERIFY(ring_buffer != nullptr);
    Buffer* buffer = ring_buffer->get_buffer();
    ERHE_VERIFY(buffer != nullptr);

    SPDLOG_LOGGER_TRACE(
        log_gpu_ring_buffer,
        "binding {} {} buffer offset = {} byte count = {}",
        m_name,
        m_binding_point.has_value() ? "uses binding point" : "non-indexed binding",
        m_binding_point.has_value() ? m_binding_point.value() : 0,
        range.first_byte_offset,
        range.byte_count
    );

    ERHE_VERIFY(
        (m_buffer_target != Buffer_target::uniform) ||
        (byte_count <= static_cast<std::size_t>(m_graphics_device.get_info().max_uniform_block_size))
    );
    ERHE_VERIFY(offset + byte_count <= buffer->get_capacity_byte_count());

    if (m_binding_point.has_value()) {
        ERHE_VERIFY(is_indexed(m_buffer_target));
        command_encoder.set_buffer(m_buffer_target, buffer, offset, byte_count, m_binding_point.value());
    } else {
        ERHE_VERIFY(!is_indexed(m_buffer_target));
        command_encoder.set_buffer(m_buffer_target, buffer);
    }
    return true;
}

Texture_heap::Texture_heap(
    Device&        device,
    const Texture& fallback_texture,
    const Sampler& fallback_sampler,
    std::size_t    reserved_slot_count
)
    : m_impl{std::make_unique<Texture_heap_impl>(device, fallback_texture, fallback_sampler, reserved_slot_count)}
{
}

Texture_heap::~Texture_heap() = default;

void Texture_heap::reset()
{
    m_impl->reset();
}

auto Texture_heap::get_shader_handle(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    return m_impl->get_shader_handle(texture, sampler);
}

auto Texture_heap::assign(std::size_t slot, const Texture* texture, const Sampler* sampler) -> uint64_t
{
    return m_impl->assign(slot, texture, sampler);
}

auto Texture_heap::allocate(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    return m_impl->allocate(texture, sampler);
}

// TODO Maybe this should use Render_command_encoder?
void Texture_heap::unbind()
{
    m_impl->unbind();
}

// TODO Maybe this should use Render_command_encoder?
auto Texture_heap::bind() -> std::size_t
{
    return m_impl->bind();
}


} // namespace erhe::graphics
