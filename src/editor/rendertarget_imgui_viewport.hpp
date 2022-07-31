#pragma once

#include "erhe/application/imgui_viewport.hpp"

#include <string_view>

namespace erhe::application
{
    class Imgui_renderer;
    class Imgui_window;
    class Imgui_windows;
    class View;
}

namespace erhe::components
{
    class Components;
}

namespace editor
{

class Rendertarget_node;
class Viewport_window;

class Rendertarget_imgui_viewport
    : public erhe::application::Imgui_viewport
{
public:
    Rendertarget_imgui_viewport(
        Rendertarget_node*                  rendertarget_node,
        const std::string_view              name,
        const erhe::components::Components& components
    );
    virtual ~Rendertarget_imgui_viewport() noexcept;

    // Implements Imgui_vewport
    [[nodiscard]] auto rendertarget_node() -> Rendertarget_node*;
    [[nodiscard]] auto begin_imgui_frame() -> bool override;
    void end_imgui_frame() override;

    // Implements Render_graph_node
    void execute_render_graph_node() override;

private:
    Rendertarget_node*                                 m_rendertarget_node;
    std::shared_ptr<erhe::application::Imgui_renderer> m_imgui_renderer;
    std::shared_ptr<erhe::application::Imgui_windows>  m_imgui_windows;
    std::shared_ptr<erhe::application::View>           m_view;
    std::string                                        m_name;
    std::string                                        m_imgui_ini_path;
    bool                                               m_has_focus{false};
    double                                             m_time     {0.0};
    double m_last_mouse_x{0.0};
    double m_last_mouse_y{0.0};
};

} // namespace erhe::application
