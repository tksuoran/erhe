#pragma once

#include <gsl/span>

#include <memory>

namespace erhe::raytrace
{

class IBuffer
{
public:
    virtual auto capacity_byte_count() const noexcept -> size_t = 0;
    virtual auto allocate_bytes     (const size_t byte_count, const size_t alignment = 64) noexcept -> size_t = 0;
    virtual auto span               () noexcept -> gsl::span<std::byte> = 0;

    static auto create(const size_t capacity_bytes_count) -> IBuffer*;
    static auto create_shared(const size_t capacity_bytes_count) -> std::shared_ptr<IBuffer>;
};



} // namespace erhe::raytrace
