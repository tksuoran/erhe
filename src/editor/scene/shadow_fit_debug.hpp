#pragma once

#include <nlohmann/json_fwd.hpp>

namespace erhe::scene_renderer {
    class Light_projections;
}

namespace editor {

// Serializes the directional shadow frustum fit debug geometry held in one
// scene view's Light_projections into JSON: per shadow-casting light the
// F_shadow planes, their bounded face quads (the truncated view frustum faces
// the caster AABBs are tested against), and the receiver (view frustum)
// corners, plus the view camera name and shadow range.
//
// Shared by the Debug Visualizations "Dump Shadow Fit to Log" button and the
// MCP get_shadow_fit_debug tool. The geometry is only populated when the fit
// ran with Shadow_frustum_fit_settings::collect_debug enabled; otherwise the
// per-light entries report valid = false with empty arrays.
[[nodiscard]] auto dump_shadow_fit_debug(const erhe::scene_renderer::Light_projections& light_projections) -> nlohmann::json;

} // namespace editor
