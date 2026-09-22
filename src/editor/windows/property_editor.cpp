#include "windows/property_editor.hpp"
#include "windows/property_group_states.hpp"

#include "erhe_imgui/imgui_item_recorder.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

void Property_editor::reset_row()
{
    m_row = 0;
}

void Property_editor::reset()
{
    m_entries.clear();
    m_row = 0;
}

void Property_editor::resume()
{
    m_entries.clear();
}

void Property_editor::push_group(std::string&& label, ImGuiTreeNodeFlags flags, float indent, bool* open_state)
{
    m_entries.push_back(Entry{true, false, std::move(label), {}, {}, {}, flags, indent, {}, {}, open_state});
}

void Property_editor::push_group(std::string&& label, const float indent, Property_group_states& states)
{
    m_entries.push_back(Entry{true, false, std::move(label), {}, {}, {}, ImGuiTreeNodeFlags_None, indent, {}, {}, nullptr, &states});
}

void Property_editor::pop_group()
{
    m_entries.push_back(Entry{false, true, {}, {}, {}});
}

void Property_editor::add_entry(std::string&& label, std::function<void()> editor, std::string&& tooltip, std::optional<uint32_t> label_text_color)
{
    m_entries.push_back(Entry{false, false, std::move(label), std::move(tooltip), {}, {editor}, ImGuiTreeNodeFlags_None, 0.0f, label_text_color});
}

void Property_editor::add_entry(std::string&& label, uint32_t label_text_color, uint32_t label_background_color, std::function<void()> editor)
{
    m_entries.push_back(Entry{false, false, std::move(label), {}, {}, {editor}, ImGuiTreeNodeFlags_None, 0.0f, label_text_color, label_background_color});
}

void Property_editor::enable_filter()
{
    m_filter_enabled = true;
}

void Property_editor::show_filter_row()
{
    m_filter.Draw("Filter");
}

void Property_editor::update_entry_visibility()
{
    m_entry_visible.clear();
    m_entry_visible.resize(m_entries.size(), static_cast<uint8_t>(1));
    if (!m_filter_enabled || !m_filter.IsActive()) {
        return;
    }

    m_filter_stack.clear();
    for (std::size_t index = 0, end = m_entries.size(); index < end; ++index) {
        const Entry& entry = m_entries[index];
        const bool   inside_match = !m_filter_stack.empty() && m_filter_stack.back().inside_match;
        if (entry.pop_group) {
            if (!m_filter_stack.empty()) {
                m_filter_stack.pop_back();
            }
            continue;
        }
        const bool self_match = m_filter.PassFilter(entry.label.c_str());
        const bool visible    = inside_match || self_match;
        m_entry_visible[index] = visible ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0);
        if (self_match && !inside_match) {
            // Show every ancestor of a match, so the matching row can be
            // reached through its groups.
            for (const Filter_group& group : m_filter_stack) {
                m_entry_visible[group.index] = static_cast<uint8_t>(1);
            }
        }
        if (entry.push_group) {
            m_filter_stack.push_back(Filter_group{index, visible});
        }
    }
    m_filter_stack.clear(); // entries hold no reference past this point; capacity kept
}

namespace {

constexpr const char* c_property_group_payload = "erhe_property_group";

} // anonymous namespace

// The header item just submitted is the drag source and the drop target of
// a shared property group. The payload is the group name; the drop slot is
// before the target when the pointer is in the upper half of its header,
// after it otherwise, drawn as a yellow line on that edge of the header.
void Property_editor::property_group_drag_drop(const Entry& entry)
{
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
        ImGui::SetDragDropPayload(c_property_group_payload, entry.label.c_str(), entry.label.size() + 1);
        ImGui::TextUnformatted(entry.label.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* const payload = ImGui::AcceptDragDropPayload(
            c_property_group_payload,
            ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect
        );
        if (payload != nullptr) {
            const std::string_view source{static_cast<const char*>(payload->Data)};
            const ImVec2 rect_min = ImGui::GetItemRectMin();
            const ImVec2 rect_max = ImGui::GetItemRectMax();
            const bool   after    = ImGui::GetMousePos().y > (0.5f * (rect_min.y + rect_max.y));
            const float  y        = after ? rect_max.y : rect_min.y;
            if (source != entry.label) {
                ImGui::GetWindowDrawList()->AddLine(ImVec2{rect_min.x, y}, ImVec2{rect_max.x, y}, IM_COL32(255, 220, 0, 255), 2.0f);
            }
            if (payload->IsDelivery()) {
                entry.group_states->move(source, entry.label, after ? Group_drop_side::after : Group_drop_side::before);
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void Property_editor::show_entries(const char* label, ImVec2 cell_padding)
{
    ERHE_PROFILE_FUNCTION();

    if (m_filter_enabled) {
        show_filter_row();
    }
    update_entry_visibility();
    const bool filtering = m_filter_enabled && m_filter.IsActive();

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cell_padding);

    bool table_visible = ImGui::BeginTable(label, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable);
    if (!table_visible) {
        ImGui::PopStyleVar(1);
        m_entries.clear();
        return;
    }

    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("editor", ImGuiTableColumnFlags_WidthStretch, 1.0f);

    if (filtering) {
        // Filtered rows are drawn in their own ID scope. Force-opening a group
        // (below) writes the open state into ImGui's storage, so without a
        // separate scope a filter would overwrite - and on clearing leave
        // behind - the fold state the user had set. With the scope, the
        // unfiltered rows keep their own storage entries untouched.
        ImGui::PushID("##filtered");
    }

    for (std::size_t entry_index = 0, entry_end = m_entries.size(); entry_index < entry_end; ++entry_index) {
        const Entry& entry = m_entries[entry_index];
        ImGui::PushID(m_row++);
        const bool filtered_in = m_entry_visible[entry_index] != 0;
        bool currently_open = m_stack.empty() || m_stack.back().subtree_open;
        float indent_amount = m_stack.empty() ? 0.0f : m_stack.back().indent_amount;
        if (entry.push_group) {
            if (currently_open && filtered_in) {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{4.0f, 4.0f});
                ImGui::TableNextRow(ImGuiTableRowFlags_None);
                ImGui::TableSetColumnIndex(0);
                // A shared property group is as open as its persisted state
                // says, in every window; while a group drag is in flight
                // ImGui's own storage rules so a hold does not fight it.
                const bool group_drag_active = (entry.group_states != nullptr) && (ImGui::GetDragDropPayload() != nullptr);
                if ((entry.group_states != nullptr) && !group_drag_active) {
                    ImGui::SetNextItemOpen(entry.group_states->is_open(entry.label), ImGuiCond_Always);
                }
                if (filtering) {
                    // A group is only visible while filtering when it or one of
                    // its descendants matches - open it so the match is reached.
                    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                }
                const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_LabelSpanAllColumns | entry.flags;
                const bool subtree_open = ImGui::TreeNodeEx(entry.label.c_str(), flags);
                if (entry.open_state != nullptr) {
                    *entry.open_state = subtree_open;
                }
                if ((entry.group_states != nullptr) && !group_drag_active && !filtering) {
                    entry.group_states->set_open(entry.label, subtree_open); // the click on the header, if any
                }
                if (entry.group_states != nullptr) {
                    property_group_drag_drop(entry);
                }
                ImGui::PopStyleVar(1);
                m_stack.emplace_back(subtree_open, entry.indent);
                if (entry.indent != 0.0f) {
                    ImGui::Indent(entry.indent);
                }
            } else {
                m_stack.emplace_back(false, 0.0f);
            }
        } else if (entry.pop_group) {
            if (indent_amount != 0.0f) {
                ImGui::Unindent(indent_amount);
            }
            if (currently_open) {
                ImGui::TreePop();
            }
            m_stack.pop_back();
        } else if (currently_open && filtered_in) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None);
            ImGui::TableSetColumnIndex(0);
            if (entry.label_text_color.has_value()) {
                ImGui::PushStyleColor(ImGuiCol_Text, entry.label_text_color.value());
            }
            if (entry.label_background_color.has_value()) {
                ImGui::PushStyleColor(ImGuiCol_Button,        entry.label_background_color.value());
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, entry.label_background_color.value());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  entry.label_background_color.value());
            }
            if (entry.label_background_color.has_value()) {
                ImGui::PushItemFlag    (ImGuiItemFlags_NoNav, true);
                //ImGui::SetNextItemWidth(22.0f); // TODO
                ImVec2 button_size{22.0f, 0.0f};
                ImGui::Button          (entry.label.c_str(), button_size);
                ImGui::PopItemFlag     ();
            } else {
                ImGui::TextUnformatted(entry.label.c_str());
            }
            bool row_hovered = ImGui::IsItemHovered();
            if (entry.label_text_color.has_value()) {
                ImGui::PopStyleColor(1);
            }
            if (entry.label_background_color.has_value()) {
                ImGui::PopStyleColor(3);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            // A row's value widgets are labelled "##..." and the name the row
            // shows is drawn as table text, so without this the row cannot be
            // addressed by name from the get_imgui_* MCP queries
            // (doc/agents/mcp_ui_driving.md). Naming happens only in a frame
            // a query asked to record, so an ordinary frame pays one branch
            // and allocates nothing.
            const bool        naming_items     = erhe::imgui::is_item_recording();
            const std::size_t first_item_index = naming_items ? erhe::imgui::get_recorded_item_count() : 0;
            entry.editor();
            if (naming_items) {
                erhe::imgui::set_recorded_item_labels(first_item_index, entry.label);
            }
            row_hovered = row_hovered || ImGui::IsItemHovered();
            if (m_state != nullptr) {
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    set_dirty_completed();
                } else if ((*m_state == Editor_state::dirty_editing) && ImGui::IsItemDeactivated()) {
                    set_dirty_completed();
                } else if (ImGui::IsItemEdited()) {
                    set_dirty_editing();
                }
            }
            if (row_hovered && (!entry.tooltip.empty() || entry.tooltip_extra)) {
                if (entry.tooltip_extra) {
                    m_tooltip_scratch.clear(); // capacity kept; only a hovered row fills it
                    m_tooltip_scratch += entry.tooltip;
                    m_tooltip_scratch += entry.tooltip_extra();
                    ImGui::SetTooltip("%s", m_tooltip_scratch.c_str());
                } else {
                    ImGui::SetTooltip("%s", entry.tooltip.c_str());
                }
            }
        }
        ImGui::PopID();
    }
    ERHE_VERIFY(m_stack.empty());
    if (filtering) {
        ImGui::PopID();
    }

    ImGui::EndTable();
    ImGui::PopStyleVar(1);

    m_entries.clear();
}

void Property_editor::set_entry_tooltip_extra(std::function<std::string()> provider)
{
    if (m_entries.empty()) {
        return;
    }
    m_entries.back().tooltip_extra = std::move(provider);
}

void Property_editor::use_state(Editor_state* state)
{
    m_state = state;
}

void Property_editor::set_dirty_editing()
{
    if (m_state == nullptr) {
        return;
    }
    if (*m_state == Editor_state::clean) {
        *m_state = Editor_state::dirty_editing;
    }
}

void Property_editor::set_dirty_completed()
{
    if (m_state == nullptr) {
        return;
    }
    *m_state = Editor_state::dirty_completed;
}

}
