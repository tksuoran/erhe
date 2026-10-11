#pragma once

namespace editor {

class Render_settings;
class Render_settings_frames;

// App_context::render_settings: a pointer-like view of the Render_settings
// copies (Render_settings_frames, renderers/render_settings.hpp) that
// resolves to the calling role's copy at each access. The render role (the
// dedicated render thread) reads the copy of the frame it renders, the
// main role (M1, M2, M3, MCP, and the render phase in inline mode, where
// the main thread renders the frame it just prepared) the copy of the
// latest snapshot. Keyed by role at the access, the call sites read
// render_settings->x the same way from either phase.
class Render_settings_access
{
public:
    [[nodiscard]] auto operator->() const -> const Render_settings*;
    [[nodiscard]] auto operator* () const -> const Render_settings&;

    const Render_settings_frames* frames{nullptr};
};

} // namespace editor
