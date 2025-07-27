#pragma once

#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

class Scissor_state
{
public:
    int x     {0};
    int y     {0};
    int width {0xffff};
    int height{0xffff};
};

class Scissor_state_hash
{
public:
    [[nodiscard]]
    auto operator()(const Scissor_state& state) const noexcept -> std::size_t
    {
        ERHE_VERIFY(state.x      <= 0xffffu);
        ERHE_VERIFY(state.y      <= 0xffffu);
        ERHE_VERIFY(state.width  <= 0xffffu);
        ERHE_VERIFY(state.height <= 0xffffu);
        return
            ((static_cast<std::size_t>(state.x     ) & 0xffffu)      ) |
            ((static_cast<std::size_t>(state.y     ) & 0xffffu) << 16) |
            ((static_cast<std::size_t>(state.width ) & 0xffffu) << 32) |
            ((static_cast<std::size_t>(state.height) & 0xffffu) << 38);
    }
};

[[nodiscard]] auto operator==(const Scissor_state& lhs, const Scissor_state& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Scissor_state& lhs, const Scissor_state& rhs) noexcept -> bool;

} // namespace erhe::graphics
