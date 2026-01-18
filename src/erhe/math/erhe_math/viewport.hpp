#pragma once

#include <glm/glm.hpp>

#include <optional>

namespace erhe::math {

class Viewport
{
public:
    [[nodiscard]] auto aspect_ratio() const -> float;

    [[nodiscard]] auto unproject(
        const glm::mat4& world_from_clip,
        const glm::vec3& window,
        float            depth_range_near,
        float            depth_range_far
    ) const -> std::optional<glm::vec3>;

    [[nodiscard]] auto project_to_screen_space(
        const glm::mat4& clip_from_world,
        const glm::vec3& position_in_world,
        float            depth_range_near,
        float            depth_range_far
    ) const -> glm::vec3;

    [[nodiscard]] auto hit_test(int px, int py) const -> bool;

    int x     {0};
    int y     {0};
    int width {0};
    int height{0};
};

} // namespace erhe::math
