#include "renderers/content_bounds.hpp"

#include <algorithm>

namespace editor {

auto pad_content_bounds(const erhe::math::Aabb& content_bounds, const float padding_m) -> erhe::math::Aabb
{
    erhe::math::Aabb bounds = content_bounds;
    if (!bounds.is_valid_3d()) {
        return bounds;
    }
    const float padding = std::max(0.0f, padding_m);
    bounds.min -= glm::vec3{padding};
    bounds.max += glm::vec3{padding};
    return bounds;
}

} // namespace editor
