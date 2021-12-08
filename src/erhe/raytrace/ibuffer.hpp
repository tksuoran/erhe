#pragma once

#include <gsl/span>

#include <memory>
#include <string_view>

namespace erhe::raytrace
{

class IBuffer
{
public:
    virtual ~IBuffer(){};

    virtual [[nodiscard]] auto capacity_byte_count() const noexcept -> size_t = 0;
    virtual [[nodiscard]] auto allocate_bytes     (const size_t byte_count, const size_t alignment = 64) noexcept -> size_t = 0;
    virtual [[nodiscard]] auto span               () noexcept -> gsl::span<std::byte> = 0;
    virtual [[nodiscard]] auto debug_label        () const -> std::string_view = 0;

    static [[nodiscard]] auto create       (const std::string_view debug_label, const size_t capacity_byte_count) -> IBuffer*;
    static [[nodiscard]] auto create_shared(const std::string_view debug_label, const size_t capacity_byte_count) -> std::shared_ptr<IBuffer>;
    static [[nodiscard]] auto create_unique(const std::string_view debug_label, const size_t capacity_byte_count) -> std::unique_ptr<IBuffer>;
};

} // namespace erhe::raytrace
