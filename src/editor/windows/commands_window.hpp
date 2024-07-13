#pragma once

#include "erhe_imgui/imgui_window.hpp"

namespace erhe::commands {
    enum class State : unsigned int;
}
namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor {

class Editor_context;

class Commands_window : public erhe::imgui::Imgui_window
{
public:
    Commands_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor
    );

    // Implements Imgui_window
    void imgui() override;

private:
    void filtered_commands(const erhe::commands::State filter);

    Editor_context& m_context;
};

} // namespace editor
