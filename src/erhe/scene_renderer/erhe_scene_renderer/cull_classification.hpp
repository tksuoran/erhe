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

// Whether the frame's GPU classification also runs the raster occlusion
// test (doc/plans/occlusion_culling.md): on, for a frame published for
// Cull_classification::gpu on a device whose Gpu_cull_classifier supports it
// (Gpu_cull_classifier::is_occlusion_supported()), the classification starts
// each occlusion view's visibility bits and the views' occlusion passes
// (Draw_list_scene::draw_occlusion()) set the bits of the entries they see.
// The classification of the next frame clears the view's mask bit of the
// entries the test found hidden (one frame of lag).
enum class Occlusion_culling : uint8_t
{
    off   = 0,
    on    = 1,
    count = 2
};

[[nodiscard]] auto c_str(Occlusion_culling occlusion_culling) -> const char*;

} // namespace erhe::scene_renderer
