#pragma once

#include "windows/property_editor.hpp"

#include "erhe_imgui/imgui_window.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace erhe        { class Item_base; }
namespace erhe::graph { class Link; }
namespace erhe::imgui { class Imgui_windows; }

namespace editor {

class App_context;
class Graph_editor_node;
class Graph_editor_window_base;
class Shader_graph_node;

class Node_properties_window : public erhe::imgui::Imgui_window
{
public:
    Node_properties_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context
    );

    // Implements Imgui_window
    void imgui   () override;
    void on_begin() override;
    void on_end  () override;

private:
    void item_flags     (const std::shared_ptr<erhe::Item_base>& item);
    void item_properties(const std::shared_ptr<erhe::Item_base>& item);
    void node_properties(Shader_graph_node& node);

    // Geometry / texture graph nodes: their selection lives purely in each
    // graph window's ax::NodeEditor canvas (issue #252), so it is gathered
    // from every graph editor window (primaries + extra instances) instead
    // of the global selection.
    void collect_graph_editor_selection();
    void graph_editor_node_properties(Graph_editor_window_base& window, const std::shared_ptr<Graph_editor_node>& node);
    void graph_editor_link_properties(Graph_editor_window_base& window, erhe::graph::Link* link);

    App_context&       m_context;
    Property_editor    m_property_editor;
    // Scratch, cleared + refilled every frame (capacity persists): the canvas
    // selection of every graph editor window, with the owning window. Links
    // are kept per window (no cross-window dedup): curve routing is canvas
    // state, so two windows editing the same graph each have their own.
    std::vector<std::pair<Graph_editor_window_base*, std::shared_ptr<Graph_editor_node>>> m_graph_editor_selection;
    std::vector<std::shared_ptr<Graph_editor_node>>                                       m_selected_nodes_scratch;
    std::vector<std::pair<Graph_editor_window_base*, erhe::graph::Link*>>                 m_graph_editor_link_selection;
    std::vector<erhe::graph::Link*>                                                       m_selected_links_scratch;
};

}
