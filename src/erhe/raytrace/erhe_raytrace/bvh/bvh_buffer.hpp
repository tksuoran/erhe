#pragma once

#include "erhe_raytrace/ibuffer.hpp"

#include <mutex>
#include <vector>

namespace erhe::raytrace
{

class Bvh_buffer
    : public IBuffer
{
public:
    Bvh_buffer           (const std::string_view debug_label, const std::size_t capacity_bytes_count);
    explicit Bvh_buffer  (Bvh_buffer&& other) noexcept;
    Bvh_buffer& operator=(Bvh_buffer&& other) noexcept;
    ~Bvh_buffer          () noexcept override;

    Bvh_buffer           (const Bvh_buffer&) = delete;
    Bvh_buffer& operator=(const Bvh_buffer&) = delete;

    // Implements IBuffer
    [[nodiscard]] auto capacity_byte_count() const noexcept -> std::size_t override;
    [[nodiscard]] auto allocate_bytes     (const std::size_t byte_count, const std::size_t alignment = 64) noexcept -> std::size_t override;
    [[nodiscard]] auto span               () noexcept -> std::span<std::byte> override;
    [[nodiscard]] auto debug_label        () const -> std::string_view override;

private:
    std::mutex             m_allocate_mutex;
    std::size_t            m_capacity_byte_count{0};
    std::size_t            m_next_free_byte     {0};

    std::vector<std::byte> m_buffer;
    std::span<std::byte>   m_span;
    std::string            m_debug_label;
};

}
