#include "graph/node_properties.hpp"
#include "graph/shader_graph_node.hpp"

#include "app_context.hpp"
#include "geometry_graph/geometry_graph_window.hpp"
#include "graph_editor/graph_editor_node.hpp"
#include "items.hpp"
#include "texture_graph/texture_graph_window.hpp"
#include "tools/selection_tool.hpp"
#include "windows/editor_windows.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/node.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_imgui/imgui_windows.hpp"

#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>

namespace editor {

Node_properties_window::Node_properties_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context
)
    : Imgui_window{imgui_renderer, imgui_windows, "Node Properties", "node_pproperties", true}
    , m_context   {app_context}
{
}

void Node_properties_window::on_begin()
{
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 0.0f);
}

void Node_properties_window::on_end()
{
    ImGui::PopStyleVar();
}

void Node_properties_window::item_flags(const std::shared_ptr<erhe::Item_base>& item)
{
    ERHE_PROFILE_FUNCTION();

    m_property_editor.push_group("Flags", ImGuiTreeNodeFlags_None, 0.0f);

    using namespace erhe::utility;
    using Item_flags = erhe::Item_flags;

    const uint64_t flags = item->get_flag_bits();
    for (uint64_t bit_position = 0; bit_position < Item_flags::count; ++ bit_position) {
        m_property_editor.add_entry(Item_flags::c_bit_labels[bit_position], [item, bit_position, flags, this]() {
            const uint64_t bit_mask = uint64_t{1} << bit_position;
            bool           value    = test_bit_set(flags, bit_mask);
            if (ImGui::Checkbox("##Node_properties_window::item_flags(", &value)) {
                if (bit_mask == Item_flags::selected) {
                    if (value) {
                        m_context.selection->add_to_selection(item);
                    } else {
                        m_context.selection->remove_from_selection(item);
                    }
                } else {
                    item->set_flag_bits(bit_mask, value);
                }
            }
        });
    }

    m_property_editor.pop_group();
}

void Node_properties_window::item_properties(const std::shared_ptr<erhe::Item_base>& item_in)
{
    ERHE_PROFILE_FUNCTION();

    const auto& content_library_node = std::dynamic_pointer_cast<Content_library_node   >(item_in);
    const auto& item                 = (content_library_node && content_library_node->item) ? content_library_node->item : item_in;

    if (!item) {
        return;
    }

    // Property_editor::push_group / add_entry take std::string&& (each stores a
    // std::string), so build the labels as std::string directly. The previous
    // fmt::memory_buffer members were never null-terminated - passing .data() to
    // the std::string&& parameters ran strlen off the end of the buffer
    // (heap-buffer-overflow) - and were never cleared, so they accumulated text
    // every frame and across every selected item. Passing the string_views to
    // fmt (not their .data()) also avoids a strlen on a non-terminated view.
    m_property_editor.push_group(fmt::format("{} {}", item->get_type_name(), item->get_name()), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed, 0.0f);
    {
        m_property_editor.add_entry(fmt::format("{} Name", item->get_type_name()), [item]() {
            std::string name = item->get_name();
            const bool enter_pressed = ImGui::InputText("##Node_properties_window::item_properties()", &name, ImGuiInputTextFlags_EnterReturnsTrue);
            if (enter_pressed || ImGui::IsItemDeactivatedAfterEdit()) { // TODO
                if (name != item->get_name()) {
                    item->set_name(name);
                }
            }
        });

        m_property_editor.add_entry("Id", [item]() { ImGui::Text("%u", static_cast<unsigned int>(item->get_id())); });

        item_flags(item);
    }

    //if (texture) {
    //    texture_properties(texture);
    // }

    m_property_editor.pop_group();
}

void Node_properties_window::node_properties(Shader_graph_node& node)
{
    m_property_editor.push_group("Node", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed, 0.0f);
    m_property_editor.add_entry(
        "Inputs",
        [&node]() {
            if (ImGui::BeginCombo("##inputs", get_node_edge_name(node.m_input_pin_edge))) {
                for (int i = 0; i < Node_edge::count; ++i) {
                    bool selected = (node.m_input_pin_edge == i);
                    ImGui::Selectable(get_node_edge_name(i), &selected, ImGuiSelectableFlags_None);
                    if (selected) {
                        node.m_input_pin_edge = i;
                    }
                }
                ImGui::EndCombo();
            }
        }
    );
    m_property_editor.add_entry(
        "Outputs",
        [&node]() {
            if (ImGui::BeginCombo("##outputs", get_node_edge_name(node.m_output_pin_edge))) {
                for (int i = 0; i < Node_edge::count; ++i) {
                    bool selected = (node.m_output_pin_edge == i);
                    ImGui::Selectable(get_node_edge_name(i), &selected, ImGuiSelectableFlags_None);
                    if (selected) {
                        node.m_output_pin_edge = i;
                    }
                }
                ImGui::EndCombo();
            }
        }
    );
    m_property_editor.add_entry(
        "Size",
        [&node]() {
            // Requested node size in canvas units; 0 = automatic
            // (content-derived). Content is not scaled.
            float size[2] = {node.get_ui_width(), node.get_ui_height()};
            const float auto_button_width = ImGui::CalcTextSize("Auto").x + (2.0f * ImGui::GetStyle().FramePadding.x);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - auto_button_width - ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::DragFloat2("##size", size, 1.0f, 0.0f, 4096.0f, "%.0f")) {
                node.set_ui_size(size[0], size[1]);
            }
            ImGui::SameLine();
            const bool is_automatic = (node.get_ui_width() <= 0.0f) && (node.get_ui_height() <= 0.0f);
            ImGui::BeginDisabled(is_automatic);
            if (ImGui::Button("Auto")) {
                node.set_ui_size(0.0f, 0.0f);
            }
            ImGui::EndDisabled();
        }
    );
    m_property_editor.pop_group();
}

void Node_properties_window::collect_graph_editor_selection()
{
    m_graph_editor_selection.clear();
    m_graph_editor_link_selection.clear();

    const auto collect_from = [this](Graph_editor_window_base* window) {
        if (window == nullptr) {
            return;
        }
        m_selected_nodes_scratch.clear();
        window->collect_selected_nodes(m_selected_nodes_scratch);
        for (const std::shared_ptr<Graph_editor_node>& node : m_selected_nodes_scratch) {
            // Two windows can edit (and select in) the same graph; show each
            // node once, from the first window that has it selected.
            const bool already_present = std::any_of(
                m_graph_editor_selection.begin(),
                m_graph_editor_selection.end(),
                [&node](const std::pair<Graph_editor_window_base*, std::shared_ptr<Graph_editor_node>>& entry) {
                    return entry.second == node;
                }
            );
            if (!already_present) {
                m_graph_editor_selection.emplace_back(window, node);
            }
        }
        // Links are kept per window (no dedup): curve routing is canvas
        // state, so each window's entry edits that window's own curve.
        m_selected_links_scratch.clear();
        window->collect_selected_links(m_selected_links_scratch);
        for (erhe::graph::Link* link : m_selected_links_scratch) {
            m_graph_editor_link_selection.emplace_back(window, link);
        }
    };

    collect_from(m_context.geometry_graph_window);
    collect_from(m_context.texture_graph_window);
    if (m_context.editor_windows != nullptr) {
        for (const std::shared_ptr<Geometry_graph_window>& window : m_context.editor_windows->get_extra_geometry_graph_windows()) {
            collect_from(window.get());
        }
        for (const std::shared_ptr<Texture_graph_window>& window : m_context.editor_windows->get_extra_texture_graph_windows()) {
            collect_from(window.get());
        }
    }
}

void Node_properties_window::graph_editor_node_properties(Graph_editor_window_base& window, const std::shared_ptr<Graph_editor_node>& node)
{
    m_property_editor.push_group(fmt::format("Node {}", node->get_name()), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed, 0.0f);

    m_property_editor.add_entry("Name", [node]() {
        std::string name = node->get_name();
        const bool enter_pressed = ImGui::InputText("##name", &name, ImGuiInputTextFlags_EnterReturnsTrue);
        if (enter_pressed || ImGui::IsItemDeactivatedAfterEdit()) {
            if (name != node->get_name()) {
                node->set_name(name);
            }
        }
    });
    m_property_editor.add_entry("Type",   [node]() { ImGui::TextUnformatted(node->get_factory_type_name().c_str()); });
    m_property_editor.add_entry("Id",     [node]() { ImGui::Text("%u", static_cast<unsigned int>(node->get_id())); });
    m_property_editor.add_entry("Window", [&window]() { ImGui::TextUnformatted(window.get_title().c_str()); });
    m_property_editor.add_entry("Position", [&window, node]() {
        const ImVec2 position = window.get_node_position(*node.get());
        float xy[2] = {position.x, position.y};
        if (ImGui::DragFloat2("##position", xy, 1.0f)) {
            window.set_node_position(*node.get(), ImVec2{xy[0], xy[1]});
        }
    });
    m_property_editor.add_entry("Size", [&window, node]() {
        // Requested node size in canvas units; automatic (content-derived)
        // when unset, in which case the drags start from the actual on-canvas
        // extent. Content is not scaled - the node just gets more room (a
        // wider center column, empty space below the content).
        const ImVec2 actual_size = window.get_node_size(*node.get());
        float size[2] = {
            (node->get_ui_width()  > 0.0f) ? node->get_ui_width()  : actual_size.x,
            (node->get_ui_height() > 0.0f) ? node->get_ui_height() : actual_size.y
        };
        const float auto_button_width = ImGui::CalcTextSize("Auto").x + (2.0f * ImGui::GetStyle().FramePadding.x);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - auto_button_width - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::DragFloat2("##size", size, 1.0f, 0.0f, 4096.0f, "%.0f")) {
            node->set_ui_size(size[0], size[1]);
        }
        ImGui::SameLine();
        const bool is_automatic = (node->get_ui_width() <= 0.0f) && (node->get_ui_height() <= 0.0f);
        ImGui::BeginDisabled(is_automatic);
        if (ImGui::Button("Auto")) {
            node->set_ui_size(0.0f, 0.0f);
        }
        ImGui::EndDisabled();
    });
    // Pin layout: only left / right are implemented by the node renderer, so
    // the combos offer just those two edges (unlike the shader graph's).
    m_property_editor.add_entry("Inputs", [node]() {
        if (ImGui::BeginCombo("##inputs", get_node_edge_name(node->get_input_pin_edge()))) {
            for (int edge : {Node_edge::left, Node_edge::right}) {
                bool selected = (node->get_input_pin_edge() == edge);
                ImGui::Selectable(get_node_edge_name(edge), &selected, ImGuiSelectableFlags_None);
                if (selected) {
                    node->set_input_pin_edge(edge);
                }
            }
            ImGui::EndCombo();
        }
    });
    m_property_editor.add_entry("Outputs", [node]() {
        if (ImGui::BeginCombo("##outputs", get_node_edge_name(node->get_output_pin_edge()))) {
            for (int edge : {Node_edge::left, Node_edge::right}) {
                bool selected = (node->get_output_pin_edge() == edge);
                ImGui::Selectable(get_node_edge_name(edge), &selected, ImGuiSelectableFlags_None);
                if (selected) {
                    node->set_output_pin_edge(edge);
                }
            }
            ImGui::EndCombo();
        }
    });
    m_property_editor.add_entry("Parameters", [this, node]() {
        node->properties_imgui(m_context);
    });

    m_property_editor.pop_group();
}

void Node_properties_window::graph_editor_link_properties(Graph_editor_window_base& window, erhe::graph::Link* link)
{
    m_property_editor.push_group(
        fmt::format(
            "Link {} -> {}",
            link->get_source()->get_owner_node()->get_name(),
            link->get_sink  ()->get_owner_node()->get_name()
        ),
        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed,
        0.0f
    );

    m_property_editor.add_entry("Source", [link]() {
        const std::string label = fmt::format("{}.{}", link->get_source()->get_owner_node()->get_name(), link->get_source()->get_name());
        ImGui::TextUnformatted(label.c_str());
    });
    m_property_editor.add_entry("Sink", [link]() {
        const std::string label = fmt::format("{}.{}", link->get_sink()->get_owner_node()->get_name(), link->get_sink()->get_name());
        ImGui::TextUnformatted(label.c_str());
    });
    m_property_editor.add_entry("Window", [&window]() { ImGui::TextUnformatted(window.get_title().c_str()); });

    // Per-link curve shape (Kochanek-Bartels): the values live in the
    // window's canvas (ax::NodeEditor link), like the routing mid points.
    const ax::NodeEditor::LinkId link_id{link};
    const auto curve_slider = [&window, link_id](const char* imgui_id, int component) {
        ax::NodeEditor::EditorContext* node_editor = window.get_node_editor();
        float params[3] = {0.0f, 0.0f, 0.0f};
        node_editor->GetLinkCurveParams(link_id, &params[0], &params[1], &params[2]);
        if (ImGui::SliderFloat(imgui_id, &params[component], -1.0f, 1.0f)) {
            node_editor->SetLinkCurveParams(link_id, params[0], params[1], params[2]);
        }
    };
    m_property_editor.add_entry("Tension",    [curve_slider]() { curve_slider("##tension",    0); });
    m_property_editor.add_entry("Continuity", [curve_slider]() { curve_slider("##continuity", 1); });
    m_property_editor.add_entry("Bias",       [curve_slider]() { curve_slider("##bias",       2); });
    m_property_editor.add_entry("Mid points", [&window, link_id]() {
        ax::NodeEditor::EditorContext* node_editor = window.get_node_editor();
        ImGui::Text("%d", node_editor->GetLinkMidPointCount(link_id));
        ImGui::SameLine();
        if (ImGui::Button("Reset curve")) {
            node_editor->SetLinkMidPoints(link_id, nullptr, 0);
            node_editor->SetLinkCurveParams(link_id, 0.0f, 0.0f, 0.0f);
        }
    });

    // Pen-tool tangent handles per mid point (mode + explicit tangent
    // offsets; also editable on the canvas by dragging the tangent dots of
    // the selected link). Switching Auto -> manual captures the effective
    // computed tangents so the curve does not jump; switching to Mirrored
    // locks in = -out.
    static const char* const c_mid_point_mode_names[4] = { "Auto", "Mirrored", "Aligned", "Free" };
    const int mid_point_count = window.get_node_editor()->GetLinkMidPointCount(link_id);
    for (int i = 0; i < mid_point_count; ++i) {
        m_property_editor.add_entry(fmt::format("Point {}", i), [&window, link_id, i]() {
            ax::NodeEditor::EditorContext* node_editor = window.get_node_editor();
            const int mode = node_editor->GetLinkMidPointMode(link_id, i);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::BeginCombo("##mode", c_mid_point_mode_names[std::clamp(mode, 0, 3)])) {
                for (int new_mode = 0; new_mode < 4; ++new_mode) {
                    bool selected = (new_mode == mode);
                    ImGui::Selectable(c_mid_point_mode_names[new_mode], &selected, ImGuiSelectableFlags_None);
                    if (selected && (new_mode != mode)) {
                        ImVec2 tan_in {0.0f, 0.0f};
                        ImVec2 tan_out{0.0f, 0.0f};
                        node_editor->GetLinkMidPointTangents(link_id, i, &tan_in, &tan_out);
                        if (new_mode == 1) {
                            tan_in = ImVec2{-tan_out.x, -tan_out.y};
                        }
                        node_editor->SetLinkMidPointTangents(link_id, i, new_mode, tan_in, tan_out);
                    }
                }
                ImGui::EndCombo();
            }
            if (mode != 0) {
                ImVec2 tan_in {0.0f, 0.0f};
                ImVec2 tan_out{0.0f, 0.0f};
                node_editor->GetLinkMidPointTangents(link_id, i, &tan_in, &tan_out);
                float out_xy[2] = {tan_out.x, tan_out.y};
                float in_xy [2] = {tan_in.x,  tan_in.y};
                bool  changed_out = false;
                bool  changed_in  = false;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                changed_out = ImGui::DragFloat2("##out", out_xy, 1.0f, 0.0f, 0.0f, "out %.0f");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                changed_in  = ImGui::DragFloat2("##in",  in_xy,  1.0f, 0.0f, 0.0f, "in %.0f");
                if (changed_out || changed_in) {
                    tan_out = ImVec2{out_xy[0], out_xy[1]};
                    tan_in  = ImVec2{in_xy [0], in_xy [1]};
                    if (mode == 1) { // Mirrored: the edited side is the master
                        if (changed_out) {
                            tan_in = ImVec2{-tan_out.x, -tan_out.y};
                        } else {
                            tan_out = ImVec2{-tan_in.x, -tan_in.y};
                        }
                    }
                    node_editor->SetLinkMidPointTangents(link_id, i, mode, tan_in, tan_out);
                }
            }
        });
    }

    m_property_editor.pop_group();
}

void Node_properties_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    m_property_editor.reset();

    const auto& selected_items = m_context.selection->get_selected_items();
    int id = 0;
    for (const auto& item : selected_items) {
        ImGui::PushID(id++);
        ERHE_DEFER( ImGui::PopID(); );
        ERHE_VERIFY(item);
        item_properties(item);
    }

    const auto selected_graph_node = get<erhe::graph::Node>(selected_items);
    if (selected_graph_node) {
        Shader_graph_node* shader_graph_node = dynamic_cast<Shader_graph_node*>(selected_graph_node.get());
        if (shader_graph_node != nullptr) {
            node_properties(*shader_graph_node);
        }
    }

    // Geometry / texture graph nodes selected on any graph editor canvas
    // (their selection is not in the global selection; see issue #252).
    collect_graph_editor_selection();
    for (const std::pair<Graph_editor_window_base*, std::shared_ptr<Graph_editor_node>>& entry : m_graph_editor_selection) {
        ImGui::PushID(id++);
        ERHE_DEFER( ImGui::PopID(); );
        graph_editor_node_properties(*entry.first, entry.second);
    }
    for (const std::pair<Graph_editor_window_base*, erhe::graph::Link*>& entry : m_graph_editor_link_selection) {
        ImGui::PushID(id++);
        ERHE_DEFER( ImGui::PopID(); );
        graph_editor_link_properties(*entry.first, entry.second);
    }

    m_property_editor.show_entries();
}

}
