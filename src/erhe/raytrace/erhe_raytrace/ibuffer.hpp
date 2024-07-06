#pragma once

#include <span>

#include <memory>
#include <string_view>

namespace erhe::raytrace {

class IBuffer
{
public:
    virtual ~IBuffer() noexcept {};

    [[nodiscard]] virtual auto capacity_byte_count() const noexcept -> std::size_t = 0;
    [[nodiscard]] virtual auto allocate_bytes     (std::size_t byte_count, std::size_t alignment = 64) noexcept -> std::size_t = 0;
    [[nodiscard]] virtual auto span               () noexcept -> std::span<std::byte> = 0;
    [[nodiscard]] virtual auto debug_label        () const -> std::string_view = 0;

    [[nodiscard]] static auto create       (const std::string_view debug_label, const std::size_t capacity_byte_count) -> IBuffer*;
    [[nodiscard]] static auto create_shared(const std::string_view debug_label, const std::size_t capacity_byte_count) -> std::shared_ptr<IBuffer>;
    [[nodiscard]] static auto create_unique(const std::string_view debug_label, const std::size_t capacity_byte_count) -> std::unique_ptr<IBuffer>;
};

} // namespace erhe::raytrace
