#pragma once

#include "tools/brushes/create/create_box.hpp"
#include "tools/brushes/create/create_cone.hpp"
#include "tools/brushes/create/create_torus.hpp"
#include "tools/brushes/create/create_uv_sphere.hpp"

#include "tools/tool.hpp"

#include "erhe_primitive/enums.hpp"

#include "erhe_imgui/imgui_window.hpp"

#include <imgui/imgui.h>


namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::scene {
    class Node;
}

namespace editor
{

class Brush;
class Render_context;

class Create
    : public erhe::imgui::Imgui_window
    , public Tool
{
public:
    static constexpr int c_priority{4};

    Create(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context,
        Tools&                       tools
    );

    void imgui() override;
    void tool_render(const Render_context& context) override;

private:
    [[nodiscard]] auto get_button_size() -> ImVec2;

    void brush_create_button(const char* label, Create_shape* brush_create);

    [[nodiscard]] auto find_parent() -> std::shared_ptr<erhe::scene::Node>;

    erhe::primitive::Normal_style m_normal_style{erhe::primitive::Normal_style::point_normals};
    float                         m_density     {1.0f};
    bool                          m_preview_ideal_shape{false};
    bool                          m_preview_shape{true};
    Create_uv_sphere              m_create_uv_sphere;
    Create_cone                   m_create_cone;
    Create_torus                  m_create_torus;
    Create_box                    m_create_box;
    Create_shape*                 m_create_shape{nullptr};
    std::string                   m_brush_name;
    std::shared_ptr<Brush>        m_brush;
};

} // namespace editor
