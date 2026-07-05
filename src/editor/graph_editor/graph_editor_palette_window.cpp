#include "graph_editor/graph_editor_palette_window.hpp"
#include "graph_editor/graph_editor_window_base.hpp"

namespace editor {

Graph_editor_palette_window::Graph_editor_palette_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Graph_editor_window_base&    graph_window,
    std::string_view             title,
    std::string_view             ini_label
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, title, ini_label}
    , m_graph_window           {graph_window}
{
}

void Graph_editor_palette_window::imgui()
{
    m_graph_window.controls_imgui();
}

} // namespace editor
