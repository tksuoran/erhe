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
    Imgui_renderer& m_imgui_renderer;
};

} // namespace erhe::imgui
