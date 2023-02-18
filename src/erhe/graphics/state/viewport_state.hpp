#pragma once

#include <functional>

namespace erhe::graphics
{

class Viewport_state
{
public:
    Viewport_state()
        : serial{get_next_serial()}
    {
    }

    Viewport_state(
        float x,
        float y,
        float width,
        float height,
        float min_depth,
        float max_depth
    )
        : serial   {get_next_serial()}
        , x        {x}
        , y        {y}
        , width    {width}
        , height   {height}
        , min_depth{min_depth}
        , max_depth{max_depth}
    {
    }

    void touch()
    {
        serial = get_next_serial();
    }

    std::size_t serial;
    float       x         {0.0f};
    float       y         {0.0f};
    float       width     {0.0f};
    float       height    {0.0f};
    float       min_depth {0.0f}; // OpenGL near
    float       max_depth {1.0f}; // OpenGL far

    // TODO scissors

    [[nodiscard]] static auto get_next_serial() -> size_t
    {
        do {
            s_serial++;
        } while (s_serial == 0);

        return s_serial;
    }

    static std::size_t s_serial;
};

class Viewport_state_hash
{
public:
    [[nodiscard]]
    auto operator()(const Viewport_state& state) const noexcept -> std::size_t
    {
        return
            (
                std::hash<float>{}(state.min_depth)
            ) ^
            (
                std::hash<float>{}(state.max_depth)
            );
    }
};

[[nodiscard]] auto operator==(
    const Viewport_state& lhs,
    const Viewport_state& rhs
) noexcept -> bool;

[[nodiscard]] auto operator!=(
    const Viewport_state& lhs,
    const Viewport_state& rhs
) noexcept -> bool;

class Viewport_state_tracker
{
public:
    void reset();

    void execute(const Viewport_state* state);

private:
    std::size_t    m_last{0};
    Viewport_state m_cache;
};

} // namespace erhe::graphics
