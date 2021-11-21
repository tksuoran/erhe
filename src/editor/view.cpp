#include "rendering.hpp"
#include "log.hpp"
#include "time.hpp"
#include "tools.hpp"
#include "view.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/id_renderer.hpp"
#include "scene/scene_manager.hpp"
#include "scene/scene_root.hpp"
#include "tools/fly_camera_tool.hpp"

#include "erhe/scene/camera.hpp"
#include "erhe/imgui/imgui_impl_erhe.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <backends/imgui_impl_glfw.h>

namespace editor {

using namespace erhe::geometry;
using namespace erhe::toolkit;

Editor_view::Editor_view()
    : erhe::components::Component{c_name}
{
}

Editor_view::~Editor_view() = default;

void Editor_view::connect()
{
    m_editor_rendering = get<Editor_rendering>();
    m_editor_time      = get<Editor_time     >();
    m_editor_tools     = get<Editor_tools    >();
    m_fly_camera_tool  = get<Fly_camera_tool >();
    m_id_renderer      = get<Id_renderer     >();
    m_operation_stack  = get<Operation_stack >();
    m_scene_manager    = get<Scene_manager   >();
    m_scene_root       = get<Scene_root      >();
}

void Editor_view::update()
{
    m_editor_time->update();
}

void Editor_view::update_pointer()
{
    const glm::vec2 pointer = m_editor_rendering->to_scene_content(
        glm::vec2{
            pointer_context.mouse_x,
            pointer_context.mouse_y
        }
    );

    auto* camera = m_scene_manager->get_view_camera().get();

    float z{1.0f};
    pointer_context.camera                 = camera;
    pointer_context.pointer_x              = static_cast<int>(pointer.x);
    pointer_context.pointer_y              = static_cast<int>(pointer.y);
    pointer_context.pointer_z              = z;
    pointer_context.viewport.x             = 0;
    pointer_context.viewport.y             = 0;
    pointer_context.viewport.width         = m_editor_rendering->scene_viewport.width;
    pointer_context.viewport.height        = m_editor_rendering->scene_viewport.height;
    pointer_context.viewport.reverse_depth = m_editor_rendering->scene_viewport.reverse_depth;
    pointer_context.scene_view_focus       = m_editor_rendering->is_content_in_focus();
    pointer_context.geometry               = nullptr;

    if (pointer_context.pointer_in_content_area() && m_id_renderer)
    {
        auto mesh_primitive = m_id_renderer->get(
            pointer_context.pointer_x,
            pointer_context.pointer_y,
            pointer_context.pointer_z
        );
        pointer_context.hover_valid = mesh_primitive.valid;
        if (pointer_context.hover_valid)
        {
            pointer_context.hover_mesh        = mesh_primitive.mesh;
            pointer_context.hover_layer       = mesh_primitive.layer;
            pointer_context.hover_primitive   = mesh_primitive.mesh_primitive_index;
            pointer_context.hover_local_index = mesh_primitive.local_index;
            pointer_context.hover_tool        = pointer_context.hover_layer == m_scene_root->tool_layer().get();
            pointer_context.hover_content     = pointer_context.hover_layer == &m_scene_root->content_layer();
            if (pointer_context.hover_mesh != nullptr)
            {
                const auto& primitive        = pointer_context.hover_mesh->data.primitives[pointer_context.hover_primitive];
                auto*       geometry         = primitive.primitive_geometry->source_geometry.get();
                pointer_context.geometry     = geometry;
                pointer_context.hover_normal = {};
                if (geometry != nullptr)
                {
                    Polygon_id polygon_id = static_cast<Polygon_id>(pointer_context.hover_local_index);
                    if (polygon_id < geometry->polygon_count())
                    {
                        auto* polygon_normals = geometry->polygon_attributes().find<glm::vec3>(c_polygon_normals);
                        if ((polygon_normals != nullptr) && polygon_normals->has(polygon_id))
                        {
                            auto local_normal    = polygon_normals->get(polygon_id);
                            auto world_from_node = pointer_context.hover_mesh->world_from_node();
                            pointer_context.hover_normal = glm::vec3{world_from_node * glm::vec4{local_normal, 0.0f}};
                        }
                    }
                }
            }
        }
        // else mesh etc. contain latest valid values
    }
    else
    {
        pointer_context.hover_valid       = false;
        pointer_context.hover_mesh        = nullptr;
        pointer_context.hover_primitive   = 0;
        pointer_context.hover_local_index = 0;
    }

    pointer_context.frame_number = m_editor_time->m_frame_number;
}

void Editor_view::on_enter()
{
    get<Editor_rendering>()->init_state();
    get<Editor_time     >()->start_time();
}

void Editor_view::on_exit()
{
    log_startup.info("exiting\n");
}

void Editor_view::on_key(
    const bool                   pressed,
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask
)
{
    ImGuiIO& io = ImGui::GetIO();

    if (io.WantCaptureKeyboard && !m_editor_rendering->is_content_in_focus() && pressed)
    {
        return;
    }

    pointer_context.shift   = (modifier_mask & Key_modifier_bit_shift) == Key_modifier_bit_shift;
    pointer_context.alt     = (modifier_mask & Key_modifier_bit_menu ) == Key_modifier_bit_menu;
    pointer_context.control = (modifier_mask & Key_modifier_bit_ctrl ) == Key_modifier_bit_ctrl;

    switch (code)
    {
        //case Keycode::Key_f2:         m_trigger_capture = true; break;
        case Keycode::Key_space:      return m_fly_camera_tool->y_pos_control(pressed);
        case Keycode::Key_left_shift: return m_fly_camera_tool->y_neg_control(pressed);
        case Keycode::Key_w:          return m_fly_camera_tool->z_neg_control(pressed);
        case Keycode::Key_s:          return m_fly_camera_tool->z_pos_control(pressed);
        case Keycode::Key_a:          return m_fly_camera_tool->x_neg_control(pressed);
        case Keycode::Key_d:          return m_fly_camera_tool->x_pos_control(pressed);
        default:                      break;
    }

    if (pressed)
    {
        switch (code)
        {
            case Keycode::Key_z:
            {
                if (pointer_context.control && m_operation_stack->can_undo())
                {
                    m_operation_stack->undo();
                }
                break;
            }

            case Keycode::Key_y:
            {
                if (pointer_context.control && m_operation_stack->can_redo())
                {
                    m_operation_stack->redo();
                }
                break;
            }

            case Keycode::Key_delete:
            {
                log_input.info("Delete pressed\n");
                m_editor_tools->delete_selected_meshes();
                break;
            }

            default: break;
        }
    }
    // clang-format on
}

void Editor_view::on_mouse_click(
    const erhe::toolkit::Mouse_button button,
    const int                         count
)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse && !m_editor_rendering->is_content_in_focus() && (count > 0))
    {
        return;
    }

    pointer_context.mouse_button[button].pressed  = (count > 0);
    pointer_context.mouse_button[button].released = (count == 0);
    pointer_context.mouse_moved                   = false;

    m_editor_tools->on_pointer();
}

void Editor_view::on_mouse_move(const double x, const double y)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse && !m_editor_rendering->is_content_in_focus())
    {
        return;
    }

    pointer_context.mouse_moved = true;
    pointer_context.mouse_x     = x;
    pointer_context.mouse_y     = y;
    update_pointer();
    m_editor_tools->on_pointer();
}

void Editor_view::on_key_press(
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask
)
{
    on_key(true, code, modifier_mask);
}

void Editor_view::on_key_release(
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask
)
{
    on_key(false, code, modifier_mask);
}

}  // namespace editor
