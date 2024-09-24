#pragma once

#include "erhe_gl/wrapper_enums.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace erhe::graphics {
    class Buffer;
    class Instance;
}

namespace erhe::renderer {

class Buffer_range
{
public:
    std::size_t first_byte_offset{0}; // relative to buffer start
    std::size_t byte_count       {0};
};

class Buffer_writer_flag_bits
{
public:
    static constexpr std::size_t allow_wrap  = 0;
    static constexpr std::size_t allow_clamp = 1;
    using value_type = uint64_t;
};
using Buffer_writer_flags = Buffer_writer_flag_bits::value_type;

class Buffer_writer_flag_masks {
public:
    static constexpr Buffer_writer_flags none        = 0u;
    static constexpr Buffer_writer_flags allow_wrap  = 1u << Buffer_writer_flag_bits::allow_wrap;
    static constexpr Buffer_writer_flags allow_clamp = 1u << Buffer_writer_flag_bits::allow_clamp;
};

class Buffer_writer
{
public:
    Buffer_writer(erhe::graphics::Instance& graphic_instance, erhe::graphics::Buffer& buffer);

    Buffer_range range;
    std::size_t  map_offset  {0};
    std::size_t  write_offset{0};
    std::size_t  write_end   {0};
    std::size_t  wrap_count  {0};

    auto begin  (gl::Buffer_target target, std::size_t byte_count, Buffer_writer_flags flags = Buffer_writer_flag_masks::none) -> std::span<std::byte>;
    auto subspan(std::size_t byte_count) -> std::span<std::byte>;
    void end    ();
    void reset  ();
    void dump   ();

private:
    erhe::graphics::Instance& m_graphics_instance;
    erhe::graphics::Buffer&   m_buffer;
    std::span<std::byte>      m_map;
};

} // namespace erhe::renderer
