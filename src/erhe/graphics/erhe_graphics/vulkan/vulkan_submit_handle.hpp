#pragma once

#include "erhe_verify/verify.hpp"

#include <cstdint>

namespace erhe::graphics {

class Submit_handle {
public:
    uint32_t m_buffer_index{0};
    uint32_t m_submit_id   {0};

    Submit_handle() = default;

    explicit Submit_handle(uint64_t handle)
        : m_buffer_index{static_cast<uint32_t>(handle & 0xffffffff)}
        , m_submit_id   {static_cast<uint32_t>(handle >> 32)}
    {
        ERHE_VERIFY(m_submit_id);
    }

    [[nodiscard]] auto empty() const -> bool
    {
        return m_submit_id == 0;
    }

    [[nodiscard]] auto handle() const -> uint64_t
    {
        return (static_cast<uint64_t>(m_submit_id) << 32u) + m_buffer_index;
    }
};

static_assert(sizeof(Submit_handle) == sizeof(uint64_t));

}
