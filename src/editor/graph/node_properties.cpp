#include "graph/node_properties.hpp"
#include "graph/shader_graph_node.hpp"

#include "editor_context.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_bit/bit_helpers.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

namespace editor {

Node_properties_window::Node_properties_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context
)
    : Imgui_window{imgui_renderer, imgui_windows, "Node Properties", "node_pproperties"}
    , m_context   {editor_context}
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

    using namespace erhe::bit;
    using Item_flags = erhe::Item_flags;

    const uint64_t flags = item->get_flag_bits();
    for (uint64_t bit_position = 0; bit_position < Item_flags::count; ++ bit_position) {
        m_property_editor.add_entry(Item_flags::c_bit_labels[bit_position], [item, bit_position, flags, this]() {
            const uint64_t bit_mask = uint64_t{1} << bit_position;
            bool           value    = test_all_rhs_bits_set(flags, bit_mask);
            if (ImGui::Checkbox("##", &value)) {
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

    std::string group_label = fmt::format("{} {}", item->get_type_name().data(), item->get_name());
    m_property_editor.push_group(group_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed, 0.0f);
    {
        std::string label_name = fmt::format("{} Name", item->get_type_name());
        m_property_editor.add_entry(label_name, [item]() {
            std::string name = item->get_name();
            const bool enter_pressed = ImGui::InputText("##", &name, ImGuiInputTextFlags_EnterReturnsTrue);
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
        "Flow Direction",
        [&node]() {
            if (ImGui::BeginCombo("Flow Direction", get_flow_direction_name(node.m_flow_direction))) {
                for (int i = 0; i < Flow_direction::count; ++i) {
                    bool selected = (node.m_flow_direction == i);
                    ImGui::Selectable(get_flow_direction_name(i), &selected, ImGuiSelectableFlags_None);
                    if (selected) {
                        node.m_flow_direction = i;
                    }
                }
                ImGui::EndCombo();
            }
        }
    );
    m_property_editor.pop_group();
}

void Node_properties_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    m_property_editor.reset();

    const auto& selection = m_context.selection->get_selection();
    int id = 0;
    for (const auto& item : selection) {
        ImGui::PushID(id++);
        ERHE_DEFER( ImGui::PopID(); );
        ERHE_VERIFY(item);
        item_properties(item);
    }

    const auto selected_graph_node = m_context.selection->get<erhe::graph::Node>();
    if (selected_graph_node) {
        Shader_graph_node* shader_graph_node = dynamic_cast<Shader_graph_node*>(selected_graph_node.get());
        if (shader_graph_node != nullptr) {
            node_properties(*shader_graph_node);
        }
    }

    m_property_editor.show_entries();
}

} // namespace editor
