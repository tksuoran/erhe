#include "erhe_graphics/buffer.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_gl/command_info.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_utility/align.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#if defined(ERHE_PROFILE_LIBRARY_TRACY)
#   include <tracy/TracyC.h>
#endif

#include <fmt/format.h>

#include <sstream>
#include <vector>

namespace erhe::graphics {

template<typename Enum>
struct Enable_bit_mask_operators
{
    static const bool enable = false;
};

template<typename Enum>
constexpr auto
operator |(Enum lhs, Enum rhs)
-> typename std::enable_if<Enable_bit_mask_operators<Enum>::enable, Enum>::type
{
    using underlying = typename std::underlying_type<Enum>::type;
    return static_cast<Enum> (static_cast<underlying>(lhs) | static_cast<underlying>(rhs));
}

template<typename Enum>
constexpr auto
operator &(Enum lhs, Enum rhs)
-> typename std::enable_if<Enable_bit_mask_operators<Enum>::enable, Enum>::type
{
    using underlying = typename std::underlying_type<Enum>::type;
    return static_cast<Enum> (static_cast<underlying>(lhs) & static_cast<underlying>(rhs));
}

template<> struct Enable_bit_mask_operators<Buffer_usage    > { static const bool enable = true; };
template<> struct Enable_bit_mask_operators<Buffer_map_flags> { static const bool enable = true; };

auto get_buffer_usage(Buffer_target target) -> Buffer_usage
{
    switch (target) {
        case Buffer_target::index        : return Buffer_usage::index;
        case Buffer_target::vertex       : return Buffer_usage::vertex;
        case Buffer_target::uniform      : return Buffer_usage::uniform;
        case Buffer_target::storage      : return Buffer_usage::storage;
        case Buffer_target::draw_indirect: return Buffer_usage::indirect;
        case Buffer_target::texture      : return Buffer_usage::texture;
        case Buffer_target::pixel        : return Buffer_usage::pixel;
        case Buffer_target::transfer     : return Buffer_usage::transfer;
        default: ERHE_FATAL("get_buffer_usage(): Bad Buffer_target %u", static_cast<unsigned int>(target)); return Buffer_usage::none;
    }
}

auto to_string(Buffer_usage usage) -> std::string
{
    using namespace erhe::utility;
    std::stringstream ss;
    bool is_empty = true;
    if (test_bit_set(usage, Buffer_usage::vertex)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "vertex";
        is_empty = false;
    }
    if (test_bit_set(usage, Buffer_usage::index)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "index";
        is_empty = false;
    }
    if (test_bit_set(usage, Buffer_usage::uniform)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "uniform";
        is_empty = false;
    }
    if (test_bit_set(usage, Buffer_usage::storage)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "storage";
        is_empty = false;
    }
    if (test_bit_set(usage, Buffer_usage::indirect)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "indirect";
        is_empty = false;
    }
    if (test_bit_set(usage, Buffer_usage::texture)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "texture";
        is_empty = false;
    }
    if (test_bit_set(usage, Buffer_usage::pixel)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "pixel";
        is_empty = false;
    }
    if (test_bit_set(usage, Buffer_usage::transfer)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "transfer";
        is_empty = false;
    }
    return ss.str();
}
auto c_str(Buffer_direction direction) -> const char*
{
    switch (direction) {
        case Buffer_direction::cpu_to_gpu: return "cpu_to_gpu";
        case Buffer_direction::gpu_only  : return "gpu_only";
        case Buffer_direction::gpu_to_cpu: return "gpu_to_cpu";
        default: return "?";
    }
}
auto c_str(Buffer_cache_mode cache_mode) -> const char*
{
    switch (cache_mode) {
        case Buffer_cache_mode::write_combined: return "write_combined";
        case Buffer_cache_mode::default_:       return "defaul";
        default: return "?";
    }
}
auto c_str(Buffer_mapping mapping) -> const char*
{
    switch (mapping) {
        case Buffer_mapping::not_mappable: return "not_mappable";
        case Buffer_mapping::persistent:   return "persistent";
        case Buffer_mapping::transient:    return "transient";
        default: return "?";
    }
}
auto c_str(Buffer_coherency coherency) -> const char*
{
    switch (coherency) {
        case Buffer_coherency::on:  return "on";
        case Buffer_coherency::off: return "off";
        default: return "?";
    }
}
auto to_string(Buffer_map_flags flags) -> std::string
{
    using namespace erhe::utility;
    std::stringstream ss;
    bool is_empty = true;
    if (test_bit_set(flags, Buffer_map_flags::explicit_flush)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "explicit_flush";
        is_empty = false;
    }
    if (test_bit_set(flags, Buffer_map_flags::invalidate_range)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "invalidate_range";
        is_empty = false;
    }
    if (test_bit_set(flags, Buffer_map_flags::invalidate_buffer)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "invalidate_buffer";
        is_empty = false;
    }
    return ss.str();
}

[[nodiscard]] auto Buffer::get_gl_storage_mask() -> gl::Buffer_storage_mask
{
    // dynamic_storage_bit
    //      The contents of the data store may be updated after creation through calls
    //      to BufferSubData. If this bit is not set, the buffer content may not be
    //      directly updated by the client.
    //      The data argument may be used to specify the initial content of the buffer's
    //      data store regardless of the presence of the DYNAMIC_STORAGE_BIT.
    //      Regardless of the presence of this bit, buffers may always be updated with
    //      server-side calls such as CopyBufferSubData and ClearBufferSubData.
    // map_read_bit
    //      The data store may be mapped by the client for read access and a pointer in
    //      the client's address space obtained that may be read from.
    // map_write_bit
    //      The data store may be mapped by the client for write access and a pointer
    //      in the client's address space obtained that may be written to.
    // map_persistent_bit
    //      The client may request that the server read from or write to the buffer
    //      while it is mapped.
    //      The client's pointer to the data store remains valid so long as the data
    //      store is mapped, even during execution of drawing or dispatch commands
    // map_coherent_bit
    //      Shared access to buffers that are simultaneously mapped for client access
    //      and are used by the server will be coherent, so long as that mapping is
    //      performed using MapBufferRange or MapNamedBufferRange.
    //      That is, data written to the store by either the client or server will
    //      be visible to any subsequently issued GL commands with no further action
    //      taken by the application. In particular,
    //          - If MAP_COHERENT_BIT is not set and the client performs a write            client = CPU
    //            followed by a call to one of the FlushMapped*BufferRange commands
    //            with a range including the written range, then in subsequent commands
    //            the server will see the writes.                                           server = GPU
    //
    //          - If MAP_COHERENT_BIT is set and the client performs a write, then in       client = CPU
    //            subsequent commands the server will see the writes.
    //
    //          - If MAP_COHERENT_BIT is not set and the server performs a write, the       server = GPU
    //            application must call MemoryBarrier with the
    //            CLIENT_MAPPED_BUFFER_BARRIER_BIT set and then call FenceSync with
    //            SYNC_GPU_COMMANDS_COMPLETE (or Finish).
    //            Then the CPU will see the writes after the sync is complete.
    //
    //          - If MAP_COHERENT_BIT is set and the server does a write, the               server = GPU
    //            application must call FenceSync with SYNC_GPU_COMMANDS_COMPLETE
    //            (or Finish). Then the CPU will see the writes after the sync is
    //            complete.
    // client_storage_bit
    //      When all other criteria for the buffer storage allocation are met, this bit     client = CPU
    //      may be used by an implementation to determine whether to use storage that is
    //      local to the server or to the client to serve as the backing store for the
    //      buffer.
    gl::Buffer_storage_mask storage{0};
    if (m_mapping != Buffer_mapping::not_mappable) {
        if (m_direction == Buffer_direction::cpu_to_gpu) {
            storage = storage | gl::Buffer_storage_mask::map_write_bit;
        } else if (m_direction == Buffer_direction::gpu_to_cpu) {
            storage = storage | gl::Buffer_storage_mask::map_read_bit;
        }
        if (m_mapping == Buffer_mapping::persistent) {
            storage = storage | gl::Buffer_storage_mask::map_persistent_bit;
        }
        if (m_coherency == Buffer_coherency::on) {
            storage = storage | gl::Buffer_storage_mask::map_coherent_bit;
        }
    }
    return storage;
}

auto Buffer::get_gl_access_mask(Buffer_map_flags flags) const -> gl::Map_buffer_access_mask
{
    //  MAP_READ_BIT
    //      the returned pointer may be used to read buffer object data.
    //      No GL error is generated if the pointer is used to query a mapping which
    //      excludes this flag, but the result is undefined and system errors (possibly
    //      including program termination) may occur.
    //  MAP_WRITE_BIT
    //      the returned pointer may be used to modify buffer object data.
    //      No GL error is generated if the pointer is used to modify a mapping which
    //      excludes this flag, but the result is undefined and system errors (possibly
    //      including program termination) may occur.
    //  MAP_PERSISTENT_BIT
    //      it is not an error for the GL to read data from or write data to the buffer
    //      while it is mapped (see section 6.3.2).
    //      If this bit is set, the value of BUFFER_STORAGE_FLAGS for the buffer being
    //      mapped must include MAP_PERSISTENT_BIT.
    //  MAP_COHERENT_BIT
    //      the mapping should be performed coherently.
    //      That is, such a mapping follows the rules set forth in section 6.2.
    //      If this bit is set, the value of BUFFER_STORAGE_FLAGS for the buffer being
    //      mapped must include MAP_COHERENT_BIT.
    //  MAP_INVALIDATE_RANGE_BIT
    //      the previous contents of the specified range may be discarded.
    //      Data within this range are undefined with the exception of subsequently
    //      written data.
    //      No GL error is generated if subsequent GL operations access unwritten data,
    //      but the result is undefined and system errors (possibly including program
    //      termination) may occur.
    //      This flag may not be used in combination with MAP_READ_BIT.
    //  MAP_INVALIDATE_BUFFER_BIT
    //      The previous contents of the entire buffer may be discarded.
    //      Data within the entire buffer are undefined with the exception of
    //      subsequently written data.
    //      No GL error is generated if subsequent GL operations access unwritten
    //      data, but the result is undefined and system errors (possibly including
    //      program termination) may occur.
    //      This flag may not be used in combination with MAP_READ_BIT.
    //  MAP_FLUSH_EXPLICIT_BIT
    //      one or more discrete subranges of the mapping may be modified.
    //      When this flag is set, modifications to each subrange must be explicitly
    //      flushed by calling FlushMappedBufferRange.
    //      No GL error is set if a subrange of the mapping is modified and
    //      not flushed, but data within the corresponding subrange of the buffer are
    //      undefined.
    //      This flag may only be used in conjunction with MAP_WRITE_BIT.
    //      When this option is selected, flushing is strictly limited to regions that are
    //      explicitly indicated with calls to FlushMappedBufferRange prior to unmap;
    //      if this option is not selected UnmapBuffer will automatically flush the
    //      entire mapped range when called.
    //  MAP_UNSYNCHRONIZED_BIT
    //      the GL should not attempt to synchronize pending operations on the buffer
    //      prior to returning from Map*BufferRange.
    //      No GL error is generated if pending operations which source or modify the
    //      buffer overlap the mapped region, but the result of such previous and any
    //      subsequent operations is undefined.
    ERHE_VERIFY(m_mapping != Buffer_mapping::not_mappable);

    gl::Map_buffer_access_mask access_mask{0};
    if (m_direction == Buffer_direction::cpu_to_gpu) {
        access_mask = access_mask | gl::Map_buffer_access_mask::map_write_bit;
    } else if (m_direction == Buffer_direction::gpu_to_cpu) {
        access_mask = access_mask | gl::Map_buffer_access_mask::map_read_bit;
    }
    if (m_mapping == Buffer_mapping::persistent) {
        access_mask = access_mask | gl::Map_buffer_access_mask::map_persistent_bit;
    }
    if (m_coherency == Buffer_coherency::on) {
        access_mask = access_mask | gl::Map_buffer_access_mask::map_coherent_bit;
    }
    using namespace erhe::utility;
    if (test_bit_set(flags, Buffer_map_flags::explicit_flush)) {
        access_mask = access_mask | gl::Map_buffer_access_mask::map_flush_explicit_bit;
    }
    if (test_bit_set(flags, Buffer_map_flags::invalidate_range)) {
        access_mask = access_mask | gl::Map_buffer_access_mask::map_invalidate_range_bit;
    }
    if (test_bit_set(flags, Buffer_map_flags::invalidate_range)) {
        access_mask = access_mask | gl::Map_buffer_access_mask::map_invalidate_buffer_bit;
    }
    return access_mask;
}

auto Buffer::gl_name() const noexcept -> unsigned int
{
    return m_handle.gl_name();
}

void Buffer::capability_check(const gl::Buffer_storage_mask storage_mask)
{
    if (
        erhe::utility::test_any_rhs_bits_set(
            storage_mask,
            gl::Buffer_storage_mask::client_storage_bit  |
            gl::Buffer_storage_mask::dynamic_storage_bit |
            gl::Buffer_storage_mask::map_coherent_bit    |
            gl::Buffer_storage_mask::map_persistent_bit
        )
    ) {
        const bool in_core       = m_device.info.gl_version >= 440;
        const bool has_extension = gl::is_extension_supported(gl::Extension::Extension_GL_ARB_buffer_storage);
        ERHE_VERIFY(in_core || has_extension);
    }
}

void Buffer::capability_check(const gl::Map_buffer_access_mask access_mask)
{
    if (erhe::utility::test_any_rhs_bits_set(access_mask,gl::Map_buffer_access_mask::map_coherent_bit | gl::Map_buffer_access_mask::map_persistent_bit)) {
        const bool in_core       = m_device.info.gl_version >= 440;
        const bool has_extension = gl::is_extension_supported(gl::Extension::Extension_GL_ARB_buffer_storage);
        ERHE_VERIFY(in_core || has_extension);
    }
}

void Buffer::allocate_storage(const void* init_data)
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(m_capacity_byte_count > 0);

    gl::Buffer_storage_mask gl_storage_mask = get_gl_storage_mask();
    capability_check(gl_storage_mask);

    gl::named_buffer_storage(gl_name(), static_cast<GLintptr>(m_capacity_byte_count), init_data, gl_storage_mask);
    {
#if TRACY_ENABLE
        TracyCZoneCtx zone{};
        if (!m_debug_label.empty()) {
            const uint32_t color = 0x804020ffu;
            uint64_t srcloc = ___tracy_alloc_srcloc_name(1, "", 0, "", 0, m_debug_label.c_str(), m_debug_label.length(), color);
            zone = ___tracy_emit_zone_begin_alloc(srcloc, 1);
        }
#endif
        ERHE_PROFILE_MEM_ALLOC_NS(this, m_capacity_byte_count, s_pool_name);
#if TRACY_ENABLE
        if (!m_debug_label.empty()) {
            ___tracy_emit_zone_end(zone);
        }
#endif
    }

    m_allocated = true;
    if (erhe::utility::test_bit_set(gl_storage_mask, gl::Buffer_storage_mask::map_persistent_bit)) {
        map_bytes(0, m_capacity_byte_count, Buffer_map_flags::none);
    }

    ERHE_VERIFY(gl_name() != 0);
    ERHE_VERIFY(m_capacity_byte_count > 0);
}

Buffer::Buffer(Device& device, const Buffer_create_info& create_info) noexcept
    : m_device             {device}
    , m_handle             {device}
    , m_capacity_byte_count{create_info.capacity_byte_count}
    , m_usage              {create_info.usage     }
    , m_direction          {create_info.direction }
    , m_cache_mode         {create_info.cache_mode}
    , m_mapping            {create_info.mapping   }
    , m_coherency          {create_info.coherency }
    , m_debug_label        {create_info.debug_label}
{
    constexpr const std::size_t sanity_threshold{2'000'000'000};
    ERHE_VERIFY(m_capacity_byte_count < sanity_threshold); // sanity check, can raise limit when needed
    log_buffer->debug(
        "Buffer::Buffer() capacity_byte_count = {}, usage = {}, direction = {}, cache_mode = {}, mapping = {}, coherency = {}) name = {} debug_label = {}",
        m_capacity_byte_count,
        to_string(create_info.usage),
        c_str(create_info.direction),
        c_str(create_info.cache_mode),
        c_str(create_info.mapping),
        c_str(create_info.coherency),
        gl_name(),
        m_debug_label
    );
    if (!m_debug_label.empty()) {
        gl::object_label(gl::Object_identifier::buffer, gl_name(), -1, m_debug_label.c_str());
    }
    allocate_storage(create_info.init_data);
}

Buffer::Buffer(Device& device)
    : m_device{device}
    , m_handle{device}
{
}

Buffer::~Buffer() noexcept
{
    if (m_allocated) {
        ERHE_PROFILE_MEM_FREE_NS(this, s_pool_name);
    }
}

Buffer::Buffer(Buffer&& other) noexcept
    : m_device             {other.m_device}
    , m_handle             {std::move(other.m_handle)}
    , m_capacity_byte_count{other.m_capacity_byte_count}
    , m_next_free_byte     {other.m_next_free_byte}
    , m_usage              {other.m_usage     }
    , m_direction          {other.m_direction }
    , m_cache_mode         {other.m_cache_mode}
    , m_mapping            {other.m_mapping   }
    , m_coherency          {other.m_coherency }
    , m_debug_label        {other.m_debug_label}
    , m_map                {other.m_map}
    , m_map_byte_offset    {other.m_map_byte_offset}
    , m_map_flags          {other.m_map_flags}
    , m_allocated          {std::exchange(other.m_allocated, false)}
{
}

auto Buffer::operator=(Buffer&& other) noexcept -> Buffer&
{
    ERHE_VERIFY(&m_device == &other.m_device);
    m_handle              = std::move(other.m_handle);
    m_debug_label         = other.m_debug_label;
    m_capacity_byte_count = other.m_capacity_byte_count;
    m_next_free_byte      = other.m_next_free_byte;
    m_usage               = other.m_usage     ;
    m_direction           = other.m_direction ;
    m_cache_mode          = other.m_cache_mode;
    m_mapping             = other.m_mapping   ;
    m_coherency           = other.m_coherency ;
    m_map                 = other.m_map;
    m_map_byte_offset     = other.m_map_byte_offset;
    m_map_flags           = other.m_map_flags;
    m_allocated           = std::exchange(other.m_allocated, false);
    return *this;
}

auto Buffer::get_map() const -> std::span<std::byte>
{
    return m_map;
}

auto Buffer::get_debug_label() const noexcept -> const std::string&
{
    return m_debug_label;
}

void Buffer::clear() noexcept
{
    m_next_free_byte = 0;
}

auto Buffer::allocate_bytes(const std::size_t byte_count, const std::size_t alignment) noexcept -> std::optional<std::size_t>
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_allocate_mutex};
    const std::size_t offset         = erhe::utility::align_offset_non_power_of_two(m_next_free_byte, alignment);
    const std::size_t next_free_byte = offset + byte_count;
    if (next_free_byte > m_capacity_byte_count) {
        const std::size_t available_byte_count = (offset < m_capacity_byte_count) ? m_capacity_byte_count - offset : 0;
        log_buffer->error(
            "erhe::graphics::Buffer::allocate_bytes(): Out of memory requesting {} bytes, currently allocated {}, total size {}, free {} bytes",
            byte_count,
            m_next_free_byte,
            m_capacity_byte_count,
            available_byte_count
        );
        return {};
    }

    m_next_free_byte = next_free_byte;
    ERHE_VERIFY(m_next_free_byte <= m_capacity_byte_count);
    log_buffer->trace("buffer {}: allocated {} bytes at offset {}", gl_name(), byte_count, offset);
    return offset;
}

auto Buffer::begin_write(const std::size_t byte_offset, std::size_t byte_count) noexcept -> std::span<std::byte>
{
    ERHE_VERIFY(gl_name() != 0);

    if (!m_device.info.use_persistent_buffers || (m_mapping != Buffer_mapping::persistent)) {
        ERHE_VERIFY(m_map.empty());
        if (byte_count == 0) {
            byte_count = m_capacity_byte_count - byte_offset;
        } else {
            byte_count = std::min(byte_count, m_capacity_byte_count - byte_offset);
        }
        m_map_byte_offset = byte_offset;
        m_map_flags = Buffer_map_flags::explicit_flush;
        const gl::Map_buffer_access_mask gl_access_mask = get_gl_access_mask(m_map_flags);

        auto* const map_pointer = static_cast<std::byte*>(
            gl::map_named_buffer_range(gl_name(), byte_offset, byte_count, gl_access_mask)
        );
        ERHE_VERIFY(map_pointer != nullptr);

        log_buffer->trace(
            ":m_map_byte_offset = {}, m_map_byte_count = {}, m_map_pointer = {} {}",
            m_map_byte_offset,
            byte_count,
            fmt::ptr(map_pointer),
            m_debug_label
        );

        m_map = std::span<std::byte>(map_pointer, byte_count);

        ERHE_VERIFY(!m_map.empty());
    }

    return m_map;
}

void Buffer::end_write(const std::size_t byte_offset, const std::size_t byte_count) noexcept
{
    ERHE_VERIFY(!m_map.empty());
    ERHE_VERIFY(gl_name() != 0);

    if (!m_device.info.use_persistent_buffers || (m_mapping != Buffer_mapping::persistent)) {
        if (byte_count > 0) {
            flush_bytes(byte_offset, byte_count);
        }
        unmap();
    }
}

auto Buffer::map_all_bytes(Buffer_map_flags flags) noexcept -> std::span<std::byte>
{
    ERHE_VERIFY(m_map.empty());
    ERHE_VERIFY(gl_name() != 0);

    log_buffer->trace("Buffer::map_all_bytes(), name = {}, flags = {}", gl_name(), to_string(flags));
    //const log::Indenter indenter;

    const std::size_t byte_count = m_capacity_byte_count;

    m_map_byte_offset = 0;

    m_map_flags = flags;
    const gl::Map_buffer_access_mask gl_access_mask = get_gl_access_mask(m_map_flags);

    auto* const map_pointer = static_cast<std::byte*>(
        gl::map_named_buffer_range(
            gl_name(),
            m_map_byte_offset,
            static_cast<GLsizeiptr>(byte_count),
            gl_access_mask
        )
    );
    ERHE_VERIFY(map_pointer != nullptr);

    log_buffer->trace(
        ":m_map_byte_offset = {}, m_map_byte_count = {}, m_map_pointer = {}",
        m_map_byte_offset,
        byte_count,
        fmt::ptr(map_pointer)
    );

    m_map = std::span<std::byte>(map_pointer, byte_count);

    ERHE_VERIFY(!m_map.empty());

    return m_map;
}

auto Buffer::map_bytes(const std::size_t byte_offset, const std::size_t byte_count, Buffer_map_flags flags) noexcept -> std::span<std::byte>
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(byte_count > 0);
    ERHE_VERIFY(m_map.empty());
    ERHE_VERIFY(gl_name() != 0);

    log_buffer->trace(
        "Buffer::map_bytes(byte_offset = {}, byte_count = {}), name = {}, flags = {}",
        byte_offset,
        byte_count,
        gl_name(),
        to_string(flags)
    );
    //const log::Indenter indenter;

    ERHE_VERIFY(byte_offset + byte_count <= m_capacity_byte_count);

    m_map_byte_offset = static_cast<GLsizeiptr>(byte_offset);

    m_map_flags = flags;
    const gl::Map_buffer_access_mask gl_access_mask = get_gl_access_mask(m_map_flags);

    auto* const map_pointer = static_cast<std::byte*>(
        gl::map_named_buffer_range(
            gl_name(),
            m_map_byte_offset,
            static_cast<GLsizeiptr>(byte_count),
            gl_access_mask
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

    m_map = std::span<std::byte>(map_pointer, byte_count);

    ERHE_VERIFY(!m_map.empty());

    return m_map;
}

void Buffer::unmap() noexcept
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(!m_map.empty());
    ERHE_VERIFY(gl_name() != 0);

    log_buffer->trace(
        "Buffer::unmap() byte_offset = {}, byte_count = {}, pointer = {}, name = {} {}",
        m_map_byte_offset,
        m_map.size(),
        reinterpret_cast<intptr_t>(m_map.data()),
        gl_name(),
        m_debug_label
    );
    //const log::Indenter indented;
    //Log::set_text_color(erhe::log::Console_color::GREY);

    const auto res = gl::unmap_named_buffer(gl_name());
    ERHE_VERIFY(res == GL_TRUE);

    m_map_byte_offset = std::numeric_limits<std::size_t>::max();

    m_map = std::span<std::byte>();

    ERHE_VERIFY(m_map.empty());
}

void Buffer::flush_bytes(const std::size_t byte_offset, const std::size_t byte_count) noexcept
{
    ERHE_VERIFY(erhe::utility::test_bit_set(m_map_flags, Buffer_map_flags::explicit_flush));
    ERHE_VERIFY(gl_name() != 0);

    // unmap will do flush
    ERHE_VERIFY(byte_offset + byte_count <= m_capacity_byte_count);

    if (byte_count == 0) {
        return;
    }

    log_buffer->trace(
       "Buffer::flush(byte_offset = {}, byte_count = {}) m_mapped_ptr = {} name = {} {}",
        byte_offset,
        byte_count,
        reinterpret_cast<intptr_t>(m_map.data()),
        gl_name(),
        m_debug_label
    );

    gl::flush_mapped_named_buffer_range(
        gl_name(),
        static_cast<GLintptr>(byte_offset - m_map_byte_offset),
        static_cast<GLsizeiptr>(byte_count)
    );
}

void Buffer::dump() const noexcept
{
    ERHE_VERIFY(gl_name() != 0);

    const std::size_t byte_count{m_capacity_byte_count};
    const std::size_t word_count{byte_count / sizeof(uint32_t)};

    int mapped{GL_FALSE};
    gl::get_named_buffer_parameter_iv(gl_name(), gl::Buffer_p_name::buffer_mapped, &mapped);

    uint32_t* data {nullptr};
    bool      unmap{false};
    if (mapped == GL_FALSE) {
        data = static_cast<uint32_t*>(
            gl::map_named_buffer_range(gl_name(), 0, byte_count, gl::Map_buffer_access_mask::map_read_bit)
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
    ERHE_VERIFY(gl_name() != 0);

    const bool flush_explicit = erhe::utility::test_bit_set(m_map_flags, Buffer_map_flags::explicit_flush);

    log_buffer->trace("flush_and_unmap(byte_count = {}) name = {}", byte_count, gl_name());

    // If explicit is not selected, unmap will do full flush
    // so we do manual flush only if explicit is selected
    if (flush_explicit) {
        flush_bytes(0, byte_count);
    }

    unmap();
}

auto Buffer::get_used_byte_count() const -> std::size_t
{
    return m_next_free_byte;
}

auto Buffer::get_available_byte_count(std::size_t alignment) const noexcept -> std::size_t
{
    std::size_t aligned_offset = erhe::utility::align_offset_non_power_of_two(m_next_free_byte, alignment);
    if (aligned_offset >= m_capacity_byte_count) {
        return 0;
    }
    return m_capacity_byte_count - aligned_offset;
}

auto Buffer::get_capacity_byte_count() const noexcept -> std::size_t
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

auto to_gl_index_type(erhe::dataformat::Format format) -> gl::Draw_elements_type
{
    switch (format) {
        case erhe::dataformat::Format::format_8_scalar_uint:  return gl::Draw_elements_type::unsigned_byte;
        case erhe::dataformat::Format::format_16_scalar_uint: return gl::Draw_elements_type::unsigned_short;
        case erhe::dataformat::Format::format_32_scalar_uint: return gl::Draw_elements_type::unsigned_int;
        default: {
            ERHE_FATAL("Bad index format");
        }
    }
}

} // namespace erhe::graphics
