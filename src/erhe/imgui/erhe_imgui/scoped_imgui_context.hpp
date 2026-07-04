#pragma once

struct ImGuiContext;

namespace erhe::imgui {

class Imgui_host;
class Imgui_renderer;

// TODO Is this actually needed any more / at all?
class Scoped_imgui_context
{
public:
    explicit Scoped_imgui_context(Imgui_host& imgui_host);
    ~Scoped_imgui_context() noexcept;

private:
    Imgui_renderer&   m_imgui_renderer;
    // Restored on scope exit so that scopes nest correctly (e.g.
    // Imgui_host::save_imgui_ini called from code running inside an imgui
    // frame); top-level scopes restore the nullptr that was current before.
    const Imgui_host* m_previous_host{nullptr};
};

} // namespace erhe::imgui
