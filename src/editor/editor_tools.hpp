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

class Editor_tools
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name       {"Editor_tools"};
    static constexpr std::string_view c_description{"Editor Tools"};
    static constexpr uint32_t         hash{
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Editor_tools ();
    ~Editor_tools() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    void imgui_windows           ();
    void render_tools            (const Render_context& context);
    void begin_frame             ();
    void register_tool           (Tool* tool);
    void register_background_tool(Tool* tool);
    void register_imgui_window   (Imgui_window* window);
    void menu                    ();

private:
    void window_menu();

    // Component dependencies
    std::shared_ptr<Editor_view> m_editor_view;
    std::shared_ptr<Editor_time> m_editor_time;

    std::mutex                                m_mutex;
    std::vector<gsl::not_null<Tool*>>         m_tools;
    std::vector<gsl::not_null<Tool*>>         m_background_tools;
    std::vector<gsl::not_null<Imgui_window*>> m_imgui_windows;
    ImGuiContext*                             m_imgui_context{nullptr};
    ImVector<ImWchar>                         m_glyph_ranges;
    bool                                      m_show_tool_properties{true};
    bool                                      m_show_style_editor   {false};
};

}
