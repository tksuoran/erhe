#pragma once

namespace editor {

// Which content draws the scene views' composition passes make, by the
// erhe::primitive::Blending_class of the primitive's material: all, or the
// opaque ones only. opaque_only leaves out the alpha-tested passes
// (alpha_test and screen_door materials: the draws whose shader discards)
// and the translucent passes (alpha_blend and the other blended modes), in
// every view (viewports and the headset), so a benchmark run measures what
// those draws cost. Session-only measurement switch
// (App_settings::content_draws_session; MCP set_content_draws), never
// persisted.
enum class Content_draws : unsigned int
{
    all         = 0,
    opaque_only = 1
};

inline constexpr const char* c_content_draws_strings[] = { "All", "Opaque Only" };

}
