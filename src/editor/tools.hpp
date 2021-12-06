#pragma once

#include "tools/pointer_context.hpp"
#include "tools/selection_tool.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/components/component.hpp"
#include "erhe/scene/viewport.hpp"

#include <gsl/gsl>

namespace editor {

class Brushes;
class Editor_view;
class Editor_time;
class Frame_log_window;
class Line_renderer;
class Physics_tool;
class Pointer_context;
class Scene_manager;
class Text_renderer;
class Tool;
class Trs_tool;

class Editor_tools
    : public erhe::components::Component
    , public erhe::components::IUpdate_once_per_frame
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name       {"Editor_tools"};
    static constexpr std::string_view c_description{"Tools"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Editor_tools ();
    ~Editor_tools() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements IUpdate_once_per_frame
    void update_once_per_frame(const erhe::components::Time_context&) override;

    // Implements Imgui_window
    void imgui                   () override;

    void imgui_windows           ();
    void imgui_editor_tools      ();
    void gui_begin_frame         ();
    void update_tools            ();
    void render_tools            (const Render_context& context);
    void begin_frame             ();
    void delete_selected_meshes  ();
    auto get_priority_action     () const -> Action;
    void set_priority_action     (Action action);
    void cancel_ready_tools      (Tool* keep);
    auto get_action_tool         (Action action) const -> Tool*;
    void on_pointer              ();
    void register_tool           (Tool* tool);
    void register_background_tool(Tool* tool);
    void register_imgui_window   (Imgui_window* window);
    void menu                    ();

private:
    void window_menu();

    Action m_priority_action{Action::select};

    Brushes*          m_brushes         {nullptr};
    Editor_view*      m_editor_view     {nullptr};
    Editor_time*      m_editor_time     {nullptr};
    Frame_log_window* m_frame_log_window{nullptr};
    Physics_tool*     m_physics_tool    {nullptr};
    Pointer_context*  m_pointer_context {nullptr};
    Selection_tool*   m_selection_tool  {nullptr};
    Trs_tool*         m_trs_tool        {nullptr};
    bool              m_show_tool_properties{true};

    std::optional<Selection_tool::Subcription> m_selection_layer_update_subscription;

    std::mutex                                m_mutex;
    std::vector<gsl::not_null<Tool*>>         m_tools;
    std::vector<gsl::not_null<Tool*>>         m_background_tools;
    std::vector<gsl::not_null<Imgui_window*>> m_imgui_windows;
    ImGuiContext*                             m_imgui_context{nullptr};
    ImVector<ImWchar>                         m_glyph_ranges;
};

}
