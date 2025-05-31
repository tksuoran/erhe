#pragma once

#include <glm/glm.hpp>

namespace erhe::math {

class Aabb
{
public:
    [[nodiscard]] auto diagonal() const -> glm::vec3
    {
        return max - min;
    }
    [[nodiscard]] auto center() const -> glm::vec3
    {
        return (max + min) * 0.5f;
    }
    [[nodiscard]] auto volume() const -> float
    {
        const auto d = diagonal();
        return d.x * d.y * d.z;
    }
    [[nodiscard]] auto is_valid() const -> bool
    {
        return
            (min.x <= max.x) ||
            (min.y <= max.y) ||
            (min.z <= max.z);
    }
    void include(const glm::vec3 p)
    {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }
    void include(const Aabb& bbox)
    {
        if (!is_valid()) {
            min = bbox.min;
            max = bbox.max;
            return;
        }
        min = glm::min(min, bbox.min);
        max = glm::max(max, bbox.max);
    }

    [[nodiscard]] auto transformed_by(const glm::mat4& m) const -> Aabb
    {
        glm::vec4 corners[8] = {
            { min.x, min.y, min.z, 1.0f },
            { max.x, min.y, min.z, 1.0f },
            { max.x, max.y, min.z, 1.0f },
            { min.x, max.y, min.z, 1.0f },
            { min.x, min.y, max.z, 1.0f },
            { max.x, min.y, max.z, 1.0f },
            { max.x, max.y, max.z, 1.0f },
            { min.x, max.y, max.z, 1.0f }
        };

        Aabb result{};
        for (glm::vec4 corner : corners) {
            glm::vec4 transformedH{m * corner};
            glm::vec3 transformed = glm::vec3{transformedH} / transformedH.w;
            result.include(transformed);
        }
        return result;
    }

    glm::vec3 min{std::numeric_limits<float>::max()}; // bounding box
    glm::vec3 max{std::numeric_limits<float>::lowest()};
};

} // namespace erhe::math
