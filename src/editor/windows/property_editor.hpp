#pragma once

#include <imgui/imgui.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

enum class Editor_state : unsigned int
{
    clean           = 0,
    dirty_editing   = 1,
    dirty_completed = 2
};

class Property_editor
{
public:
    void reset_row          ();
    void reset              ();
    void resume             ();
    void push_group         (std::string&& label, ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None, float indent = 0.0f, bool* open_state = nullptr);
    void pop_group          ();
    // The indent the window gives the rows of a group below the group header.
    [[nodiscard]] auto get_group_indent() const -> float { return m_indent; }
    // label_text_color tints the label text (IM_COL32); a row of a
    // registered property (Dependency_property_rows) is tinted so the
    // rows still hand-written for an item are told apart at a glance.
    void add_entry          (std::string&& label, std::function<void()> editor, std::string&& tooltip = {}, std::optional<uint32_t> label_text_color = {});
    // Axis-style label: a small button in label_background_color.
    void add_entry          (std::string&& label, uint32_t label_text_color, uint32_t label_background_color, std::function<void()> editor);
    // Extra tooltip text for the entry just added, produced only while that
    // row is hovered: the composition origin of a property value
    // (doc/erhe/usd_compatibility_design.md X5) costs an ancestor walk and a few
    // strings, which no frame should pay for every row.
    void set_entry_tooltip_extra(std::function<std::string()> provider);
    // Opt in to the text search filter row drawn at the top of show_entries().
    // Only windows that ask for it get the row; the filter text is owned by
    // this Property_editor, so it survives across frames.
    void enable_filter      ();
    void show_entries       (const char* label = "##", ImVec2 cell_padding = ImVec2{0.0f, 0.0f});
    void use_state          (Editor_state* state);
    void set_dirty_editing  ();
    void set_dirty_completed();

protected:
    class Entry
    {
    public:
        bool                    push_group{false};
        bool                    pop_group{false};
        std::string             label;
        std::string             tooltip;
        std::function<std::string()> tooltip_extra;
        std::function<void()>   editor;
        ImGuiTreeNodeFlags      flags{ImGuiTreeNodeFlags_None};
        float                   indent{0.0f};
        std::optional<uint32_t> label_text_color{};
        std::optional<uint32_t> label_background_color{};
        bool*                   open_state{nullptr};
    };

    // Fills m_entry_visible for the current m_entries: an entry is visible
    // when its own label matches the filter, when an enclosing group's label
    // matches (the whole subtree of a matching group is shown), or - for a
    // group - when any of its descendants matches, so every ancestor of a
    // match is shown.
    void update_entry_visibility();
    void show_filter_row        ();

    class Filter_group
    {
    public:
        std::size_t index;
        bool        inside_match;
    };

    float                    m_indent{10.0f};
    int                      m_row   {0};
    Editor_state*            m_state {nullptr};
    std::vector<Entry>       m_entries;
    std::string              m_tooltip_scratch; // built while a row with a tooltip_extra is hovered
    bool                     m_filter_enabled{false};
    ImGuiTextFilter          m_filter{};        // same search syntax as the Operations window filter
    std::vector<uint8_t>      m_entry_visible;  // scratch, one per entry; capacity kept
    std::vector<Filter_group> m_filter_stack;   // scratch; capacity kept

    struct Stack_entry
    {
        bool subtree_open;
        float indent_amount;
    };

    std::vector<Stack_entry> m_stack;
};

}
