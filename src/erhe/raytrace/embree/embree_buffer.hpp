#pragma once

#include "erhe/raytrace/ibuffer.hpp"

#include <embree3/rtcore.h>

#include <string>

namespace erhe::raytrace
{

class Embree_buffer
    : public IBuffer
{
public:
    Embree_buffer(const std::string_view debug_label, const size_t capacity_byte_count); // rtcNewBuffer()
    ~Embree_buffer() override;                       // rtcReleaseBuffer()

    // rtcNewSharedBuffer()
    // rtcRetainBuffer()
    // rtcGetBufferData()

    [[nodiscard]] auto capacity_byte_count() const noexcept -> size_t override;
    [[nodiscard]] auto allocate_bytes     (const size_t byte_count, const size_t alignment = 64) noexcept -> size_t override;
    [[nodiscard]] auto span               () noexcept -> gsl::span<std::byte> override;

    [[nodiscard]] auto get_rtc_buffer() const -> RTCBuffer;
    [[nodiscard]] auto debug_label   () const -> std::string_view override;

private:
    RTCBuffer   m_buffer;
    size_t      m_capacity_byte_count{0};
    size_t      m_next_free_byte     {0};
    std::string m_debug_label;
};

}