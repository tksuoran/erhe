#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <functional>

typedef int ImGuiWindowFlags;

namespace editor {

/// Concrete Imgui_window that delegates its virtual methods to callbacks.
/// Used by Tool subclasses that need an ImGui settings panel without
/// inheriting from Imgui_window directly (composition over inheritance).
class Tool_window : public erhe::imgui::Imgui_window
{
public:
    Tool_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        std::string_view             title,
        std::string_view             ini_label,
        std::function<void()>        imgui_callback,
        bool                         developer = false
    );

    // Optional callbacks — set after construction if needed.
    std::function<ImGuiWindowFlags()> flags_callback;
    std::function<void()>             on_begin_callback;
    std::function<void()>             on_end_callback;

    // Implements Imgui_window
    void imgui   () override;
    auto flags   () -> ImGuiWindowFlags override;
    void on_begin() override;
    void on_end  () override;

private:
    std::function<void()> m_imgui_callback;
};

} // namespace editor
