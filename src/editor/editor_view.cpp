#include "rendering.hpp"

#include "configuration.hpp"
#include "log.hpp"
#include "editor_time.hpp"
#include "editor_tools.hpp"
#include "editor_view.hpp"
#include "window.hpp"

#include "operations/operation_stack.hpp"
#include "renderers/id_renderer.hpp"
#include "scene/scene_root.hpp"
#include "tools/fly_camera_tool.hpp"
#include "windows/log_window.hpp"
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

const ImVec4 log_color          {0.8f, 0.8f, 1.0f, 0.7f};
const ImVec4 consume_event_color{1.0f, 1.0f, 8.0f, 0.6f};
const ImVec4 filter_event_color {1.0f, 0.8f, 7.0f, 0.6f};

Editor_view::Editor_view()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_description}
{
}

Editor_view::~Editor_view() = default;

void Editor_view::connect()
{
    m_configuration    = get<Configuration   >();
    m_editor_rendering = get<Editor_rendering>();
    m_editor_time      = get<Editor_time     >();
    m_editor_tools     = get<Editor_tools    >();
    m_fly_camera_tool  = get<Fly_camera_tool >();
    m_log_window       = get<Log_window      >();
    m_operation_stack  = get<Operation_stack >();
    m_pointer_context  = get<Pointer_context >();
    m_scene_root       = get<Scene_root      >();
    m_viewport_windows = get<Viewport_windows>();
    m_window           = get<Window          >();
}

void Editor_view::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);
}

void Editor_view::register_command(Command* command)
{
    m_commands.push_back(command);
}

auto Editor_view::bind_command_to_key(
    Command*                     command,
    const erhe::toolkit::Keycode code,
    const bool                   pressed,
    const uint32_t               modifier_mask
) -> erhe::toolkit::Unique_id<Key_binding>::id_type
{
    auto& binding = m_key_bindings.emplace_back(command, code, pressed, modifier_mask);
    return binding.get_id();
}

auto Editor_view::bind_command_to_mouse_click(
    Command*                          command,
    const erhe::toolkit::Mouse_button button
) -> erhe::toolkit::Unique_id<Key_binding>::id_type
{
    auto binding = std::make_unique<Mouse_click_binding>(command, button);
    auto id = binding->get_id();
    m_mouse_bindings.push_back(std::move(binding));
    return id;
}

auto Editor_view::bind_command_to_mouse_motion(
    Command* command
) -> erhe::toolkit::Unique_id<Mouse_motion_binding>::id_type
{
    auto binding = std::make_unique<Mouse_motion_binding>(command);
    auto id = binding->get_id();
    m_mouse_bindings.push_back(std::move(binding));
    return id;
}

auto Editor_view::bind_command_to_mouse_drag(
    Command*                          command,
    const erhe::toolkit::Mouse_button button
) -> erhe::toolkit::Unique_id<Mouse_drag_binding>::id_type
{
    auto binding = std::make_unique<Mouse_drag_binding>(command, button);
    auto id = binding->get_id();
    m_mouse_bindings.push_back(std::move(binding));
    return id;
}

void Editor_view::remove_command_binding(
    const erhe::toolkit::Unique_id<Command_binding>::id_type binding_id
)
{
    std::erase_if(
        m_key_bindings,
        [binding_id](Key_binding& binding)
        {
            return binding.get_id() == binding_id;
        }
    );
    std::erase_if(
        m_mouse_bindings,
        [binding_id](const std::unique_ptr<Mouse_binding>& binding)
        {
            return binding.get()->get_id() == binding_id;
        }
    );
}

static constexpr std::string_view c_swap_buffers{"swap buffers"};

void Editor_view::on_refresh()
{
    if (!m_configuration->show_window)
    {
        return;
    }
    if (!m_ready)
    {
        gl::clear_color(0.0f, 0.0f, 0.0f, 1.0f);
        gl::clear(
            gl::Clear_buffer_mask::color_buffer_bit |
            gl::Clear_buffer_mask::depth_buffer_bit |
            gl::Clear_buffer_mask::stencil_buffer_bit
        );
        m_window->get_context_window()->swap_buffers();
        return;
    }

    if (m_configuration->gui)
    {
        ImGui_ImplErhe_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    }

    //m_editor_time     ->update();
    //m_viewport_windows->update();
    m_editor_rendering->render();

    if (m_configuration->show_window && m_configuration->gui)
    {
        ImGui::EndFrame();
        ImGui::Render();
        m_editor_rendering->clear();
        ImGui_ImplErhe_RenderDrawData(ImGui::GetDrawData());
    }

    m_window->get_context_window()->swap_buffers();
}

void Editor_view::update()
{
    ERHE_PROFILE_FUNCTION

    if (m_configuration->gui)
    {
        ImGui_ImplErhe_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    }

    m_editor_time     ->update();
    m_viewport_windows->update();
    m_editor_rendering->render();

    if (m_configuration->show_window && m_configuration->gui)
    {
        ImGui::EndFrame();
        ImGui::Render();
        m_editor_rendering->clear();
        ImGui_ImplErhe_RenderDrawData(ImGui::GetDrawData());
    }

    if (m_configuration->show_window)
    {
        ERHE_PROFILE_SCOPE(c_swap_buffers.data());

        m_window->get_context_window()->swap_buffers();
    }

    m_ready = true;
}

void Editor_view::on_enter()
{
    m_editor_rendering->init_state();
    m_editor_time->start_time();
}

void Editor_view::on_key(
    const bool                   pressed,
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask
)
{
    Command_context context{
        *this,
        *m_pointer_context,
        get<Log_window>().get()
    };

    for (auto& binding : m_key_bindings)
    {
        if (binding.on_key(context, pressed, code, modifier_mask))
        {
            return;
        }
    }

    context.log_window()->tail_log(
        filter_event_color,
        "key {} {} not consumed",
        erhe::toolkit::c_str(code),
        pressed ? "press" : "release"
    );
    // Keycode::Key_f2:     m_trigger_capture = true; break;
    // Keycode::Key_delete: m_editor_tools->delete_selected_meshes();
}

namespace {

[[nodiscard]] auto get_priority(const State state) -> int
{
    switch (state)
    {
    case State::Active:   return 1;
    case State::Ready:    return 2;
    case State::Inactive: return 3;
    case State::Disabled: return 4;
    }
    return 999;
}

};

auto Editor_view::get_command_priority(Command* command) const -> int
{
    if (command == m_active_mouse_command)
    {
        return 0;
    }
    return get_priority(command->state());
}

void Editor_view::sort_mouse_bindings()
{
    std::sort(
        m_mouse_bindings.begin(),
        m_mouse_bindings.end(),
        [this](
            const std::unique_ptr<Mouse_binding>& lhs,
            const std::unique_ptr<Mouse_binding>& rhs
        ) -> bool
        {
            auto* const lhs_command = lhs.get()->get_command();
            auto* const rhs_command = rhs.get()->get_command();
            ERHE_VERIFY(lhs_command != nullptr);
            ERHE_VERIFY(rhs_command != nullptr);
            return get_command_priority(lhs_command) < get_command_priority(rhs_command);
        }
    );
}

void Editor_view::inactivate_ready_commands()
{
    Command_context context{
        *this,
        *m_pointer_context,
        get<Log_window>().get()
    };
    for (auto* command : m_commands)
    {
        if (command->state() == State::Ready)
        {
            command->set_inactive(context);
        }
    }
}

void Editor_view::update_active_mouse_command(Command* command)
{
    inactivate_ready_commands();
    if (
        (command->state() == State::Active) &&
        (m_active_mouse_command != command)
    )
    {
        ERHE_VERIFY(m_active_mouse_command == nullptr);
        m_active_mouse_command = command;
    }
    else if (
        (command->state() != State::Active) &&
        (m_active_mouse_command == command)
    )
    {
        m_active_mouse_command = nullptr;
    }
}

void Editor_view::on_mouse_click(
    const erhe::toolkit::Mouse_button button,
    const int                         count
)
{
    sort_mouse_bindings();

    ImGuiIO& io = ImGui::GetIO();
    const bool viewport_hovered = (m_pointer_context->window() != nullptr) && m_pointer_context->window()->is_hovered();
    if (
        io.WantCaptureMouse &&
        !viewport_hovered &&
        (m_active_mouse_command == nullptr)
    )
    {
        return;
    }

    m_log_window->tail_log(
        log_color,
        "mouse button {} {}",
        erhe::toolkit::c_str(button),
        count
    );

    m_pointer_context->update_mouse(button, count);

    Command_context context{
        *this,
        *m_pointer_context,
        get<Log_window>().get()
    };
    for (const auto& binding : m_mouse_bindings)
    {
        auto* command = binding->get_command();
        ERHE_VERIFY(command != nullptr);
        if (binding->on_button(context, button, count))
        {
            update_active_mouse_command(command);
            break;
        }
    }
}

void Editor_view::on_mouse_move(const double x, const double y)
{
    ImGuiIO& io = ImGui::GetIO();
    const bool viewport_hovered = (m_pointer_context->window() != nullptr) && m_pointer_context->window()->is_hovered();
    if (
        io.WantCaptureMouse &&
        !viewport_hovered &&
        (m_active_mouse_command == nullptr)
    )
    {
        m_log_window->tail_log(filter_event_color, "ImGui WantCaptureMouse");
        return;
    }

    m_log_window->tail_log(log_color, "mouse move");

    m_pointer_context->update_mouse(x, y);

    Command_context context{
        *this,
        *m_pointer_context,
        get<Log_window>().get()
    };
    for (const auto& binding : m_mouse_bindings)
    {
        auto* command = binding->get_command();
        ERHE_VERIFY(command != nullptr);
        if (binding->on_motion(context))
        {
            update_active_mouse_command(command);
            break;
        }
    }
}

void Editor_view::on_key_press(
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask
)
{
    m_log_window->tail_log(log_color, "key press {}", erhe::toolkit::c_str(code));

    on_key(true, code, modifier_mask);
}

void Editor_view::on_key_release(
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask
)
{
    m_log_window->tail_log(log_color, "key release {}", erhe::toolkit::c_str(code));

    on_key(false, code, modifier_mask);
}

void Editor_view::imgui()
{
    const ImGuiTreeNodeFlags leaf_flags{
        ImGuiTreeNodeFlags_NoTreePushOnOpen |
        ImGuiTreeNodeFlags_Leaf
    };

    ImGui::Text(
        "Active mouse command: %s",
        (m_active_mouse_command != nullptr)
            ? m_active_mouse_command->name()
            : "(none)"
    );

    if (ImGui::TreeNodeEx("Active", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (auto* command : m_commands)
        {
            if (command->state() == State::Active)
            {
                ImGui::TreeNodeEx(command->name(), leaf_flags);
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Ready", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (auto* command : m_commands)
        {
            if (command->state() == State::Ready)
            {
                ImGui::TreeNodeEx(command->name(), leaf_flags);
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Inactive", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (auto* command : m_commands)
        {
            if (command->state() == State::Inactive)
            {
                ImGui::TreeNodeEx(command->name(), leaf_flags);
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Disabled", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (auto* command : m_commands)
        {
            if (command->state() == State::Disabled)
            {
                ImGui::TreeNodeEx(command->name(), leaf_flags);
            }
        }
        ImGui::TreePop();
    }
}

}  // namespace editor
