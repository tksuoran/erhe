#pragma once

#include "erhe_math/aabb.hpp"

namespace editor {

class Scene_root;

// Union of the visible content meshes' world bounds, grown by padding_m on
// every side: the box the indirect diffuse probe volumes are fitted to
// (doc/editor/ddgi.md "Data layout", doc/editor/radiance_cascades.md).
// Skinned meshes are included: they do not go into the acceleration
// structure, but they are lit by the volume, so the volume has to cover
// them. Invalid when the scene has no visible content.
[[nodiscard]] auto compute_padded_content_bounds(Scene_root& scene_root, float padding_m) -> erhe::math::Aabb;

} // namespace editor
