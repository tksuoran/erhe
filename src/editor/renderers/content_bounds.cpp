#include "renderers/content_bounds.hpp"

#include "scene/scene_root.hpp"

#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"

#include <algorithm>

namespace editor {

auto compute_padded_content_bounds(Scene_root& scene_root, const float padding_m) -> erhe::math::Aabb
{
    erhe::math::Aabb bounds{};

    const erhe::scene::Mesh_layer* content_layer = scene_root.layers().content();
    if (content_layer == nullptr) {
        return bounds;
    }

    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : content_layer->meshes) {
        if (!mesh || !mesh->is_visible() || !mesh->is_active()) {
            continue;
        }
        const erhe::math::Aabb mesh_bounds = mesh->get_aabb_world();
        if (!mesh_bounds.is_valid_3d()) {
            continue;
        }
        bounds.include(mesh_bounds);
    }
    if (!bounds.is_valid_3d()) {
        return bounds;
    }

    const float padding = std::max(0.0f, padding_m);
    bounds.min -= glm::vec3{padding};
    bounds.max += glm::vec3{padding};
    return bounds;
}

} // namespace editor
