#pragma once

#include "erhe/components/component.hpp"
#include "erhe/scene/viewport.hpp"
#include "tools/pointer_context.hpp"
#include "tools/selection_tool.hpp"

#include <gsl/gsl>

namespace editor {

class Brushes;
class Editor_view;
class Line_renderer;
class Physics_tool;
class Pointer_context;
class Scene_manager;
class Text_renderer;
class Tool;
class Trs_tool;

class Render_context
{
public:
    Pointer_context*      pointer_context{nullptr};
    Scene_manager*        scene_manager  {nullptr};
    Line_renderer*        line_renderer  {nullptr};
    Text_renderer*        text_renderer  {nullptr};
    erhe::scene::Viewport viewport       {0, 0, 0, 0, true};
    double                time           {0.0};
};

class Editor_tools
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Editor_tools"};

    Editor_tools ();
    ~Editor_tools() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    void gui_begin_frame         ();
    void imgui                   ();
    void imgui_render            ();
    void update_and_render_tools (const Render_context& render_context);
    void render_update_tools     (const Render_context& render_context);

    void delete_selected_meshes  ();

    auto get_priority_action     () const -> Action;
    void set_priority_action     (Action action);
    void cancel_ready_tools      (Tool* keep);
    auto get_action_tool         (Action action) const -> Tool*;

    void on_pointer              ();

    void register_tool           (Tool* tool);
    void register_background_tool(Tool* tool);
    void register_imgui_window   (Imgui_window* window);

private:
    Action m_priority_action{Action::select};

    std::shared_ptr<Brushes>        m_brushes;
    std::shared_ptr<Editor_view>    m_editor_view;
    std::shared_ptr<Physics_tool>   m_physics_tool;
    std::shared_ptr<Selection_tool> m_selection_tool;
    std::shared_ptr<Trs_tool>       m_trs_tool;

    std::optional<Selection_tool::Subcription> m_selection_layer_update_subscription;

    std::mutex                                m_mutex;
    std::vector<gsl::not_null<Tool*>>         m_tools;
    std::vector<gsl::not_null<Tool*>>         m_background_tools;
    std::vector<gsl::not_null<Imgui_window*>> m_imgui_windows;
    ImGuiContext*                             m_imgui_context{nullptr};
};

}
