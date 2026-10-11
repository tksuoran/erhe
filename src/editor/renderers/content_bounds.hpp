#pragma once

#include "erhe_math/aabb.hpp"

namespace editor {

// content_bounds grown by padding_m on every side: the box the indirect
// diffuse probe volumes are fitted to (doc/editor/ddgi.md "Data layout",
// doc/editor/radiance_cascades.md). content_bounds is the union of the
// visible content meshes' world bounds the prepare phase publishes
// (Ray_trace_slot::content_bounds), skinned meshes included: they do not go
// into the acceleration structure, but they are lit by the volume, so the
// volume has to cover them. Invalid when content_bounds is.
[[nodiscard]] auto pad_content_bounds(const erhe::math::Aabb& content_bounds, float padding_m) -> erhe::math::Aabb;

} // namespace editor
