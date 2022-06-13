#include "erhe/application/view.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/log.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/time.hpp"
#include "erhe/application/window.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/commands/key_binding.hpp"
#include "erhe/application/commands/mouse_click_binding.hpp"
#include "erhe/application/commands/mouse_drag_binding.hpp"
#include "erhe/application/commands/mouse_motion_binding.hpp"
#include "erhe/application/commands/mouse_wheel_binding.hpp"
#include "erhe/application/renderers/imgui_renderer.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/raytrace/mesh_intersect.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

#include <backends/imgui_impl_glfw.h>

namespace erhe::application {

//// const ImVec4 log_color          {0.8f, 0.8f, 1.0f, 0.7f};
//// const ImVec4 consume_event_color{1.0f, 1.0f, 8.0f, 0.6f};
//// const ImVec4 filter_event_color {1.0f, 0.8f, 7.0f, 0.6f};

View::View()
    : erhe::components::Component{c_label}
    , Imgui_window               {c_title, c_label}
{
}

View::~View() noexcept
{
}

void View::connect()
{
    m_configuration = get    <Configuration>();
    m_imgui_windows = require<Imgui_windows>();
    m_time          = get    <Time         >();
    m_window        = require<Window       >();
}

void View::initialize_component()
{
    m_imgui_windows->register_imgui_window(this);

    double mouse_x;
    double mouse_y;
    m_window->get_context_window()->get_cursor_position(mouse_x, mouse_y);
    m_last_mouse_position = glm::dvec2{mouse_x, mouse_y};
}

void View::set_client(View_client* view_client)
{
    m_view_client = view_client;
}

void View::register_command(Command* const command)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    m_commands.push_back(command);
}

auto View::bind_command_to_key(
    Command* const                   command,
    const erhe::toolkit::Keycode     code,
    const bool                       pressed,
    const nonstd::optional<uint32_t> modifier_mask
) -> erhe::toolkit::Unique_id<Key_binding>::id_type
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    auto& binding = m_key_bindings.emplace_back(command, code, pressed, modifier_mask);
    return binding.get_id();
}

auto View::bind_command_to_mouse_click(
    Command* const                    command,
    const erhe::toolkit::Mouse_button button
) -> erhe::toolkit::Unique_id<Mouse_click_binding>::id_type
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    auto binding = std::make_unique<Mouse_click_binding>(command, button);
    auto id = binding->get_id();
    m_mouse_bindings.push_back(std::move(binding));
    return id;
}

auto View::bind_command_to_mouse_wheel(
    Command* const command
) -> erhe::toolkit::Unique_id<Mouse_wheel_binding>::id_type
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    auto binding = std::make_unique<Mouse_wheel_binding>(command);
    auto id = binding->get_id();
    m_mouse_wheel_bindings.push_back(std::move(binding));
    return id;
}

auto View::bind_command_to_mouse_motion(
    Command* const command
) -> erhe::toolkit::Unique_id<Mouse_motion_binding>::id_type
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    auto binding = std::make_unique<Mouse_motion_binding>(command);
    auto id = binding->get_id();
    m_mouse_bindings.push_back(std::move(binding));
    return id;
}

auto View::bind_command_to_mouse_drag(
    Command* const                    command,
    const erhe::toolkit::Mouse_button button
) -> erhe::toolkit::Unique_id<Mouse_drag_binding>::id_type
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    auto binding = std::make_unique<Mouse_drag_binding>(command, button);
    auto id = binding->get_id();
    m_mouse_bindings.push_back(std::move(binding));
    return id;
}

void View::remove_command_binding(
    const erhe::toolkit::Unique_id<Command_binding>::id_type binding_id
)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    m_key_bindings.erase(
        std::remove_if(
            m_key_bindings.begin(),
            m_key_bindings.end(),
            [binding_id](const Key_binding& binding)
            {
                return binding.get_id() == binding_id;
            }
        ),
        m_key_bindings.end()
    );
    m_mouse_bindings.erase(
        std::remove_if(
            m_mouse_bindings.begin(),
            m_mouse_bindings.end(),
            [binding_id](const std::unique_ptr<Mouse_binding>& binding)
            {
                return binding.get()->get_id() == binding_id;
            }
        ),
        m_mouse_bindings.end()
    );
    m_mouse_wheel_bindings.erase(
        std::remove_if(
            m_mouse_wheel_bindings.begin(),
            m_mouse_wheel_bindings.end(),
            [binding_id](const std::unique_ptr<Mouse_wheel_binding>& binding)
            {
                return binding.get()->get_id() == binding_id;
            }
        ),
        m_mouse_wheel_bindings.end()
    );
}

void View::command_inactivated(Command* const command)
{
    // std::lock_guard<std::mutex> lock{m_command_mutex};

    if (m_active_mouse_command == command)
    {
        m_active_mouse_command = nullptr;
    }
}

void View::on_refresh()
{
    if (!m_configuration->window.show)
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

    if (m_view_client != nullptr)
    {
        m_view_client->render();
    }
    m_window->get_context_window()->swap_buffers();
}

static constexpr std::string_view c_swap_buffers{"swap_buffers"};

void View::run()
{
    m_imgui_windows->make_imgui_context_current();
    for (;;)
    {
        if (m_close_requested)
        {
            break;
        }

        get<Window>()->get_context_window()->poll_events();
        //m_imgui_windows->make_imgui_context_uncurrent();

        if (m_close_requested)
        {
            break;
        }

        update();
    }
}

void View::on_close()
{
    m_close_requested = true;
}

void View::update()
{
    ERHE_PROFILE_FUNCTION

    m_time->update();

    if (m_view_client != nullptr)
    {
        m_view_client->update();
        m_view_client->render();

        if (get<Configuration>()->imgui.enabled)
        {
            m_view_client->bind_default_framebuffer();
            m_view_client->clear();
            //m_imgui_windows->make_imgui_context_current();
            m_imgui_windows->imgui_windows();
            m_imgui_windows->render_imgui_frame();
            //m_imgui_windows->make_imgui_context_uncurrent();
        }
    }

    if (m_configuration->window.show)
    {
        ERHE_PROFILE_SCOPE(c_swap_buffers.data());

        erhe::graphics::Gpu_timer::end_frame();
        m_window->get_context_window()->swap_buffers();
    }

    m_ready = true;
}

[[nodiscard]] auto View::mouse_input_sink() const -> Imgui_window*
{
    return m_mouse_input_sink;
}

void View::on_enter()
{
    if (m_view_client != nullptr)
    {
        m_view_client->initial_state();
    }
    m_time->start_time();
}

void View::on_focus(int focused)
{
    m_imgui_windows->on_focus(focused);
}

void View::on_cursor_enter(int entered)
{
    m_imgui_windows->on_cursor_enter(entered);
}

void View::on_key(
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask,
    const bool                   pressed
)
{
    m_imgui_windows->on_key(
        static_cast<signed int>(code),
        modifier_mask,
        pressed
    );

    if (m_view_client != nullptr)
    {
        m_view_client->update_keyboard(pressed, code, modifier_mask);
    }

    Command_context context{
        *this,
        m_mouse_input_sink
    };

    for (auto& binding : m_key_bindings)
    {
        if (binding.on_key(context, pressed, code, modifier_mask))
        {
            return;
        }
    }

    log_input_event_filtered->trace(
        "key {} {} not consumed",
        erhe::toolkit::c_str(code),
        pressed ? "press" : "release"
    );
}

void View::on_char(
    const unsigned int codepoint
)
{
    log_input_event->trace("char input codepoint = {}", codepoint);
    m_imgui_windows->on_char(codepoint);
}

namespace {

[[nodiscard]] auto get_priority(const State state) -> int
{
    switch (state)
    {
        //using enum State;
        case State::Active:   return 1;
        case State::Ready:    return 2;
        case State::Inactive: return 3;
        case State::Disabled: return 4;
    }
    return 999;
}

};

auto View::get_command_priority(Command* const command) const -> int
{
    if (command == m_active_mouse_command)
    {
        return 0;
    }
    return get_priority(command->state());
}

void View::sort_mouse_bindings()
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

void View::inactivate_ready_commands()
{
    //std::lock_guard<std::mutex> lock{m_command_mutex};

    Command_context context{
        *this,
        m_mouse_input_sink
    };
    for (auto* command : m_commands)
    {
        if (command->state() == State::Ready)
        {
            command->set_inactive(context);
        }
    }
}

void View::set_mouse_input_sink(Imgui_window* mouse_input_sink)
{
    m_mouse_input_sink = mouse_input_sink;
    if (mouse_input_sink != nullptr)
    {
        m_window_position           = glm::vec2{ImGui::GetWindowPos()};
        m_window_size               = glm::vec2{ImGui::GetWindowSize()};
        m_window_content_region_min = glm::vec2{ImGui::GetWindowContentRegionMin()};
        m_window_content_region_max = glm::vec2{ImGui::GetWindowContentRegionMax()};
    }
    else
    {
        m_window_position = glm::vec2{0.0f, 0.0f};
        m_window_size     = glm::vec2{0.0f, 0.0f};
    }
}

auto View::to_window_bottom_left(const glm::vec2 position_in_root) const -> glm::vec2
{
    // TODO This has not been tested!
    const float content_x      = static_cast<float>(position_in_root.x) - m_window_position.x - m_window_content_region_min.x;
    const float content_y      = static_cast<float>(position_in_root.y) - m_window_position.y - m_window_content_region_min.y;
    const float content_flip_y = m_window_size.y - content_y;
    return {
        content_x,
        content_flip_y
    };
}

auto View::to_window_top_left(const glm::vec2 position_in_root) const -> glm::vec2
{
    const float content_x = static_cast<float>(position_in_root.x) - m_window_position.x - m_window_content_region_min.x;
    const float content_y = static_cast<float>(position_in_root.y) - m_window_position.y - m_window_content_region_min.y;
    return {
        content_x,
        content_y
    };
}

auto View::last_mouse_position() const -> glm::dvec2
{
    return m_last_mouse_position;
}

auto View::last_mouse_position_delta() const -> glm::dvec2
{
    return m_last_mouse_position_delta;
}

auto View::last_mouse_wheel_delta() const -> glm::dvec2
{
    return m_last_mouse_wheel_delta;
}

void View::update_active_mouse_command(
    Command* const command)
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

auto View::get_imgui_capture_mouse() const -> bool
{
    const bool viewports_hosted_in_imgui =
        m_configuration->window.show &&
        m_configuration->imgui.enabled;

    if (!viewports_hosted_in_imgui)
    {
        return false;
    }

    const auto& imgui_windows = get<Imgui_windows>();
    if (!imgui_windows)
    {
        return false;
    }

    return imgui_windows->want_capture_mouse();
}

void View::on_mouse_click(
    const erhe::toolkit::Mouse_button button,
    const int                         count
)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    m_imgui_windows->on_mouse_click(static_cast<uint32_t>(button), count);

    sort_mouse_bindings();

    const bool imgui_capture_mouse  = get_imgui_capture_mouse();
    const bool has_mouse_input_sink = (m_mouse_input_sink != nullptr);

    if (
        imgui_capture_mouse &&
        !has_mouse_input_sink &&
        (m_active_mouse_command == nullptr)
    )
    {
        return;
    }

    log_input_event->trace(
        "mouse button {} {}",
        erhe::toolkit::c_str(button),
        count
    );

    if (m_view_client != nullptr)
    {
        m_view_client->update_mouse(button, count);
    }

    Command_context context{
        *this,
        m_mouse_input_sink,
        m_last_mouse_position
    };
    for (const auto& binding : m_mouse_bindings)
    {
        auto* const command = binding->get_command();
        ERHE_VERIFY(command != nullptr);
        if (binding->on_button(context, button, count))
        {
            update_active_mouse_command(command);
            break;
        }
    }
}

void View::on_mouse_wheel(const double x, const double y)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    m_imgui_windows->on_mouse_wheel(x, y);

    sort_mouse_bindings();

    const bool imgui_capture_mouse  = get_imgui_capture_mouse();
    const bool has_mouse_input_sink = (m_mouse_input_sink != nullptr);

    if (
        imgui_capture_mouse &&
        !has_mouse_input_sink &&
        (m_active_mouse_command == nullptr)
    )
    {
        return;
    }

    m_last_mouse_wheel_delta.x = x;
    m_last_mouse_wheel_delta.y = y;

    log_input_event->trace("mouse wheel {}, {}", x, y);

    Command_context context{
        *this,
        m_mouse_input_sink,
        m_last_mouse_position,
        m_last_mouse_wheel_delta
    };
    for (const auto& binding : m_mouse_wheel_bindings)
    {
        auto* const command = binding->get_command();
        ERHE_VERIFY(command != nullptr);
        binding->on_wheel(context); // does not set active mouse command - each wheel event is one shot
    }
}

void View::on_mouse_move(const double x, const double y)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    const bool imgui_capture_mouse  = get_imgui_capture_mouse();
    const bool has_mouse_input_sink = (m_mouse_input_sink != nullptr);

    if (
        imgui_capture_mouse &&
        !has_mouse_input_sink &&
        (m_active_mouse_command == nullptr)
    )
    {
        SPDLOG_LOGGER_TRACE(log_input_event_filtered, "ImGui WantCaptureMouse");
        return;
    }

    glm::dvec2 new_mouse_position{x, y};
    m_last_mouse_position_delta = m_last_mouse_position - new_mouse_position;
    if (m_view_client != nullptr)
    {
        m_view_client->update_mouse(x, y);
    }

    m_last_mouse_position = new_mouse_position;
    Command_context context{
        *this,
        m_mouse_input_sink,
        m_last_mouse_position,
        m_last_mouse_position_delta
    };

    for (const auto& binding : m_mouse_bindings)
    {
        auto* const command = binding->get_command();
        ERHE_VERIFY(command != nullptr);
        if (binding->on_motion(context))
        {
            update_active_mouse_command(command);
            break;
        }
    }
}

void View::imgui()
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

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
