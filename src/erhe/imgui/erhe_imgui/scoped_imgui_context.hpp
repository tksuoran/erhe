#pragma once

#include <cstdint>
#include <mutex>

struct ImGuiContext;

namespace erhe::imgui
{

class Imgui_viewport;
class Imgui_renderer;

class Scoped_imgui_context
{
public:
    explicit Scoped_imgui_context(Imgui_viewport& imgui_viewport);
    ~Scoped_imgui_context() noexcept;

private:
    Imgui_renderer& m_imgui_renderer;
};

} // namespace erhe::imgui
