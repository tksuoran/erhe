#pragma once

#include "erhe/components/component.hpp"

#include <imgui.h>

#include <gsl/gsl>

namespace editor {

class Editor_view;
class Editor_time;
class Imgui_window;
class Render_context;
class Tool;

class Editor_imgui_windows
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name       {"Editor_imgui_windows"};
    static constexpr std::string_view c_description{"Editor ImGui Windows"};
    static constexpr uint32_t         hash{
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Editor_imgui_windows ();
    ~Editor_imgui_windows() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    void imgui_windows        ();
    void register_imgui_window(Imgui_window* window);
    void menu                 ();

private:
    void window_menu();

    std::mutex                                m_mutex;
    std::vector<gsl::not_null<Imgui_window*>> m_imgui_windows;
    ImGuiContext*                             m_imgui_context{nullptr};
    ImVector<ImWchar>                         m_glyph_ranges;
    bool                                      m_show_tool_properties{true};
    bool                                      m_show_style_editor   {false};
};

}
