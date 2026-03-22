#pragma once

#include "erhe_buffer/free_list_allocator.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace erhe::buffer {

class IBuffer
{
public:
    virtual ~IBuffer() noexcept;

    [[nodiscard]] virtual auto get_capacity_byte_count () const noexcept -> std::size_t = 0;
    [[nodiscard]] virtual auto allocate_bytes          (std::size_t byte_count, std::size_t alignment) noexcept -> std::optional<std::size_t> = 0;
    virtual                    void free_bytes         (std::size_t byte_offset, std::size_t byte_count) noexcept = 0;
    [[nodiscard]] virtual auto get_span                () noexcept -> std::span<std::byte> = 0;
    [[nodiscard]] virtual auto get_debug_label         () const -> std::string_view = 0;
    [[nodiscard]] virtual auto get_used_byte_count     () const -> std::size_t = 0;
    [[nodiscard]] virtual auto get_available_byte_count(std::size_t alignment) const -> std::size_t = 0;
    [[nodiscard]] virtual auto get_allocator           () -> Free_list_allocator& = 0;
};

class Cpu_buffer : public IBuffer
{
public:
    Cpu_buffer           (std::string_view debug_label, std::size_t capacity_bytes_count);
    explicit Cpu_buffer  (Cpu_buffer&& other) noexcept;
    Cpu_buffer& operator=(Cpu_buffer&& other) noexcept;
    ~Cpu_buffer          () noexcept override;

    Cpu_buffer           (const Cpu_buffer&) = delete;
    Cpu_buffer& operator=(const Cpu_buffer&) = delete;

    // Implements IBuffer
    [[nodiscard]] auto get_capacity_byte_count () const noexcept -> std::size_t                                   override;
    [[nodiscard]] auto allocate_bytes          (std::size_t byte_count, std::size_t alignment) noexcept -> std::optional<std::size_t> override;
                  void free_bytes              (std::size_t byte_offset, std::size_t byte_count) noexcept         override;
    [[nodiscard]] auto get_span                () noexcept -> std::span<std::byte>                                override;
    [[nodiscard]] auto get_debug_label         () const -> std::string_view                                       override;
    [[nodiscard]] auto get_used_byte_count     () const -> std::size_t                                            override;
    [[nodiscard]] auto get_available_byte_count(std::size_t alignment) const -> std::size_t                       override;
    [[nodiscard]] auto get_allocator           () -> Free_list_allocator&                                         override;

private:
    Free_list_allocator m_allocator;

#if defined(ERHE_USE_PROFILE_ALLOCATOR)
    std::vector<std::byte, Profile_allocator<std::byte>> m_buffer;
#else
    std::vector<std::byte> m_buffer;
#endif
    std::span<std::byte>   m_span;
    std::string            m_debug_label;
};

} // namespace erhe::buffer
