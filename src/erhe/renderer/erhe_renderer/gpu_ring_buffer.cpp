// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_renderer/gpu_ring_buffer.hpp"
#include "erhe_renderer/renderer_log.hpp"

#include "erhe_bit/bit_helpers.hpp"
#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::renderer{

namespace {

static constexpr gl::Buffer_storage_mask storage_mask_persistent{
    gl::Buffer_storage_mask::map_coherent_bit   |
    gl::Buffer_storage_mask::map_persistent_bit |
    gl::Buffer_storage_mask::map_write_bit
};
static constexpr gl::Buffer_storage_mask storage_mask_not_persistent{
    gl::Buffer_storage_mask::map_write_bit
};
inline auto storage_mask(erhe::graphics::Instance& instance) -> gl::Buffer_storage_mask
{
    return instance.info.use_persistent_buffers
        ? storage_mask_persistent
        : storage_mask_not_persistent;
}

static constexpr gl::Map_buffer_access_mask access_mask_persistent{
    gl::Map_buffer_access_mask::map_coherent_bit   |
    gl::Map_buffer_access_mask::map_persistent_bit |
    gl::Map_buffer_access_mask::map_write_bit
};
static constexpr gl::Map_buffer_access_mask access_mask_not_persistent{
    gl::Map_buffer_access_mask::map_write_bit
};

inline auto access_mask(erhe::graphics::Instance& instance) -> gl::Map_buffer_access_mask
{
    return instance.info.use_persistent_buffers
        ? access_mask_persistent
        : access_mask_not_persistent;
}

}

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
    size_t               byte_start_offset
)
    : m_ring_buffer                     {&ring_buffer}
    , m_span                            {span}
    , m_wrap_count                      {wrap_count}
    , m_byte_span_start_offset_in_buffer{byte_start_offset}
    , m_byte_write_position_in_span     {0}
    , m_usage                           {usage}
{
    ERHE_VERIFY(usage != Ring_buffer_usage::None);
    SPDLOG_LOGGER_TRACE(
        log_gpu_ring_buffer,
        "Buffer_range::Buffer_range() buffer = {} start offset = {} span byte count = {}",
        ring_buffer.get_buffer().debug_label,
        byte_start_offset,
        span.size_bytes()
    );
}

Buffer_range::Buffer_range(Buffer_range&& old)
    : m_ring_buffer                     {std::exchange(old.m_ring_buffer,                      nullptr)}
    , m_span                            {std::exchange(old.m_span,                             {}     )}
    , m_wrap_count                      {std::exchange(old.m_wrap_count,                       0      )}
    , m_byte_span_start_offset_in_buffer{std::exchange(old.m_byte_span_start_offset_in_buffer, 0      )}
    , m_byte_write_position_in_span     {std::exchange(old.m_byte_write_position_in_span,      0      )}
    , m_byte_flush_position_in_span     {std::exchange(old.m_byte_flush_position_in_span,      0      )}
    , m_usage                           {std::exchange(old.m_usage,                            Ring_buffer_usage::None)}
    , m_is_closed                       {std::exchange(old.m_is_closed,                        true   )} 
    , m_is_submitted                    {std::exchange(old.m_is_submitted,                     false  )} 
    , m_is_cancelled                    {std::exchange(old.m_is_cancelled,                     true   )} 
{
}
 

Buffer_range& Buffer_range::operator=(Buffer_range&& old)
{
    m_ring_buffer                      = std::exchange(old.m_ring_buffer,                      nullptr);
    m_span                             = std::exchange(old.m_span,                             {}     );
    m_wrap_count                       = std::exchange(old.m_wrap_count,                       0      );
    m_byte_span_start_offset_in_buffer = std::exchange(old.m_byte_span_start_offset_in_buffer, 0      );
    m_byte_write_position_in_span      = std::exchange(old.m_byte_write_position_in_span,      0      );
    m_byte_flush_position_in_span      = std::exchange(old.m_byte_flush_position_in_span,      0      );
    m_usage                            = std::exchange(old.m_usage,                            Ring_buffer_usage::None);
    m_is_closed                        = std::exchange(old.m_is_closed,                        true   );
    m_is_submitted                     = std::exchange(old.m_is_submitted,                     false  );
    m_is_cancelled                     = std::exchange(old.m_is_cancelled,                     true   );
    return *this;
}

Buffer_range::~Buffer_range()
{
    //ERHE_VERIFY((m_ring_buffer == nullptr) || m_is_submitted || m_is_cancelled);
}

void Buffer_range::close(std::size_t byte_write_position_in_span)
{
    ERHE_VERIFY(!is_closed());
    ERHE_VERIFY(!m_is_submitted);
    ERHE_VERIFY(byte_write_position_in_span >= m_byte_write_position_in_span);
    ERHE_VERIFY(byte_write_position_in_span <= m_span.size_bytes());
    ERHE_VERIFY(m_ring_buffer != nullptr);
    m_byte_write_position_in_span = byte_write_position_in_span;
    m_ring_buffer->close(m_byte_span_start_offset_in_buffer, m_byte_write_position_in_span);
    m_is_closed = true;
}

void Buffer_range::flush(std::size_t byte_write_position_in_span)
{
    ERHE_VERIFY(m_usage == Ring_buffer_usage::CPU_write);
    ERHE_VERIFY(!is_closed());
    ERHE_VERIFY(!m_is_submitted);
    ERHE_VERIFY(byte_write_position_in_span >= m_byte_write_position_in_span);
    ERHE_VERIFY(byte_write_position_in_span >= m_byte_flush_position_in_span);
    ERHE_VERIFY(m_ring_buffer != nullptr);
    const size_t flush_byte_count = byte_write_position_in_span - m_byte_flush_position_in_span;
    if (flush_byte_count > 0) {
        m_ring_buffer->flush(m_byte_flush_position_in_span, flush_byte_count);
    }
    m_byte_write_position_in_span = byte_write_position_in_span;
    m_byte_flush_position_in_span = byte_write_position_in_span;
}

auto Buffer_range::bind() -> bool
{
    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(is_closed());
    ERHE_VERIFY(!m_is_submitted);

    if (m_ring_buffer == nullptr) {
        return false;
    }
    return m_ring_buffer->bind(*this);
}

void Buffer_range::submit()
{
    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(is_closed());
    ERHE_VERIFY(!m_is_submitted);
    m_is_submitted = true;
    if (m_byte_write_position_in_span == 0) {
        return;
    }

    ERHE_VERIFY(m_ring_buffer != nullptr);
    m_ring_buffer->push(
        Sync_entry{
            .wrap_count  = m_wrap_count,
            .byte_offset = m_byte_span_start_offset_in_buffer,
            .byte_count  = m_byte_write_position_in_span,
            .fence_sync  = gl::fence_sync(gl::Sync_condition::sync_gpu_commands_complete, 0),
            .result      = gl::Sync_status::timeout_expired
        }
    );
}

void Buffer_range::cancel()
{
    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(is_closed());
    m_is_cancelled = true;
    if (m_byte_write_position_in_span == 0) {
        return;
    }

    ERHE_VERIFY(m_ring_buffer != nullptr);
    m_ring_buffer->push(
        Sync_entry{
            .wrap_count  = m_wrap_count,
            .byte_offset = m_byte_span_start_offset_in_buffer,
            .byte_count  = m_byte_write_position_in_span,
            .fence_sync  = nullptr,
            .result      = gl::Sync_status::condition_satisfied
        }
    );
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
    return m_span.size_bytes();
}

auto Buffer_range::get_written_byte_count() const -> std::size_t
{
    return m_byte_write_position_in_span;
}

auto Buffer_range::is_closed() const -> bool
{
    return m_is_closed;
}

//
//

GPU_ring_buffer::GPU_ring_buffer(
    erhe::graphics::Instance&          graphics_instance,
    const GPU_ring_buffer_create_info& create_info
)
    : m_instance     {graphics_instance}
    , m_binding_point{create_info.binding_point}
    , m_buffer{
        m_instance,
        erhe::graphics::Buffer_create_info{
            .target              = create_info.target,
            .capacity_byte_count = create_info.size,
            .storage_mask        = storage_mask(m_instance),
            .access_mask         = access_mask(m_instance),
            .debug_label         = create_info.debug_label
        }
    }
    , m_read_wrap_count{0}
    , m_read_offset    {m_buffer.capacity_byte_count()}
{
    ERHE_VERIFY(gl_helpers::is_indexed(create_info.target) || create_info.binding_point == std::numeric_limits<unsigned int>::max());

    log_gpu_ring_buffer->info(
        "allocating GPU ring buffer {} bytes for {} - {} binding {}",
        create_info.size,
        m_buffer.debug_label(),
        gl::c_str(m_buffer.target()),
        m_binding_point
    );
}

auto GPU_ring_buffer::get_buffer() -> erhe::graphics::Buffer&
{
    return m_buffer;
}

auto GPU_ring_buffer::get_name() const -> const std::string&
{
    return m_name;
}

void GPU_ring_buffer::sanity_check()
{
    if (m_write_wrap_count == m_read_wrap_count + 1) {
        ERHE_VERIFY(m_read_offset >= m_write_position);
    } else if (m_read_wrap_count == m_write_wrap_count) {
        ERHE_VERIFY(m_write_position >= m_read_offset);
    } else {
        ERHE_FATAL("buffer issue");
    }
}

void GPU_ring_buffer::get_size_available_for_write(
    std::size_t& out_alignment_byte_count_without_wrap,
    std::size_t& out_available_byte_count_without_wrap,
    std::size_t& out_available_byte_count_with_wrap
) const
{
    // Initial situation:
    //   +--------------------------+
    //   ^                          ^
    //   w1                         r0

    //  CPU progress:
    //   +------+---------------+---+
    //                          ^   ^
    //                          w1  r0

    //  GPU progress:
    //   +------+---------------+---+
    //          ^               ^    
    //          r1              w1   

    //  CPU progress:
    //   +----+---+---+--------------+-+
    //        ^   ^                     
    //        w2  r1                    
    gl::Buffer_target target = m_buffer.target();
    const std::size_t aligned_write_position = m_instance.align_buffer_offset(target, m_write_position);
    out_alignment_byte_count_without_wrap    = aligned_write_position - m_write_position;

    if (m_write_wrap_count == m_read_wrap_count + 1) {
        ERHE_VERIFY(m_read_offset >= m_write_position);
        out_available_byte_count_without_wrap = m_read_offset - m_write_position;
        out_available_byte_count_with_wrap = 0; // cannot wrap because GPU is still using the buffer from this point
        if (out_available_byte_count_without_wrap > out_alignment_byte_count_without_wrap) {
            out_available_byte_count_without_wrap -= out_alignment_byte_count_without_wrap;
        } else {
            out_alignment_byte_count_without_wrap = 0;
            out_available_byte_count_without_wrap = 0;
        }
        return;
    } else if (m_read_wrap_count == m_write_wrap_count) {
        ERHE_VERIFY(m_write_position >= m_read_offset);
        out_available_byte_count_without_wrap = m_buffer.capacity_byte_count() - m_write_position;
        out_available_byte_count_with_wrap = m_read_offset;
        if (out_available_byte_count_without_wrap > out_alignment_byte_count_without_wrap) {
            out_available_byte_count_without_wrap -= out_alignment_byte_count_without_wrap;
        } else {
            out_alignment_byte_count_without_wrap = 0;
            out_available_byte_count_without_wrap = 0;
        }
        return;
    } else {
        ERHE_FATAL("buffer issue");
        out_available_byte_count_without_wrap = 0;
        out_available_byte_count_with_wrap = 0;
        return;
    }
}

auto GPU_ring_buffer::open(Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    std::size_t alignment_byte_count_without_wrap{0};
    std::size_t available_byte_count_without_wrap{0};
    std::size_t available_byte_count_with_wrap   {0};
    bool update_called{false};
    bool wrap{false};

    for (;;) {
        sanity_check();

        get_size_available_for_write(alignment_byte_count_without_wrap, available_byte_count_without_wrap, available_byte_count_with_wrap);

        if (byte_count == 0) {
            byte_count = std::max(available_byte_count_without_wrap, available_byte_count_with_wrap);
        }

        wrap = (byte_count > available_byte_count_without_wrap);
        if (wrap && (byte_count > available_byte_count_with_wrap)) {
            update_entries();
            if (!update_called) { // Instant retry after first call to update_entries();
                update_called = true;
                continue;
            }

            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            bool show_warning = true;
            if (m_last_warning_time.has_value()) {
                const auto duration = now - m_last_warning_time.value();
                if (duration < std::chrono::milliseconds(500)) {
                    show_warning = false;
                }
            }
            m_last_warning_time = std::chrono::steady_clock::now();
            if (show_warning) {
                log_gpu_ring_buffer->warn("CPU stalling waiting for GPU");
            }
            gl::finish();
            std::this_thread::yield();
            continue;
        } else {
            break;
        }
    }

    if (wrap) {
        wrap_write();
    } else {
        m_write_position += alignment_byte_count_without_wrap;
    }

    sanity_check();

    if (!m_instance.info.use_persistent_buffers) {
        if (usage == Ring_buffer_usage::CPU_write) {
            m_buffer.begin_write(m_write_position, byte_count); // maps requested range
        }
        return Buffer_range{*this, usage, m_buffer.map(), m_write_wrap_count, m_write_position};
    } else {
        return Buffer_range{*this, usage, m_buffer.map().subspan(m_write_position, byte_count), m_write_wrap_count, m_write_position};
    }
}

void GPU_ring_buffer::flush(std::size_t byte_offset, std::size_t byte_count)
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    if (!m_instance.info.use_persistent_buffers) {
        m_buffer.flush_bytes(byte_offset, byte_count);
    }
}

void GPU_ring_buffer::close(std::size_t byte_offset, std::size_t byte_write_count)
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    m_write_position += byte_write_count;

    sanity_check();

    if (!m_instance.info.use_persistent_buffers) {
        m_buffer.end_write(byte_offset, byte_write_count); // flush and unmap
    }
    m_map = {};
}

GPU_ring_buffer::~GPU_ring_buffer()
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    while (!m_sync_entries.empty()) {
        Sync_entry& entry = m_sync_entries.front();
        if (entry.fence_sync != nullptr) {
            gl::delete_sync((GLsync)(entry.fence_sync));
        }
        m_sync_entries.pop_front();
    }
}

void GPU_ring_buffer::update_entries()
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    // Keep track of how how GPU has completed reads
    while (!m_sync_entries.empty()) {
        Sync_entry& entry = m_sync_entries.front();

        if (entry.result != gl::Sync_status::condition_satisfied) {
            entry.result = gl::client_wait_sync((GLsync)(entry.fence_sync), gl::Sync_object_mask::sync_flush_commands_bit, 0);
        }

        if (
            (entry.result == gl::Sync_status::already_signaled) ||
            (entry.result == gl::Sync_status::condition_satisfied)
        ) {
            // If entry is ready, we can update read position and remove the entry
            gl::delete_sync((GLsync)(entry.fence_sync));
            size_t new_read_wrap_count = entry.wrap_count;
            size_t new_read_offset = entry.byte_offset + entry.byte_count;
            if (m_write_wrap_count == new_read_wrap_count + 1) {
                ERHE_VERIFY(new_read_offset >= m_write_position);
            } else if (new_read_wrap_count == m_write_wrap_count) {
                ERHE_VERIFY(m_write_position >= new_read_offset);
            } else {
                ERHE_FATAL("buffer issue");
            }
            m_read_wrap_count = new_read_wrap_count;
            m_read_offset = new_read_offset;
            m_sync_entries.pop_front();
            sanity_check();

        } else {
            // If entry is not ready, we stop, we expect entries to complete in submission
            // order. Strictly speaking this is not necessarily the case, but it simplifies
            // the logic.
            return;
        }
    }
}

void GPU_ring_buffer::wrap_write()
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    // Insert dummy entry to cover remaining unused buffer space, so that update_entries()
    // can register the space as available - there is no GPU draw nor associated fence
    // guarding that memory.
    const size_t padding_byte_count = m_buffer.capacity_byte_count() - m_write_position;
    if (padding_byte_count > 0) {
        Sync_entry entry{
            .wrap_count  = m_write_wrap_count,
            .byte_offset = m_write_position,
            .byte_count  = m_buffer.capacity_byte_count() - m_write_position,
            .fence_sync  = nullptr,
            .result      = gl::Sync_status::condition_satisfied
        };
        m_sync_entries.push_back(entry);
    }

    sanity_check();

    ++m_write_wrap_count;
    m_write_position = 0;

    sanity_check();

}

void GPU_ring_buffer::push(Sync_entry&& entry)
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    m_sync_entries.push_back(entry);
}

auto GPU_ring_buffer::bind(const Buffer_range& range) -> bool
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    const size_t offset     = range.get_byte_start_offset_in_buffer();
    const size_t byte_count = range.get_written_byte_count();
    if (byte_count == 0) {
        return false;
    }

    const auto& buffer = get_buffer();

    SPDLOG_LOGGER_TRACE(
        log_gpu_ring_buffer,
        "binding {} {} {} buffer offset = {} byte count = {}",
        m_name,
        gl::c_str(buffer.target()),
        m_binding_point.has_value() ? "uses binding point" : "non-indexed binding",
        m_binding_point.has_value() ? m_binding_point.value() : 0,
        range.first_byte_offset,
        range.byte_count
    );

    ERHE_VERIFY(
        (buffer.target() != gl::Buffer_target::uniform_buffer) ||
        (byte_count <= static_cast<std::size_t>(m_instance.limits.max_uniform_block_size))
    );
    ERHE_VERIFY(offset + byte_count <= buffer.capacity_byte_count());

    if (gl_helpers::is_indexed(buffer.target())) {
        gl::bind_buffer_range(
            buffer.target(),
            static_cast<GLuint>    (m_binding_point),
            static_cast<GLuint>    (buffer.gl_name()),
            static_cast<GLintptr>  (offset),
            static_cast<GLsizeiptr>(byte_count)
        );
    } else {
        gl::bind_buffer(buffer.target(), static_cast<GLuint>(buffer.gl_name()));
    }
    return true;
}

} // namespace erhe::renderer
