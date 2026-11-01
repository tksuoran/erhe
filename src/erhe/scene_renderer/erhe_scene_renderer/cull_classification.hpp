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

// Whether the frame's classification applies the potentially visible set
// rule (doc/plans/pvs.md "Runtime consumption"): on, for a frame published
// while the Draw_list_scene's database is ready, the classification (both
// paths) clears a color view's mask bit of a tracked entry that no cell
// around the view's query box (Cull_view_tests::pvs_query_min / max) can
// see. An absent or stale database culls nothing.
enum class Pvs_culling : uint8_t
{
    off   = 0,
    on    = 1,
    count = 2
};

[[nodiscard]] auto c_str(Pvs_culling pvs_culling) -> const char*;

// Whether the frame's classification selects levels of detail
// (doc/plans/lod_gpu_selection.md): on, the frame's lod_header is active
// when the Draw_list_scene has entries with a chain. Off (and on in a scene
// without chains): every draw reads its entry's template range (the forced
// level, or level 0).
enum class Lod_selection : uint8_t
{
    off   = 0,
    on    = 1,
    count = 2
};

[[nodiscard]] auto c_str(Lod_selection lod_selection) -> const char*;

// Where the CPU's consumers of the selected levels read them
// (doc/plans/lod_gpu_selection.md section 9, decisions 5 and 6;
// doc/erhe/draw_list_renderer.md R1c "CPU level source"):
// - every_frame: the CPU classification runs the selection lanes every
//   frame (cull_lanes::lod_lanes(), shadow_lod_lanes()), so the CPU command
//   writer, the edge-line feed, the drawn-level results and the shadow map
//   cache read the frame's own levels;
// - on_demand: under Cull_classification::gpu (the compaction writes the
//   commands) the lanes run only on a classification check's frame; the
//   CPU consumers read the levels the GPU selected for the retired frame
//   (frames in flight late) and the shadow map cache its per-column level
//   changes. Published as every_frame (one info log) where the frame is
//   classified on the CPU.
enum class Lod_cpu_selection : uint8_t
{
    every_frame = 0,
    on_demand   = 1,
    count       = 2
};

[[nodiscard]] auto c_str(Lod_cpu_selection lod_cpu_selection) -> const char*;

// The frame's level of detail settings (Draw_list_scene::publish(), from the
// editor's graphics preset): the selection switch, the pixel error
// threshold, the global bias added to every selected level
// (doc/plans/lod.md sections 2.3 and 2.5) and the hysteresis band
// (doc/plans/lod_gpu_selection.md section 6): in [0, 1), an entry moves to
// a coarser level only once that level's error stays within
// threshold_pixels * (1 - hysteresis_band); 0 = no hysteresis; and where
// the CPU consumers read the levels (Lod_cpu_selection).
class Lod_frame_settings
{
public:
    Lod_selection     selection       {Lod_selection::off};
    float             threshold_pixels{1.0f};
    int               bias            {0};
    float             hysteresis_band {0.0f};
    Lod_cpu_selection cpu_selection   {Lod_cpu_selection::every_frame};
};

// The largest hysteresis band the editor's settings accept (a band near 1
// would never coarsen).
constexpr float max_lod_hysteresis_band{0.9f};

} // namespace erhe::scene_renderer
