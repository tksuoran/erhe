#pragma once

#include "erhe/raytrace/ibuffer.hpp"

#include <mutex>
#include <vector>

namespace erhe::raytrace
{

class Null_buffer
    : public IBuffer
{
public:
    explicit Null_buffer  (const std::string_view debug_label, const size_t capacity_bytes_count);
    explicit Null_buffer  (Null_buffer&& other) noexcept;
    Null_buffer& operator=(Null_buffer&& other) noexcept;
    ~Null_buffer          () noexcept override;

    Null_buffer           (const Null_buffer&) = delete;
    Null_buffer& operator=(const Null_buffer&) = delete;

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