#pragma once

#include "erhe/raytrace/ibuffer.hpp"

#include <mutex>
#include <vector>

namespace erhe::raytrace
{

class Bvh_buffer
    : public IBuffer
{
public:
    explicit Bvh_buffer  (const std::string_view debug_label, const size_t capacity_bytes_count);
    explicit Bvh_buffer  (Bvh_buffer&& other) noexcept;
    Bvh_buffer& operator=(Bvh_buffer&& other) noexcept;
    ~Bvh_buffer          () noexcept override;

    Bvh_buffer           (const Bvh_buffer&) = delete;
    Bvh_buffer& operator=(const Bvh_buffer&) = delete;

    // Implements IBuffer
    [[nodiscard]] auto capacity_byte_count() const noexcept -> size_t override;
    [[nodiscard]] auto allocate_bytes     (const size_t byte_count, const size_t alignment = 64) noexcept -> size_t override;
    [[nodiscard]] auto span               () noexcept -> gsl::span<std::byte> override;
    [[nodiscard]] auto debug_label        () const -> std::string_view override;

private:
    std::mutex             m_allocate_mutex;
    size_t                 m_capacity_byte_count{0};
    size_t                 m_next_free_byte     {0};

    std::vector<std::byte> m_buffer;
    gsl::span<std::byte>   m_span;
    std::string            m_debug_label;
};

}
