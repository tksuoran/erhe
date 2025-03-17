#pragma once

#include <imgui/imgui.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

class Property_editor
{
public:
    void reset       ();
    void resume      ();
    void push_group  (std::string_view label, ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None, float indent = 0.0f, bool* open_state = nullptr);
    void pop_group   ();
    void add_entry   (std::string_view label, std::function<void()> editor);
    void add_entry   (std::string_view label, uint32_t label_text_color, uint32_t label_background_color, std::function<void()> editor);
    void show_entries(const char* label = "##", ImVec2 cell_padding = ImVec2{0.0f, 0.0f});

protected:
    float m_indent = 10.0f;
    int m_row = 0;
    struct Entry
    {
        bool                    push_group{false};
        bool                    pop_group{false};
        std::string             label;
        std::function<void()>   editor;
        ImGuiTreeNodeFlags      flags{ImGuiTreeNodeFlags_None};
        float                   indent{0.0f};
        std::optional<uint32_t> label_text_color;
        std::optional<uint32_t> label_background_color;
        bool*                   open_state{nullptr};
    };
    std::vector<Entry> m_entries;
};

} // namespace editor
