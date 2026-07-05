#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <string_view>

namespace editor {

class Graph_editor_window_base;

// Companion window hosting a graph editor's node palette, so the palette and the
// canvas can be docked / sized independently. It owns no graph state; it forwards
// to Graph_editor_window_base::controls_imgui(). Shared by the geometry graph and
// the texture graph (each used to carry a byte-identical copy).
class Graph_editor_palette_window : public erhe::imgui::Imgui_window
{
public:
    Graph_editor_palette_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Graph_editor_window_base&    graph_window,
        std::string_view             title,
        std::string_view             ini_label
    );

    // Implements Imgui_window
    void imgui() override;

private:
    Graph_editor_window_base& m_graph_window;
};

} // namespace editor
