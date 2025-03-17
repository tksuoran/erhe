#include "windows/property_editor.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

void Property_editor::reset()
{
    m_entries.clear();
    m_row = 0;
}

void Property_editor::resume()
{
    m_entries.clear();
}

void Property_editor::push_group(std::string_view label, ImGuiTreeNodeFlags flags, float indent, bool* open_state)
{
    m_entries.push_back(Entry{true, false, std::string{label}, {}, flags, indent, {}, {}, open_state});
}

void Property_editor::pop_group()
{
    m_entries.push_back(Entry{false, true, {}, {}});
}

void Property_editor::add_entry(std::string_view label, std::function<void()> editor)
{
    m_entries.push_back(Entry{false, false, std::string{label}, {editor}});
}

void Property_editor::add_entry(std::string_view label, uint32_t label_text_color, uint32_t label_background_color, std::function<void()> editor)
{
    m_entries.push_back(Entry{false, false, std::string{label}, {editor}, ImGuiTreeNodeFlags_None, 0.0f, label_text_color, label_background_color});
}

void Property_editor::show_entries(const char* label, ImVec2 cell_padding)
{
    ERHE_PROFILE_FUNCTION();

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cell_padding);

    bool table_visible = ImGui::BeginTable(label, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable);
    if (!table_visible) {
        return;
    }

    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("editor", ImGuiTableColumnFlags_WidthStretch, 1.0f);

    struct Stack_entry
    {
        bool subtree_open;
        float indent_amount;
    };
    std::vector<Stack_entry> stack;
    for (const Entry& entry : m_entries) {
        ImGui::PushID(m_row++);
        bool currently_open = stack.empty() || stack.back().subtree_open;
        float indent_amount = stack.empty() ? 0.0f : stack.back().indent_amount;
        if (entry.push_group) {
            if (currently_open) {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{4.0f, 4.0f});
                ImGui::TableNextRow(ImGuiTableRowFlags_None);
                ImGui::TableSetColumnIndex(0);
                const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_LabelSpanAllColumns | entry.flags;
                const bool subtree_open = ImGui::TreeNodeEx(entry.label.c_str(), flags);
                if (entry.open_state != nullptr) {
                    *entry.open_state = subtree_open;
                }
                ImGui::PopStyleVar(1);
                stack.emplace_back(subtree_open, entry.indent);
                if (entry.indent != 0.0f) {
                    ImGui::Indent(entry.indent);
                }
            } else {
                stack.emplace_back(false, 0.0f);
            }
        } else if (entry.pop_group) {
            if (indent_amount != 0.0f) {
                ImGui::Unindent(indent_amount);
            }
            if (currently_open) {
                ImGui::TreePop();
            }
            stack.pop_back();
        } else if (currently_open) {
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
            if (entry.label_text_color.has_value() || entry.label_background_color.has_value()) {
                ImGui::PushItemFlag    (ImGuiItemFlags_NoNav, true);
                //ImGui::SetNextItemWidth(22.0f); // TODO
                ImVec2 button_size{22.0f, 0.0f};
                ImGui::Button          (entry.label.c_str(), button_size);
                ImGui::PopItemFlag     ();
            } else {
                ImGui::TextUnformatted(entry.label.c_str());
            }
            if (entry.label_text_color.has_value()) {
                ImGui::PopStyleColor(1);
            }
            if (entry.label_background_color.has_value()) {
                ImGui::PopStyleColor(3);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN); 
            entry.editor();
        }
        ImGui::PopID();
    }
    ERHE_VERIFY(stack.empty());

    ImGui::EndTable();
    ImGui::PopStyleVar(1);
}

} // namespace editor {
