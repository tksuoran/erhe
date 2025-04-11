#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "graph/graph_window.hpp"
#include "graph/constant.hpp"
#include "graph/add.hpp"
#include "graph/passthrough.hpp"
#include "graph/subtract.hpp"
#include "graph/multiply.hpp"
#include "graph/divide.hpp"
#include "graph/load.hpp"
#include "graph/store.hpp"
#include "windows/sheet_window.hpp"
#include "windows/property_editor.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_node_editor.h"

#include <imgui/imgui.h>

namespace editor {

class Node_style_editor_window : public erhe::imgui::Imgui_window
{
public:
    Node_style_editor_window(
        erhe::imgui::Imgui_renderer&   imgui_renderer,
        erhe::imgui::Imgui_windows&    imgui_windows,
        ax::NodeEditor::EditorContext& node_editor
    )
        : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Graph Style", "graph_style"}
    {
        Property_editor& e = m_property_editor;
        auto& style = node_editor.GetStyle();
        style.PinArrowSize  = 14.0f;
        style.PinArrowWidth = 14.0f;
        style.LinkStrength  = 30.0f;
        style.NodePadding   = ImVec4{20.0f, 5.0f, 20.0f, 5.0f};
        e.add_entry("Node Pad 0",    [&style](){ ImGui::DragFloat("##Node Pad 0",    &style.NodePadding.x,      0.1f, 0.0f, 20.0f); });
        e.add_entry("Node Pad 1",    [&style](){ ImGui::DragFloat("##Node Pad 1",    &style.NodePadding.y,      0.1f, 0.0f, 20.0f); });
        e.add_entry("Node Pad 2",    [&style](){ ImGui::DragFloat("##Node Pad 2",    &style.NodePadding.z,      0.1f, 0.0f, 20.0f); });
        e.add_entry("Node Pad 3",    [&style](){ ImGui::DragFloat("##Node Pad 3",    &style.NodePadding.w,      0.1f, 0.0f, 20.0f); });
        e.add_entry("Pin Round",     [&style](){ ImGui::DragFloat("##Pin Round",     &style.PinRounding,        0.1f, 0.0f, 40.0f); });
        e.add_entry("Pin Border",    [&style](){ ImGui::DragFloat("##Pin Border",    &style.PinBorderWidth,     0.1f, 0.0f, 15.0f); });
        e.add_entry("Pin Rad",       [&style](){ ImGui::DragFloat("##Pin Rad",       &style.PinRadius,          0.1f, 0.0f, 40.0f); });
        e.add_entry("Pin Arrow S.",  [&style](){ ImGui::DragFloat("##Pin Arrow S.",  &style.PinArrowSize,       0.1f, 0.0f, 40.0f); });
        e.add_entry("Pin Arrow W.",  [&style](){ ImGui::DragFloat("##Pin Arrow W.",  &style.PinArrowWidth,      0.1f, 0.0f, 40.0f); });
        e.add_entry("Link Str",      [&style](){ ImGui::DragFloat("##Link Str",      &style.LinkStrength,       1.0f, 0.0f, 500.0f); });
        e.add_entry("Source Dir X",  [&style](){ ImGui::DragFloat("##Source Dir X",  &style.SourceDirection.x,  0.1f, -1.0f, 1.0f); });
        e.add_entry("Source Dir Y",  [&style](){ ImGui::DragFloat("##Source Dir Y",  &style.SourceDirection.y,  0.1f, -1.0f, 1.0f); });
        e.add_entry("Target Dir X",  [&style](){ ImGui::DragFloat("##Target Dir X",  &style.TargetDirection.x,  0.1f, -1.0f, 1.0f); });
        e.add_entry("Target Dir Y",  [&style](){ ImGui::DragFloat("##Target Dir Y",  &style.TargetDirection.y,  0.1f, -1.0f, 1.0f); });
        e.add_entry("Pivot Align X", [&style](){ ImGui::DragFloat("##Pivot Align X", &style.PivotAlignment.x,   0.1f, -2.0f, 2.0f); });
        e.add_entry("Pivot Align Y", [&style](){ ImGui::DragFloat("##Pivot Align Y", &style.PivotAlignment.y,   0.1f, -2.0f, 2.0f); });
        e.add_entry("Pivot Size X",  [&style](){ ImGui::DragFloat("##Pivot Size X",  &style.PivotSize.x,        0.1f, -2.0f, 2.0f); });
        e.add_entry("Pivot Size Y",  [&style](){ ImGui::DragFloat("##Pivot Size Y",  &style.PivotSize.y,        0.1f, -2.0f, 2.0f); });
        e.add_entry("Pivot Scale X", [&style](){ ImGui::DragFloat("##Pivot Scale X", &style.PivotScale.x,       0.1f, -2.0f, 2.0f); });
        e.add_entry("Pivot Scale Y", [&style](){ ImGui::DragFloat("##Pivot Scale Y", &style.PivotScale.y,       0.1f, -2.0f, 2.0f); });
    }

    void imgui() override
    {
        m_property_editor.reset_row();
        m_property_editor.show_entries();
    }

private:
    Property_editor m_property_editor;
};

Graph_window::Graph_window(
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context,
    Editor_message_bus&          editor_message_bus
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Graph", "graph"}
    , m_context                {editor_context}
{
    static_cast<void>(commands); // TODO Keeping in case we need to add commands here

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            on_message(message);
        }
    );

    ax::NodeEditor::Config config;
    m_node_editor = std::make_unique<ax::NodeEditor::EditorContext>(nullptr);

    m_style_editor_window = std::make_unique<Node_style_editor_window>(imgui_renderer, imgui_windows, *m_node_editor.get());
}

Graph_window::~Graph_window() noexcept
{
}

void Graph_window::on_message(Editor_message&)
{
    //// using namespace erhe::bit;
    //// if (test_any_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_selection)) {
    //// }
}

auto Graph_window::flags() -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

auto Graph_window::make_constant() -> Shader_graph_node*
{
    m_nodes.push_back(std::make_shared<Constant>());
    Shader_graph_node* node = m_nodes.back().get();
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    return node;
}

auto Graph_window::make_passthrough() -> Shader_graph_node*
{
    m_nodes.push_back(std::make_shared<Passthrough>());
    Shader_graph_node* node = m_nodes.back().get();
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    return node;
}

auto Graph_window::make_load() -> Shader_graph_node*
{
    m_nodes.push_back(std::make_shared<Load>());
    Shader_graph_node* node = m_nodes.back().get();
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    return node;
}
auto Graph_window::make_store() -> Shader_graph_node*
{
    m_nodes.push_back(std::make_shared<Store>());
    Shader_graph_node* node = m_nodes.back().get();
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    return node;
}

auto Graph_window::make_add() -> Shader_graph_node*
{
    m_nodes.push_back(std::make_shared<Add>());
    Shader_graph_node* node = m_nodes.back().get();
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    return node;
}

auto Graph_window::make_sub() -> Shader_graph_node*
{
    m_nodes.push_back(std::make_shared<Subtract>());
    Shader_graph_node* node = m_nodes.back().get();
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    return node;
}

auto Graph_window::make_mul() -> Shader_graph_node*
{
    m_nodes.push_back(std::make_shared<Multiply>());
    Shader_graph_node* node = m_nodes.back().get();
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    return node;
}

auto Graph_window::make_div() -> Shader_graph_node*
{
    m_nodes.push_back(std::make_shared<Divide>());
    Shader_graph_node* node = m_nodes.back().get();
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    return node;
}

void Graph_window::imgui()
{
                       if (ImGui::Button("+")) { m_graph.register_node(make_add()); }
    ImGui::SameLine(); if (ImGui::Button("-")) { m_graph.register_node(make_sub()); }
    ImGui::SameLine(); if (ImGui::Button("*")) { m_graph.register_node(make_mul()); }
    ImGui::SameLine(); if (ImGui::Button("/")) { m_graph.register_node(make_div()); }
    ImGui::SameLine(); if (ImGui::Button("=")) { m_graph.register_node(make_passthrough()); }
    ImGui::SameLine(); if (ImGui::Button("1")) { m_graph.register_node(make_constant()); }
    ImGui::SameLine(); if (ImGui::Button("L")) { m_graph.register_node(make_load()); }
    ImGui::SameLine(); if (ImGui::Button("S")) { m_graph.register_node(make_store()); }

    Sheet_window* sheet_window = m_context.sheet_window;
    Sheet* sheet = (sheet_window != nullptr) ? sheet_window->get_sheet() : nullptr;
    m_graph.evaluate(sheet);

    m_node_editor->Begin("Graph", ImVec2{0.0f, 0.0f});

    for (erhe::graph::Node* node : m_graph.get_nodes()) {
        Shader_graph_node* shader_graph_node = dynamic_cast<Shader_graph_node*>(node);
        shader_graph_node->node_editor(m_context, *m_node_editor.get());
    }

    // Links
    for (const std::unique_ptr<erhe::graph::Link>& link : m_graph.get_links()) {
        log_graph_editor->info(
            "  Link source {} {} sink {} {}",
            link->get_source()->get_name(), link->get_source()->get_id(),
            link->get_sink  ()->get_name(), link->get_sink  ()->get_id()
        );
        m_node_editor->Link(
            ax::NodeEditor::LinkId{link.get()},
            ax::NodeEditor::PinId{link->get_source()},
            ax::NodeEditor::PinId{link->get_sink()}
        );
    }

    if (m_node_editor->BeginCreate()) {
        ax::NodeEditor::PinId lhs_pin_handle;
        ax::NodeEditor::PinId rhs_pin_handle;
        if (m_node_editor->QueryNewLink(&lhs_pin_handle, &rhs_pin_handle)) {
            bool acceptable = false;
            if (rhs_pin_handle && lhs_pin_handle) {
                erhe::graph::Pin*  lhs_pin     = lhs_pin_handle.AsPointer<erhe::graph::Pin>();
                erhe::graph::Pin*  rhs_pin     = rhs_pin_handle.AsPointer<erhe::graph::Pin>();
                erhe::graph::Pin*  source_pin  = lhs_pin->is_source() ? lhs_pin : rhs_pin->is_source() ? rhs_pin : nullptr;
                erhe::graph::Pin*  sink_pin    = lhs_pin->is_sink  () ? lhs_pin : rhs_pin->is_sink  () ? rhs_pin : nullptr;
                erhe::graph::Node* source_node = (source_pin != nullptr) ? source_pin->get_owner_node() : nullptr;
                erhe::graph::Node* sink_node   = (sink_pin   != nullptr) ? sink_pin  ->get_owner_node() : nullptr;
                if ((source_pin != nullptr) && (sink_pin != nullptr) && (source_pin != sink_pin) && (source_node != sink_node)) {
                    acceptable = true;
                    if (m_node_editor->AcceptNewItem()) { // mouse released?
                        erhe::graph::Link* link = m_graph.connect(source_pin, sink_pin);
                        if (link != nullptr) {
                            m_node_editor->Link(
                                ax::NodeEditor::LinkId{link},
                                ax::NodeEditor::PinId{source_pin},
                                ax::NodeEditor::PinId{sink_pin}
                            );
                        }
                    }
                } 
            }
            if (!acceptable) {
                m_node_editor->RejectNewItem();
            }
        }
    }
    m_node_editor->EndCreate();

    if (m_node_editor->BeginDelete()) {
        ax::NodeEditor::NodeId node_handle = 0;
        while (m_node_editor->QueryDeletedNode(&node_handle)){
            if (m_node_editor->AcceptDeletedItem()) {
                auto i = std::find_if(m_nodes.begin(), m_nodes.end(), [node_handle](const std::shared_ptr<Shader_graph_node>& entry){
                    ax::NodeEditor::NodeId entry_node_id = ax::NodeEditor::NodeId{entry->get_id()};
                    return entry_node_id == node_handle;
                });
                if (i != m_nodes.end()) {
                    const std::shared_ptr<Shader_graph_node>& shader_node = *i;
                    if (shader_node->is_selected()) {
                        m_context.selection->remove_from_selection(shader_node);
                    }
                    m_graph.unregister_node(i->get());
                    m_nodes.erase(i);
                }
            }
        }

        ax::NodeEditor::LinkId link_handle;
        while (m_node_editor->QueryDeletedLink(&link_handle)) {
            if (m_node_editor->AcceptDeletedItem()) {
                std::vector<std::unique_ptr<erhe::graph::Link>>& links = m_graph.get_links();
                auto i = std::find_if(links.begin(), links.end(), [link_handle](const std::unique_ptr<erhe::graph::Link>& entry){
                    ax::NodeEditor::LinkId entry_link_id = ax::NodeEditor::LinkId{entry.get()};
                    return entry_link_id == link_handle;
                });
                if (i != links.end()) {
                    m_graph.disconnect(i->get());
                }
            }
        }
    }
    m_node_editor->EndDelete();
    m_node_editor->End();
}

} // namespace editor
