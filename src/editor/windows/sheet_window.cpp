#include "windows/sheet_window.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace editor {

Sheet::Sheet()
{
    // Initialize tinyexpr variables
    const char col_char[8] = {'A','B','C','D','E','F','G','H'};
    const char row_char[8] = {'1','2','3','4','5','6','7','8'};
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            char* label = m_te_labels.at(row * 8 + col);
            label[0] = col_char[col];
            label[1] = row_char[row];
            label[2] = '\0';
            double& te_value = m_te_values.at(row * 8 + col);
            te_value = 0.0;
            te_variable& te_variable = m_te_variables.at(row * 8 + col);
            te_variable.name = label;
            te_variable.address = &te_value;
            te_variable.type = TE_VARIABLE;
            te_variable.context = nullptr;
            m_te_expressions.at(row * 8 + col) = nullptr;
        }
    }
}

void Sheet::set_expression(int row, int col, std::string_view value)
{
    std::string& cell_string = m_data.at(row * 8 + col);
    cell_string = value;
    te_expr* te_expression = m_te_expressions.at(row * 8 + col);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (te_expression != nullptr) {
        te_free(te_expression);
    }
    if (value.empty()) {
        m_te_expressions.at(row * 8 + col) = nullptr;
        return;
    }
    int error = 0;
    m_te_expressions.at(row * 8 + col) = te_compile(cell_string.c_str(), m_te_variables.data(), static_cast<int>(m_te_variables.size()), &error);
}

auto Sheet::get_expression(int row, int col) -> std::string&
{
    return m_data.at(row * 8 + col);
}

void Sheet::set_value(int row, int column, double value)
{
    std::string value_string = fmt::format("{}", value);
    set_expression(row, column, value_string);
}

auto Sheet::get_value(int row, int column) const -> double
{
    const double& te_value = m_te_values.at(row * 8 + column);
    return te_value;
}

void Sheet::evaluate_expressions()
{
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            double& te_value = m_te_values.at(row * 8 + col);
            te_expr*& te_expression = m_te_expressions.at(row * 8 + col);
            if (te_expression != nullptr) {
                te_value = te_eval(te_expression);
            }
        }
    }
}

Sheet_window::Sheet_window(
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context,
    Editor_message_bus&          editor_message_bus
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Sheet", "sheet"}
    , m_context                {editor_context}
{
    static_cast<void>(commands); // TODO Keeping in case we need to add commands here

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            on_message(message);
        }
    );
}

void Sheet_window::on_message(Editor_message&)
{
    //// using namespace erhe::bit;
    //// if (test_any_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_selection)) {
    //// }
}

auto Sheet_window::get_sheet() -> Sheet*
{
    return &m_sheet;
}

void Sheet_window::imgui()
{
    bool dirty = false;

    if (ImGui::Button("Compute")) {
        dirty = true;
    }

    ImGui::SameLine();

    if (m_show_expression) {
        if (ImGui::Button("Show Values")) {
            m_show_expression = false;
        }
    } else if (ImGui::Button("Show Expressions")) {
        m_show_expression = true;
    }

    ImGui::BeginTable("sheet", 9, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable);
    ImGui::TableSetupColumn("##rows", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("A", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("B", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("C", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("D", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("E", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("F", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("G", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("H", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableHeadersRow();
    for (int row = 0; row < 8; ++row) {
        ImGui::PushID(row);
        ERHE_DEFER( ImGui::PopID(); );
        ImGui::TableNextRow(ImGuiTableRowFlags_None);
        ImGui::TableNextColumn();
        ImGui::Text("%d", row + 1);
        for (int col = 0; col < 8; ++col) {
            ImGui::PushID(col);
            ERHE_DEFER( ImGui::PopID(); );
            ImGui::TableNextColumn();
            std::string& cell_string = m_sheet.get_expression(row, col);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (m_show_expression) {
                ImGui::InputText("##", &cell_string);
                if (ImGui::IsItemEdited()) {
                    dirty = true;
                    m_sheet.set_expression(row, col, cell_string);
                }
            } else {
                if (!cell_string.empty()) { //te_expression != nullptr) {
                    const double value = m_sheet.get_value(row, col);
                    std::string value_string = fmt::format("{}", value);
                    ImGui::InputText("##", &value_string, ImGuiInputTextFlags_ReadOnly);
                } else {
                    std::string value_string{};
                    ImGui::InputText("##", &value_string, ImGuiInputTextFlags_ReadOnly);
                }
            }
        }
    }
    ImGui::EndTable();

    if (dirty) {
        m_sheet.evaluate_expressions();
        dirty = false;
    }
}

} // namespace editor
