#pragma once

#include <imgui/imgui.h>

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
    void push_group         (std::string_view label, ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None, float indent = 0.0f, bool* open_state = nullptr);
    void pop_group          ();
    void add_entry          (std::string_view label, std::function<void()> editor);
    void add_entry          (std::string_view label, uint32_t label_text_color, uint32_t label_background_color, std::function<void()> editor);
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
        std::function<void()>   editor;
        ImGuiTreeNodeFlags      flags{ImGuiTreeNodeFlags_None};
        float                   indent{0.0f};
        std::optional<uint32_t> label_text_color{};
        std::optional<uint32_t> label_background_color{};
        bool*                   open_state{nullptr};
    };

    float              m_indent{10.0f};
    int                m_row   {0};
    Editor_state*      m_state {nullptr};
    std::vector<Entry> m_entries;
};

}
