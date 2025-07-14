#pragma once

#include "erhe_profile/profile.hpp"

#include <cstddef>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace erhe::buffer {

class IBuffer
{
public:
    virtual ~IBuffer() noexcept {};

    [[nodiscard]] virtual auto get_capacity_byte_count () const noexcept -> std::size_t = 0;
    [[nodiscard]] virtual auto allocate_bytes          (std::size_t byte_count, std::size_t alignment = 64) noexcept -> std::optional<std::size_t> = 0;
    [[nodiscard]] virtual auto get_span                () noexcept -> std::span<std::byte> = 0;
    [[nodiscard]] virtual auto get_debug_label         () const -> std::string_view = 0;
    [[nodiscard]] virtual auto get_used_byte_count     () const -> std::size_t = 0;
    [[nodiscard]] virtual auto get_available_byte_count(std::size_t alignment = 64) const -> std::size_t = 0;
};

class Cpu_buffer : public erhe::buffer::IBuffer
{
public:
    Cpu_buffer           (const std::string_view debug_label, const std::size_t capacity_bytes_count);
    explicit Cpu_buffer  (Cpu_buffer&& other) noexcept;
    Cpu_buffer& operator=(Cpu_buffer&& other) noexcept;
    ~Cpu_buffer          () noexcept override;

    Cpu_buffer           (const Cpu_buffer&) = delete;
    Cpu_buffer& operator=(const Cpu_buffer&) = delete;

    // Implements IBuffer
    auto get_capacity_byte_count () const noexcept -> std::size_t                                                                        override;
    auto allocate_bytes          (const std::size_t byte_count, const std::size_t alignment = 64) noexcept -> std::optional<std::size_t> override;
    auto get_span                () noexcept -> std::span<std::byte>                                                                     override;
    auto get_debug_label         () const -> std::string_view                                                                            override;
    auto get_used_byte_count     () const -> std::size_t                                                                                 override;
    auto get_available_byte_count(std::size_t alignment) const -> std::size_t                                                            override;

private:
    ERHE_PROFILE_MUTEX(std::mutex, m_allocate_mutex);
    std::size_t                    m_capacity_byte_count{0};
    std::size_t                    m_next_free_byte     {0};

#if defined(ERHE_USE_PROFILE_ALLOCATOR)
    std::vector<std::byte, Profile_allocator<std::byte>> m_buffer;
#else
    std::vector<std::byte> m_buffer;
#endif
    std::span<std::byte>   m_span;
    std::string            m_debug_label;
};

} // namespace erhe::buffer
