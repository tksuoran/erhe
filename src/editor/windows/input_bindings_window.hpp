#pragma once

#include "erhe_commands/binding_desc.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace erhe::commands { class Command; class Commands; }
namespace erhe::imgui    { class Imgui_windows; }

namespace editor {

class App_context;

// Edits the user's input binding overrides (doc/editor/input_bindings.md):
// one row per user-bindable command, grouped by the command name prefix,
// with its bindings, a marker when the user has edited them, conflicts with
// other commands and a per-row reset. An edit calls
// erhe::commands::Commands::set_binding_override() and saves
// input_bindings.json right away.
//
// The rows are derived from Commands and rebuilt only when Commands reports a
// binding change (add_bindings_changed_callback), not per frame.
class Input_bindings_window : public erhe::imgui::Imgui_window
{
public:
    Input_bindings_window(
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context
    );

    // Implements Imgui_window
    void imgui() override;

private:
    class Row
    {
    public:
        erhe::commands::Command*          command{nullptr};
        std::string                       name;
        std::string                       group;
        erhe::commands::Input_kind        input_kind{erhe::commands::Input_kind::internal};
        std::vector<erhe::commands::Binding_desc> bindings;
        std::vector<std::string>          labels;      // display label per binding
        std::vector<std::string>          search_keys; // text form per binding, for the filter
        bool                              modified{false};
        std::string                       conflicts;   // tooltip text, empty when none
    };

    enum class Edit_mode : unsigned int
    {
        none      = 0,
        capturing = 1, // waiting for a key press or mouse click
        editing   = 2
    };

    void rebuild_rows       ();
    void row_imgui          (std::size_t row_index);
    void begin_edit         (std::size_t row_index, std::size_t binding_index);
    void edit_popup_imgui   ();
    void capture_imgui      ();
    void modifiers_imgui    ();
    auto commit             (Row& row, const std::vector<erhe::commands::Binding_desc>& bindings) -> bool;
    void reset              (Row& row);
    void reset_all          ();
    [[nodiscard]] auto row_matches_filter(const Row& row) const -> bool;

    App_context&                 m_context;
    std::vector<Row>             m_rows;
    bool                         m_rows_dirty{true};
    bool                         m_show_modified_only{false};
    std::string                  m_filter;

    // Edit popup state
    Edit_mode                    m_edit_mode{Edit_mode::none};
    bool                         m_open_edit_popup{false};
    std::size_t                  m_edit_row{0};
    std::size_t                  m_edit_binding{0}; // == bindings.size() adds a binding
    erhe::commands::Binding_desc m_edit_desc{};
    std::string                  m_edit_text;       // m_edit_desc.to_string()
    std::string                  m_edit_error;
    std::vector<erhe::commands::Binding_desc> m_edit_bindings; // scratch for commit
};

} // namespace editor
