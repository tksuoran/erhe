#pragma once

#include <cstdint>

namespace erhe::scene_renderer {

// Where the culling classification of the draw lists runs
// (doc/plans/culling.md "Two complete paths"): cpu, the lane loops of
// Draw_list_scene::classify_cull_views(); gpu, the same classification also
// computed by a compute pass (Gpu_cull_classifier) into GPU mask regions,
// from which a second compute pass generates the shadow passes' draw
// commands (phase 6, Draw_list_scene::set_shadow_command_selection()). The
// CPU masks drive the color passes either way.
enum class Cull_classification : uint8_t
{
    cpu   = 0,
    gpu   = 1,
    count = 2
};

[[nodiscard]] auto c_str(Cull_classification classification) -> const char*;

} // namespace erhe::scene_renderer
