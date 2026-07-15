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
            float ui_scale = node.get_ui_scale();
            if (ImGui::SliderFloat("##size", &ui_scale, 0.25f, 4.0f, "%.2f")) {
                node.set_ui_scale(ui_scale);
            }
        }
    );
    m_property_editor.pop_group();
}

void Node_properties_window::collect_graph_editor_selection()
{
    m_graph_editor_selection.clear();

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
        // The on-canvas extent is content-derived; the scale below adjusts it.
        const ImVec2 size = window.get_node_size(*node.get());
        float ui_scale = node->get_ui_scale();
        ImGui::Text("%.0f x %.0f", size.x, size.y);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderFloat("##size_scale", &ui_scale, 0.25f, 4.0f, "x %.2f")) {
            node->set_ui_scale(ui_scale);
        }
    });
    m_property_editor.add_entry("Parameters", [this, node]() {
        node->properties_imgui(m_context);
    });

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

    m_property_editor.show_entries();
}

}
