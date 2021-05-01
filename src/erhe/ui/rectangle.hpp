#ifndef rectangle_hpp_erhe_ui
#define rectangle_hpp_erhe_ui

#include <algorithm>
#include <glm/glm.hpp>
#include <limits>

namespace erhe::ui
{

class Rectangle
{
public:
    Rectangle(float min_x, float min_y, float max_x, float max_y) noexcept
    {
        m_min.x = min_x;
        m_max.x = max_x;
        m_min.y = min_y;
        m_max.y = max_y;
    }

    Rectangle(float width, float height) noexcept
    {
        m_min.x = 0.0f;
        m_min.y = 0.0f;
        m_max.x = width - 1.0f;
        m_max.y = height - 1.0f;
    }

    Rectangle() noexcept
    {
        reset_for_grow();
    }

    Rectangle(glm::vec2 min, glm::vec2 max) noexcept
        : m_min{min}
        , m_max{max}
    {
    }

    Rectangle(const Rectangle& other) noexcept
        : m_min{other.m_min}
        , m_max{other.m_max}
    {
    }

    Rectangle(Rectangle&& other) noexcept
        : m_min{other.m_min}
        , m_max{other.m_max}
    {
    }

    auto operator=(const Rectangle& other) noexcept
    -> Rectangle&
    {
        m_min = other.m_min;
        m_max = other.m_max;
        return *this;
    }

    auto operator=(const Rectangle&& other) noexcept
    -> Rectangle&
    {
        m_min = other.m_min;
        m_max = other.m_max;
        return *this;
    }

    auto min() const noexcept
    -> const glm::vec2&
    {
        return m_min;
    }

    auto min() noexcept
    -> glm::vec2&
    {
        return m_min;
    }

    void set_min(glm::vec2 value) noexcept
    {
        m_min = value;
    }

    auto max() const noexcept
    -> const glm::vec2&
    {
        return m_max;
    }

    auto max() noexcept
    -> glm::vec2&
    {
        return m_max;
    }

    void set_max(glm::vec2 value) noexcept
    {
        m_max = value;
    }

    auto size() const noexcept
    -> glm::vec2
    {
        return glm::vec2(m_max.x - m_min.x + 1.0f,
                         m_max.y - m_min.y + 1.0f);
    }

    void set_size(glm::vec2 value) noexcept
    {
        m_max.x = m_min.x + value.x - 1.0f;
        m_max.y = m_min.y + value.y + 1.0f;
    }

    inline auto half_size() const noexcept
    -> glm::vec2
    {
        return size() / 2.0f;
    }

    inline auto center() const noexcept
    -> glm::vec2
    {
        return min() + half_size();
    }

    void reset_for_grow() noexcept
    {
        m_min.x =  std::numeric_limits<float>::max();
        m_max.x = -std::numeric_limits<float>::max();
        m_min.y =  std::numeric_limits<float>::max();
        m_max.y = -std::numeric_limits<float>::max();
    }

    auto hit(const glm::vec2& hit_position) const noexcept
    -> bool
    {
        if ((hit_position.x < m_min.x) ||
            (hit_position.y < m_min.y) ||
            (hit_position.x > m_max.x) ||
            (hit_position.y > m_max.y))
        {
            return false;
        }
        return true;
    }

    auto hit(const glm::ivec2& hit_position) const noexcept
    -> bool
    {
        if ((hit_position.x < m_min.x) ||
            (hit_position.y < m_min.y) ||
            (hit_position.x > m_max.x) ||
            (hit_position.y > m_max.y))
        {
            return false;
        }
        return true;
    }

    void extend_by(float x, float y) noexcept
    {
        m_min.x = std::min(m_min.x, x);
        m_max.x = std::max(m_max.x, x);
        m_min.y = std::min(m_min.y, y);
        m_max.y = std::max(m_max.y, y);
    }

    void clip_to(Rectangle reference) noexcept
    {
        m_min.x = std::max(m_min.x, reference.min().x);
        m_max.x = std::min(m_max.x, reference.max().x);
        m_min.y = std::max(m_min.y, reference.min().y);
        m_max.y = std::min(m_max.y, reference.max().y);
    }

    auto shrink(glm::vec2 padding) noexcept
    -> Rectangle
    {
        return Rectangle(m_min + padding, m_max - padding);
    }

    void grow_by(float padding_x, float padding_y) noexcept
    {
        m_min.x -= padding_x;
        m_min.y -= padding_y;
        m_max.x += padding_x;
        m_max.y += padding_y;
    }

//private:
    glm::vec2 m_min{0.0f};
    glm::vec2 m_max{0.0f};
};

} // namespace erhe::ui

#endif
