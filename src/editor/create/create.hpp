#pragma once

#include "create/create_box.hpp"
#include "create/create_capsule.hpp"
#include "create/create_cone.hpp"
#include "create/create_torus.hpp"
#include "create/create_uv_sphere.hpp"
#include "app_message.hpp"

#include "erhe_message_bus/message_bus.hpp"
#include "tools/tool.hpp"
#include "tools/tool_window.hpp"

#include "erhe_primitive/enums.hpp"

#include <imgui/imgui.h>

namespace erhe::imgui { class Imgui_windows; }
namespace erhe::scene { class Node; }

namespace editor {

class Brush;
class Render_context;

class Create : public Tool
{
public:
    static constexpr int c_priority{4};

    Create(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context,
        Tools&                       tools
    );

    void tool_render(const Render_context& context) override;
    // Cached reference, for the MCP get_editor_references query
    // (doc/import-undo-reference-clearing.md).
    [[nodiscard]] auto get_brush() const -> const std::shared_ptr<Brush>&;

private:
    // Content removed without a scene closing (undo of a glTF import).
    void on_items_removed(const Removed_items& removed);

    void window_imgui();

    [[nodiscard]] auto get_button_size() -> ImVec2;

    void brush_create_button(const char* label, Create_shape* brush_create);

    [[nodiscard]] auto find_parent() -> std::shared_ptr<erhe::scene::Node>;

    Tool_window                   m_window;
    erhe::primitive::Normal_style m_normal_style{erhe::primitive::Normal_style::point_normals};
    float                         m_density     {1.0f};
    bool                          m_preview_ideal_shape{false};
    bool                          m_preview_shape{true};
    Create_uv_sphere              m_create_uv_sphere;
    Create_cone                   m_create_cone;
    Create_capsule                m_create_capsule;
    Create_torus                  m_create_torus;
    Create_box                    m_create_box;
    Create_shape*                 m_create_shape{nullptr};
    std::string                   m_brush_name;
    std::shared_ptr<Brush>        m_brush;
    erhe::message_bus::Subscription<Items_removed_message> m_items_removed_subscription;
};

}
