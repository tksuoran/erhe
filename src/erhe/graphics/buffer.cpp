#include "erhe/graphics/buffer.hpp"
#include "erhe/gl/command_info.hpp"
#include "erhe/gl/enum_string_functions.hpp"
#include "erhe/gl/enum_bit_mask_operators.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/graphics_log.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/verify.hpp"

#include <fmt/format.h>

#include <sstream>
#include <vector>

namespace erhe::graphics
{

auto Buffer::gl_name() const noexcept -> unsigned int
{
    return m_handle.gl_name();
}

void Buffer::capability_check(const gl::Buffer_storage_mask storage_mask)
{
    if (
        erhe::toolkit::test_any_rhs_bits_set(
            storage_mask,
            gl::Buffer_storage_mask::client_storage_bit  |
            gl::Buffer_storage_mask::dynamic_storage_bit |
            gl::Buffer_storage_mask::map_coherent_bit    |
            gl::Buffer_storage_mask::map_persistent_bit
        )
    ) {
        const bool in_core       = Instance::info.gl_version >= 440;
        const bool has_extension = gl::is_extension_supported(gl::Extension::Extension_GL_ARB_buffer_storage);
        ERHE_VERIFY(in_core || has_extension);
    }
}

void Buffer::capability_check(const gl::Map_buffer_access_mask access_mask)
{
    if (
        erhe::toolkit::test_any_rhs_bits_set(
            access_mask,
            gl::Map_buffer_access_mask::map_coherent_bit  |
            gl::Map_buffer_access_mask::map_persistent_bit
        )
    ) {
        const bool in_core       = Instance::info.gl_version >= 440;
        const bool has_extension = gl::is_extension_supported(gl::Extension::Extension_GL_ARB_buffer_storage);
        ERHE_VERIFY(in_core || has_extension);
    }
}

void Buffer::allocate_storage()
{
    ERHE_VERIFY(m_capacity_byte_count > 0);

    capability_check(m_storage_mask);
    capability_check(m_access_mask);

    gl::named_buffer_storage(
        gl_name(),
        static_cast<GLintptr>(m_capacity_byte_count),
        nullptr,
        m_storage_mask
    );

    if (
        erhe::toolkit::test_all_rhs_bits_set(
            m_storage_mask,
            gl::Buffer_storage_mask::map_persistent_bit
        )
    ) {
        map_bytes(0, m_capacity_byte_count, m_access_mask);
    }

    //// if (!Instance::info.use_persistent_buffers) {
    ////     m_cpu_copy.resize(m_capacity_byte_count);
    //// }

    Ensures(gl_name() != 0);
    Ensures(m_capacity_byte_count > 0);
}

Buffer::Buffer(
    const gl::Buffer_target       target,
    const std::size_t             capacity_byte_count,
    const gl::Buffer_storage_mask storage_mask
) noexcept
    : m_target             {target}
    , m_capacity_byte_count{capacity_byte_count}
    , m_storage_mask       {storage_mask}
{
    log_buffer->trace(
        "Buffer::Buffer(target = {}, capacity_byte_count = {}, storage_mask = {}) name = {}",
        gl::c_str(target),
        capacity_byte_count,
        gl::to_string(storage_mask),
        gl_name()
    );

    allocate_storage();
}

Buffer::Buffer(
    const std::size_t             capacity_byte_count,
    const gl::Buffer_storage_mask storage_mask
) noexcept
    : m_target             {0}
    , m_capacity_byte_count{capacity_byte_count}
    , m_storage_mask       {storage_mask}
{
    log_buffer->trace(
        "Buffer::Buffer(capacity_byte_count = {}, storage_mask = {}) name = {}",
        capacity_byte_count,
        gl::to_string(storage_mask),
        gl_name()
    );

    allocate_storage();
}

Buffer::Buffer(
    const gl::Buffer_target          target,
    const std::size_t                capacity_byte_count,
    const gl::Buffer_storage_mask    storage_mask,
    const gl::Map_buffer_access_mask access_mask
) noexcept
    : m_target             {target}
    , m_capacity_byte_count{capacity_byte_count}
    , m_storage_mask       {storage_mask}
    , m_access_mask        {access_mask}
{
    log_buffer->trace(
        "Buffer::Buffer(target = {}, capacity_byte_count = {}, storage_mask = {}, access_mask = {}) name = {}",
        gl::c_str(target),
        capacity_byte_count,
        gl::to_string(storage_mask),
        gl::to_string(access_mask),
        gl_name()
    );

    allocate_storage();
}

Buffer::Buffer(
    const gl::Buffer_target          target,
    const std::size_t                capacity_byte_count,
    const gl::Buffer_storage_mask    storage_mask,
    const gl::Map_buffer_access_mask access_mask,
    const std::string_view           debug_label
) noexcept
    : m_target             {target}
    , m_capacity_byte_count{capacity_byte_count}
    , m_storage_mask       {storage_mask}
    , m_access_mask        {access_mask}
{
    log_buffer->trace(
        "Buffer::Buffer(target = {}, capacity_byte_count = {}, storage_mask = {}, map_buffer_access_mask = {}) name = {} {}",
        gl::c_str(m_target),
        m_capacity_byte_count,
        gl::to_string(m_storage_mask),
        gl::to_string(m_access_mask),
        gl_name(),
        debug_label
    );

    set_debug_label(debug_label);
    allocate_storage();
}

Buffer::Buffer()
{
}

Buffer::~Buffer() noexcept
{
}

Buffer::Buffer(Buffer&& other) noexcept
{
    m_handle                 = std::move(other.m_handle);
    m_debug_label            = std::move(other.m_debug_label);
    m_target                 = other.m_target;
    m_capacity_byte_count    = other.m_capacity_byte_count;
    m_next_free_byte         = other.m_next_free_byte;
    m_storage_mask           = other.m_storage_mask;
    m_access_mask            = other.m_access_mask;
    m_map                    = other.m_map;
    m_map_byte_offset        = other.m_map_byte_offset;
    m_map_buffer_access_mask = other.m_map_buffer_access_mask;
    //// m_cpu_copy               = std::move(other.m_cpu_copy);
}

auto Buffer::operator=(Buffer&& other) noexcept -> Buffer&
{
    m_handle                 = std::move(other.m_handle);
    m_debug_label            = std::move(other.m_debug_label);
    m_target                 = other.m_target;
    m_capacity_byte_count    = other.m_capacity_byte_count;
    m_next_free_byte         = other.m_next_free_byte;
    m_storage_mask           = other.m_storage_mask;
    m_access_mask            = other.m_access_mask;
    m_map                    = other.m_map;
    m_map_byte_offset        = other.m_map_byte_offset;
    m_map_buffer_access_mask = other.m_map_buffer_access_mask;
    //// m_cpu_copy               = std::move(other.m_cpu_copy);
    return *this;
}

auto Buffer::map() const -> gsl::span<std::byte>
{
    return m_map;
}

auto Buffer::target() const noexcept -> gl::Buffer_target
{
    return m_target;
}

void Buffer::set_debug_label(const std::string_view label) noexcept
{
    m_debug_label = fmt::format("(B:{}) {}", gl_name(), label);
    gl::object_label(
        gl::Object_identifier::buffer,
        gl_name(),
        static_cast<GLsizei>(m_debug_label.length()),
        m_debug_label.c_str()
    );
}

auto Buffer::debug_label() const noexcept -> const std::string&
{
    return m_debug_label;
}

auto Buffer::allocate_bytes(
    const std::size_t byte_count,
    const std::size_t alignment
) noexcept -> std::size_t
{
    ERHE_VERIFY(alignment > 0);

    const std::lock_guard<std::mutex> lock{m_allocate_mutex};

    while ((m_next_free_byte % alignment) != 0) {
        ++m_next_free_byte;
    }
    const auto offset = m_next_free_byte;
    m_next_free_byte += byte_count;
    ERHE_VERIFY(m_next_free_byte <= m_capacity_byte_count);

    log_buffer->trace("buffer {}: allocated {} bytes at offset {}", gl_name(), byte_count, offset);
    return offset;
}

auto Buffer::begin_write(const std::size_t byte_offset, std::size_t byte_count) noexcept -> gsl::span<std::byte>
{
    Expects(gl_name() != 0);

    if (!Instance::info.use_persistent_buffers) {
        Expects(m_map.empty());
        //// ERHE_VERIFY(m_capacity_byte_count == m_cpu_copy.size());
        //auto* const map_pointer = reinterpret_cast<std::byte*>(m_cpu_copy.data());
        //m_map = gsl::span<std::byte>(map_pointer, m_capacity_byte_count);
        if (byte_count == 0) {
            byte_count = m_capacity_byte_count - byte_offset;
        } else {
            byte_count = std::min(byte_count, m_capacity_byte_count - byte_offset);
        }
        m_map_byte_offset = byte_offset;
        m_map_buffer_access_mask =
            gl::Map_buffer_access_mask::map_flush_explicit_bit   |
            //gl::Map_buffer_access_mask::map_invalidate_range_bit |
            gl::Map_buffer_access_mask::map_write_bit;

        auto* const map_pointer = reinterpret_cast<std::byte*>(
            gl::map_named_buffer_range(
                gl_name(),
                byte_offset,
                byte_count,
                m_map_buffer_access_mask
            )
        );
        ERHE_VERIFY(map_pointer != nullptr);

        log_buffer->trace(
            ":m_map_byte_offset = {}, m_map_byte_count = {}, m_map_pointer = {} {}",
            m_map_byte_offset,
            byte_count,
            fmt::ptr(map_pointer),
            debug_label()
        );

        m_map = gsl::span<std::byte>(map_pointer, byte_count);

        Ensures(!m_map.empty());
    }

    return m_map;
}

void Buffer::end_write(
    const std::size_t byte_offset,
    const std::size_t byte_count
) noexcept
{
    Expects(!m_map.empty());
    Expects(gl_name() != 0);

    if (!Instance::info.use_persistent_buffers) {
        if (byte_count > 0) {
            flush_bytes(byte_offset, byte_count);
        }
        unmap();
    }
}

auto Buffer::map_all_bytes(
    const gl::Map_buffer_access_mask access_mask
) noexcept -> gsl::span<std::byte>
{
    Expects(m_map.empty());
    Expects(gl_name() != 0);

    log_buffer->trace(
        "Buffer::map_all_bytes(access_mask = {}) target = {}, name = {}",
        gl::to_string(access_mask),
        gl::c_str(m_target),
        gl_name()
    );
    //const log::Indenter indenter;

    const std::size_t byte_count = m_capacity_byte_count;

    m_map_byte_offset = 0;
    m_map_buffer_access_mask = access_mask;

    auto* const map_pointer = reinterpret_cast<std::byte*>(
        gl::map_named_buffer_range(
            gl_name(),
            m_map_byte_offset,
            static_cast<GLsizeiptr>(byte_count),
            m_map_buffer_access_mask
        )
    );
    ERHE_VERIFY(map_pointer != nullptr);

    log_buffer->trace(
        ":m_map_byte_offset = {}, m_map_byte_count = {}, m_map_pointer = {}",
        m_map_byte_offset,
        byte_count,
        fmt::ptr(map_pointer)
    );

    m_map = gsl::span<std::byte>(map_pointer, byte_count);

    Ensures(!m_map.empty());

    return m_map;
}

auto Buffer::map_bytes(
    const std::size_t                byte_offset,
    const std::size_t                byte_count,
    const gl::Map_buffer_access_mask access_mask
) noexcept -> gsl::span<std::byte>
{
    ERHE_VERIFY(byte_count > 0);
    Expects(m_map.empty());
    Expects(gl_name() != 0);

    log_buffer->trace(
        "Buffer::map_bytes(byte_offset = {}, byte_count = {}, access_mask = {}) target = {}, name = {}",
        byte_offset,
        byte_count,
        gl::to_string(access_mask),
        gl::c_str(m_target),
        gl_name()
    );
    //const log::Indenter indenter;

    ERHE_VERIFY(byte_offset + byte_count <= m_capacity_byte_count);

    m_map_byte_offset = static_cast<GLsizeiptr>(byte_offset);
    m_map_buffer_access_mask = access_mask;

    auto* const map_pointer = reinterpret_cast<std::byte*>(
        gl::map_named_buffer_range(
            gl_name(),
            m_map_byte_offset,
            static_cast<GLsizeiptr>(byte_count),
            m_map_buffer_access_mask
        )
    );
    if (map_pointer == nullptr) {
        log_buffer->warn("glMapNamedBufferRange() returned nullptr");
        GLint is_buffer = gl::is_buffer(gl_name());
        log_buffer->trace("is_buffer = {}", is_buffer);
        GLint int_access           {0};
        GLint int_access_flags     {0};
        GLint int_immutable_storage{0};
        GLint int_map_length       {0};
        GLint int_map_offset       {0};
        GLint int_mapped           {0};
        GLint int_size             {0};
        GLint int_storage_flags    {0};
        GLint int_usage            {0};
        gl::get_named_buffer_parameter_iv(gl_name(), gl::Buffer_p_name::buffer_access           , &int_access           );
        gl::get_named_buffer_parameter_iv(gl_name(), gl::Buffer_p_name::buffer_access_flags     , &int_access_flags     );
        gl::get_named_buffer_parameter_iv(gl_name(), gl::Buffer_p_name::buffer_immutable_storage, &int_immutable_storage);
        gl::get_named_buffer_parameter_iv(gl_name(), gl::Buffer_p_name::buffer_map_length       , &int_map_length       );
        gl::get_named_buffer_parameter_iv(gl_name(), gl::Buffer_p_name::buffer_map_offset       , &int_map_offset       );
        gl::get_named_buffer_parameter_iv(gl_name(), gl::Buffer_p_name::buffer_mapped           , &int_mapped           );
        gl::get_named_buffer_parameter_iv(gl_name(), gl::Buffer_p_name::buffer_size             , &int_size             );
        gl::get_named_buffer_parameter_iv(gl_name(), gl::Buffer_p_name::buffer_storage_flags    , &int_storage_flags    );
        gl::get_named_buffer_parameter_iv(gl_name(), gl::Buffer_p_name::buffer_usage            , &int_usage            );
        const auto access        = static_cast<gl::Buffer_access         >(int_access       );
        const auto access_flags  = static_cast<gl::Map_buffer_access_mask>(int_access_flags );
        const auto storage_flags = static_cast<gl::Buffer_storage_mask   >(int_storage_flags);
        const auto usage         = static_cast<gl::Buffer_usage          >(int_usage        );
        log_buffer->trace("access            = {}",          (int_access == 0) ? "0" : gl::c_str(access));
        log_buffer->trace("access flags      = {} ({:04x})", gl::to_string(access_flags), int_access_flags);
        log_buffer->trace("immutable storage = {}",          int_immutable_storage);
        log_buffer->trace("map length        = {}",          int_map_length);
        log_buffer->trace("map offset        = {}",          int_map_offset);
        log_buffer->trace("mapped            = {}",          int_mapped);
        log_buffer->trace("size              = {}",          int_size);
        log_buffer->trace("storage flags     = {} ({:04x})", gl::to_string(storage_flags), int_storage_flags);
        log_buffer->trace("usage             = {}",          (int_usage == 0) ? "0" : gl::c_str(usage), int_usage);
        void* get_map_pointer{nullptr};
        gl::get_named_buffer_pointer_v(gl_name(), gl::Buffer_pointer_name::buffer_map_pointer, &get_map_pointer);
        log_buffer->trace("map pointer       = {}", fmt::ptr(map_pointer));
    }
    ERHE_VERIFY(map_pointer != nullptr);

    log_buffer->trace(
        ":m_map_byte_offset = {}, m_map_byte_count = {}, m_map_pointer = {}",
        m_map_byte_offset,
        byte_count,
        fmt::ptr(map_pointer)
    );

    m_map = gsl::span<std::byte>(map_pointer, byte_count);

    Ensures(!m_map.empty());

    return m_map;
}

void Buffer::unmap() noexcept
{
    Expects(!m_map.empty());
    Expects(gl_name() != 0);

    log_buffer->trace(
        "Buffer::unmap() target = {}, byte_offset = {}, byte_count = {}, pointer = {}, name = {} {}",
        gl::c_str(m_target),
        m_map_byte_offset,
        m_map.size(),
        reinterpret_cast<intptr_t>(m_map.data()),
        gl_name(),
        debug_label()
    );
    //const log::Indenter indented;
    //Log::set_text_color(erhe::log::Console_color::GREY);

    const auto res = gl::unmap_named_buffer(gl_name());
    ERHE_VERIFY(res == GL_TRUE);

    m_map_byte_offset = std::numeric_limits<std::size_t>::max();

    m_map = gsl::span<std::byte>();

    Ensures(m_map.empty());
}

void Buffer::flush_bytes(
    const std::size_t byte_offset,
    const std::size_t byte_count
) noexcept
{
    Expects((m_map_buffer_access_mask & gl::Map_buffer_access_mask::map_flush_explicit_bit) == gl::Map_buffer_access_mask::map_flush_explicit_bit);
    Expects(gl_name() != 0);

    // unmap will do flush
    ERHE_VERIFY(byte_offset + byte_count <= m_capacity_byte_count);

    if (byte_count == 0) {
        return;
    }

    log_buffer->trace(
       "Buffer::flush(byte_offset = {}, byte_count = {}) target = {}, m_mapped_ptr = {} name = {} {}",
        byte_offset,
        byte_count,
        gl::c_str(m_target),
        reinterpret_cast<intptr_t>(m_map.data()),
        gl_name(),
        debug_label()
    );

    gl::flush_mapped_named_buffer_range(
        gl_name(),
        static_cast<GLintptr>(byte_offset - m_map_byte_offset),
        static_cast<GLsizeiptr>(byte_count)
    );
}

void Buffer::dump() const noexcept
{
    Expects(gl_name() != 0);

    const std::size_t byte_count{m_capacity_byte_count};
    const std::size_t word_count{byte_count / sizeof(uint32_t)};

    int mapped{GL_FALSE};
    gl::get_named_buffer_parameter_iv(
        gl_name(),
        gl::Buffer_p_name::buffer_mapped,
        &mapped
    );

    uint32_t* data {nullptr};
    bool      unmap{false};
    if (mapped == GL_FALSE) {
        data = reinterpret_cast<uint32_t*>(
            gl::map_named_buffer_range(
                gl_name(),
                0,
                byte_count,
                gl::Map_buffer_access_mask::map_read_bit
            )
        );
        unmap = (data != nullptr);
    }

    std::vector<uint32_t> storage;
    if (data == nullptr) {
        // This happens if we already had buffer mapped
        storage.resize(word_count + 1);
        data = storage.data();
        gl::get_named_buffer_sub_data(gl_name(), 0, word_count * sizeof(uint32_t), data);
    }

    std::stringstream ss;
    for (std::size_t i = 0; i < word_count; ++i) {
        if (i % 16u == 0) {
            ss << fmt::format("%08x: ", static_cast<unsigned int>(i));
        }

        ss << fmt::format("%08x ", data[i]);

        if (i % 16u == 15u) {
            ss << "\n";
        }
    }
    log_buffer->trace("\n{}", ss.str());

    if (unmap) {
        gl::unmap_named_buffer(gl_name());
    }
}

void Buffer::flush_and_unmap_bytes(const std::size_t byte_count) noexcept
{
    Expects(gl_name() != 0);

    const bool flush_explicit =
        (m_map_buffer_access_mask & gl::Map_buffer_access_mask::map_flush_explicit_bit) == gl::Map_buffer_access_mask::map_flush_explicit_bit;

    log_buffer->trace("flush_and_unmap(byte_count = {}) name = {}", byte_count, gl_name());

    // If explicit is not selected, unmap will do full flush
    // so we do manual flush only if explicit is selected
    if (flush_explicit) {
        flush_bytes(0, byte_count);
    }

    unmap();
}

auto Buffer::free_capacity_bytes() const noexcept -> std::size_t
{
    return m_capacity_byte_count - m_next_free_byte;
}

auto Buffer::capacity_byte_count() const noexcept -> std::size_t
{
    return m_capacity_byte_count;
}

auto operator==(const Buffer& lhs, const Buffer& rhs) noexcept -> bool
{
    return lhs.gl_name() == rhs.gl_name();
}

auto operator!=(const Buffer& lhs, const Buffer& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
