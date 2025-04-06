#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include "tinyexpr/tinyexpr.h"

#include <array>
#include <string>

namespace erhe::commands {
    class Commands;
}
namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor {

class Editor_context;
class Editor_message;
class Editor_message_bus;

class Sheet
{
public:
    Sheet();

    void set_expression(int row, int col, std::string_view value);
    auto get_expression(int row, int col) -> std::string&;

    void set_value(int row, int column, double value);
    auto get_value(int row, int column) const -> double;

    void evaluate_expressions();

private:
    std::array<std::string, 64> m_data;
    std::array<char[3], 64>     m_te_labels;
    std::array<double, 64>      m_te_values;
    std::array<te_variable, 64> m_te_variables;
    std::array<te_expr*, 64>    m_te_expressions;
};

class Sheet_window : public erhe::imgui::Imgui_window
{
public:
    Sheet_window(
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context,
        Editor_message_bus&          editor_message_bus
    );

    // Implements Imgui_window
    void imgui() override;

    auto get_sheet() -> Sheet*;

private:
    void on_message(Editor_message& message);

    Editor_context& m_context;
    bool            m_show_expression{false};
    Sheet           m_sheet;
};

} // namespace editor
