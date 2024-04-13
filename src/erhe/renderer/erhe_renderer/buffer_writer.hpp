#pragma once

#include <span>

#include <cstddef>

namespace igl
{
    class IBuffer;
    class IDevice;
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

    auto begin  (igl::IBuffer* buffer, std::size_t byte_count) -> std::span<std::byte>;
    auto subspan(std::size_t byte_count) -> std::span<std::byte>;
    void end    ();
    void reset  ();
    void dump   ();

private:
    igl::IDevice&        m_device;
    igl::IBuffer*        m_buffer{nullptr};
    std::span<std::byte> m_range;
};

} // namespace erhe::renderer
