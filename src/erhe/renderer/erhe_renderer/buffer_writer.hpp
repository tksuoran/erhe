#pragma once

#include <gsl/span>

#include <cstddef>

namespace erhe::graphics
{
    class Buffer;
    class Instance;
}

namespace erhe::renderer
{

class Buffer_range
{
public:
    std::size_t first_byte_offset{0};
    std::size_t byte_count       {0};
};

class Buffer_writer
{
public:
    explicit Buffer_writer(igl::IDevice& device);

    Buffer_range range;
    std::size_t  map_offset  {0};
    std::size_t  write_offset{0};
    std::size_t  write_end   {0};

    void shader_storage_align();
    void uniform_align       ();
    auto begin               (erhe::graphics::Buffer* buffer, std::size_t byte_count) -> gsl::span<std::byte>;
    auto subspan             (std::size_t byte_count) -> gsl::span<std::byte>;
    void end                 ();
    void reset               ();
    void dump                ();

private:
    erhe::graphics::Instance& m_instance;
    erhe::graphics::Buffer*   m_buffer{nullptr};
    gsl::span<std::byte>      m_map;
};

} // namespace erhe::renderer
