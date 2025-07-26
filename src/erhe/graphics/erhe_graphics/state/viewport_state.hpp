#pragma once

#include <functional>

namespace erhe::graphics {

class Viewport_rect_state
{
public:
    float x        {0.0f};
    float y        {0.0f};
    float width    {0.0f};
    float height   {0.0f};
};

class Viewport_rect_state_hash
{
public:
    [[nodiscard]]
    auto operator()(const Viewport_rect_state& state) const noexcept -> std::size_t
    {
        return
            ( std::hash<float>{}(state.x        )) ^
            ( std::hash<float>{}(state.y        )) ^
            ( std::hash<float>{}(state.width    )) ^
            ( std::hash<float>{}(state.height   ));
    }
};

[[nodiscard]] auto operator==(const Viewport_rect_state& lhs, const Viewport_rect_state& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Viewport_rect_state& lhs, const Viewport_rect_state& rhs) noexcept -> bool;

//

class Viewport_depth_range_state
{
public:
    float min_depth{0.0f}; // OpenGL near
    float max_depth{1.0f}; // OpenGL far
};

class Viewport_depth_range_state_hash
{
public:
    [[nodiscard]]
    auto operator()(const Viewport_depth_range_state& state) const noexcept -> std::size_t
    {
        return
            ( std::hash<float>{}(state.min_depth)) ^
            ( std::hash<float>{}(state.max_depth));
    }
};

[[nodiscard]] auto operator==(const Viewport_depth_range_state& lhs, const Viewport_depth_range_state& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Viewport_depth_range_state& lhs, const Viewport_depth_range_state& rhs) noexcept -> bool;

} // namespace erhe::graphics
