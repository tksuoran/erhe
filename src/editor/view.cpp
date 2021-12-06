#include "rendering.hpp"
#include "log.hpp"
#include "time.hpp"
#include "tools.hpp"
#include "view.hpp"
#include "window.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/id_renderer.hpp"
#include "scene/scene_manager.hpp"
#include "scene/scene_root.hpp"
#include "tools/fly_camera_tool.hpp"
#include "windows/frame_log_window.hpp"
#include "windows/viewport_window.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/imgui/imgui_impl_erhe.hpp"
#include "erhe/raytrace/mesh_intersect.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

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
    m_frame_log_window = get<Frame_log_window>();
    m_operation_stack  = get<Operation_stack >();
    m_pointer_context  = get<Pointer_context >();
    m_scene_manager    = get<Scene_manager   >();
    m_scene_root       = get<Scene_root      >();
    m_viewport_windows = get<Viewport_windows>();
    m_window           = get<Window          >();
}

static constexpr std::string_view c_swap_buffers{"swap buffers"};

void Editor_view::update()
{
    ImGui_ImplErhe_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    m_frame_log_window->new_frame();
    m_editor_time->update();
    m_viewport_windows->update();
    m_editor_tools->update_tools();
    m_editor_rendering->render();

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplErhe_RenderDrawData(ImGui::GetDrawData());

    {
        ERHE_PROFILE_SCOPE(c_swap_buffers.data());

        m_window->get_context_window()->swap_buffers();
    }
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

    m_frame_log_window->log(
        "key pressed = {} code = {}, modifier_mask = {:04x}",
        pressed,
        code,
        modifier_mask
    );

    if (
        io.WantCaptureKeyboard &&
        (m_pointer_context->window() == nullptr) &&
        pressed
    )
    {
        return;
    }

    m_pointer_context->update_keyboard(pressed, code, modifier_mask);

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
                if (
                    m_pointer_context->control_key_down() &&
                    m_operation_stack->can_undo()
                )
                {
                    m_operation_stack->undo();
                }
                break;
            }

            case Keycode::Key_y:
            {
                if (
                    m_pointer_context->control_key_down() &&
                    m_operation_stack->can_redo()
                )
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
    m_frame_log_window->log(
        "mouse button = {} count = {}",
        button,
        count
    );

    m_pointer_context->update_mouse(button, count);

    ImGuiIO& io = ImGui::GetIO();
    auto* frame_log = get<Frame_log_window>();

    const bool want_capture        = io.WantCaptureMouse;
    const bool pointer_in_viewport = m_pointer_context->window() != nullptr;
    frame_log->log(
        "button = {}, count = {}, want capture = {}, pointer in viewport = {}",
        button,
        count,
        want_capture,
        pointer_in_viewport
    );
    if (
        want_capture &&
        !pointer_in_viewport &&
        (count > 0)
    )
    {
        return;
    }

    m_editor_tools->on_pointer();
}

void Editor_view::on_mouse_move(const double x, const double y)
{
    m_frame_log_window->log(
        "mouse x = {} y = {}",
        x,
        y
    );

    m_pointer_context->update_mouse(x, y);

    ImGuiIO& io = ImGui::GetIO();
    if (
        io.WantCaptureMouse && 
        (m_pointer_context->window() == nullptr)
    )
    {
        return;
    }

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
