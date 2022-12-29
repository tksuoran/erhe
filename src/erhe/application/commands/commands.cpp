// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/commands/controller_trigger_binding.hpp"
#include "erhe/application/commands/controller_trigger_click_binding.hpp"
#include "erhe/application/commands/controller_trigger_drag_binding.hpp"
#include "erhe/application/commands/controller_trigger_value_binding.hpp"
#include "erhe/application/commands/controller_trackpad_binding.hpp"
#include "erhe/application/commands/key_binding.hpp"
#include "erhe/application/commands/mouse_click_binding.hpp"
#include "erhe/application/commands/mouse_drag_binding.hpp"
#include "erhe/application/commands/mouse_motion_binding.hpp"
#include "erhe/application/commands/mouse_wheel_binding.hpp"
#include "erhe/application/commands/update_binding.hpp"
#include "erhe/application/window.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace erhe::application {

Commands::Commands()
    : erhe::components::Component{c_type_name}
{
}

Commands::~Commands() noexcept
{
}

void Commands::declare_required_components()
{
    require<Window>();
}

void Commands::initialize_component()
{
    double mouse_x{};
    double mouse_y{};
    get<Window>()->get_context_window()->get_cursor_position(mouse_x, mouse_y);
    m_last_mouse_position = glm::dvec2{mouse_x, mouse_y};
}

void Commands::post_initialize()
{
    m_configuration = get<Configuration>();
}

void Commands::register_command(Command* const command)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    m_commands.push_back(command);
}

[[nodiscard]] auto Commands::get_commands() const -> const std::vector<Command*>&
{
    return m_commands;
}

[[nodiscard]] auto Commands::get_key_bindings() const -> const std::vector<Key_binding>&
{
    return m_key_bindings;
}

[[nodiscard]] auto Commands::get_mouse_bindings() const -> const std::vector<std::unique_ptr<Mouse_binding>>&
{
    return m_mouse_bindings;
}

[[nodiscard]] auto Commands::get_mouse_wheel_bindings() const -> const std::vector<std::unique_ptr<Mouse_wheel_binding>>&
{
    return m_mouse_wheel_bindings;
}

[[nodiscard]] auto Commands::get_controller_trigger_bindings() const -> const std::vector<std::unique_ptr<Controller_trigger_binding>>&
{
    return m_controller_trigger_bindings;
}

[[nodiscard]] auto Commands::get_controller_trackpad_bindings() const -> const std::vector<Controller_trackpad_binding>&
{
    return m_controller_trackpad_bindings;
}

[[nodiscard]] auto Commands::get_update_bindings() const -> const std::vector<Update_binding>&
{
    return m_update_bindings;
}

auto Commands::bind_command_to_key(
    Command* const                command,
    const erhe::toolkit::Keycode  code,
    const bool                    pressed,
    const std::optional<uint32_t> modifier_mask
) -> erhe::toolkit::Unique_id<Key_binding>::id_type
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    auto& binding = m_key_bindings.emplace_back(command, code, pressed, modifier_mask);
    return binding.get_id();
}

auto Commands::bind_command_to_mouse_click(
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

auto Commands::bind_command_to_mouse_wheel(
    Command* const command
) -> erhe::toolkit::Unique_id<Mouse_wheel_binding>::id_type
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    auto binding = std::make_unique<Mouse_wheel_binding>(command);
    auto id = binding->get_id();
    m_mouse_wheel_bindings.push_back(std::move(binding));
    return id;
}

auto Commands::bind_command_to_mouse_motion(
    Command* const command
) -> erhe::toolkit::Unique_id<Mouse_motion_binding>::id_type
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    auto binding = std::make_unique<Mouse_motion_binding>(command);
    auto id = binding->get_id();
    m_mouse_bindings.push_back(std::move(binding));
    return id;
}

auto Commands::bind_command_to_mouse_drag(
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

auto Commands::bind_command_to_controller_trigger_click(
    Command* const command
) -> erhe::toolkit::Unique_id<Controller_trigger_click_binding>::id_type
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    auto binding = std::make_unique<Controller_trigger_click_binding>(command);
    auto id = binding->get_id();
    m_controller_trigger_bindings.push_back(std::move(binding));
    return id;
}

auto Commands::bind_command_to_controller_trigger_drag(
    Command* const command
) -> erhe::toolkit::Unique_id<Controller_trigger_drag_binding>::id_type
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    auto binding = std::make_unique<Controller_trigger_drag_binding>(command);
    auto id = binding->get_id();
    m_controller_trigger_bindings.push_back(std::move(binding));
    return id;
}

auto Commands::bind_command_to_controller_trigger_value(
    Command* const command
) -> erhe::toolkit::Unique_id<Controller_trigger_value_binding>::id_type
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    auto binding = std::make_unique<Controller_trigger_value_binding>(command);
    auto id = binding->get_id();
    m_controller_trigger_bindings.push_back(std::move(binding));
    return id;
}

auto Commands::bind_command_to_controller_trackpad(
    Command* const command,
    const bool     click
) -> erhe::toolkit::Unique_id<Controller_trackpad_binding>::id_type
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    auto& binding = m_controller_trackpad_bindings.emplace_back(
        command,
        click
            ? Command_binding::Type::Controller_trackpad_clicked
            : Command_binding::Type::Controller_trackpad_touched
    );
    return binding.get_id();
}

auto Commands::bind_command_to_update(
    Command* const command
) -> erhe::toolkit::Unique_id<Update_binding>::id_type
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    auto& binding = m_update_bindings.emplace_back(command);
    return binding.get_id();
}

void Commands::remove_command_binding(
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
    m_controller_trigger_bindings.erase(
        std::remove_if(
            m_controller_trigger_bindings.begin(),
            m_controller_trigger_bindings.end(),
            [binding_id](const std::unique_ptr<Controller_trigger_binding>& binding)
            {
                return binding.get()->get_id() == binding_id;
            }
        ),
        m_controller_trigger_bindings.end()
    );
    m_controller_trackpad_bindings.erase(
        std::remove_if(
            m_controller_trackpad_bindings.begin(),
            m_controller_trackpad_bindings.end(),
            [binding_id](const Controller_trackpad_binding& binding)
            {
                return binding.get_id() == binding_id;
            }
        ),
        m_controller_trackpad_bindings.end()
    );
    m_update_bindings.erase(
        std::remove_if(
            m_update_bindings.begin(),
            m_update_bindings.end(),
            [binding_id](const Update_binding& binding)
            {
                return binding.get_id() == binding_id;
            }
        ),
        m_update_bindings.end()
    );
}

void Commands::command_inactivated(Command* const command)
{
    // std::lock_guard<std::mutex> lock{m_command_mutex};

    if (m_active_mouse_command == command)
    {
        m_active_mouse_command = nullptr;
    }
    if (m_active_trigger_command == command)
    {
        m_active_trigger_command = nullptr;
    }
}

void Commands::on_key(
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask,
    const bool                   pressed
)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    Command_context context{*this};

    for (auto& binding : m_key_bindings)
    {
        if (!binding.is_command_host_enabled())
        {
            continue;
        }
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

void Commands::on_update()
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    {
        // TODO OpenXR?
        Command_context context{
            *this,
            m_last_mouse_button_bits,
            m_last_mouse_position,
            glm::vec2{0.0f, 0.0f}
        };

        for (auto& binding : m_update_bindings)
        {
            if (!binding.is_command_host_enabled())
            {
                continue;
            }
            binding.on_update(context);
        }
    }

    // Call mouse drag bindings if buttons are being held down
    if (m_last_mouse_button_bits != 0)
    {
        Command_context context{
            *this,
            m_last_mouse_button_bits,
            m_last_mouse_position,
            glm::vec2{0.0f, 0.0f}
        };

        for (auto& binding : m_mouse_bindings)
        {
            if (!binding->is_command_host_enabled())
            {
                continue;
            }

            if (binding->get_type() == Command_binding::Type::Mouse_drag)
            {
                auto*      drag_binding = reinterpret_cast<Mouse_drag_binding*>(binding.get());
                Command*   command      = binding->get_command();
                const auto state        = command->get_command_state();
                if ((state == State::Ready) || (state == State::Active))
                {
                    const auto     button = drag_binding->get_button();
                    const uint32_t bit    = (1 << button);
                    if ((m_last_mouse_button_bits & bit) == bit)
                    {
                        drag_binding->on_motion(context);
                    }
                }
            }
        }
    }

    if (m_last_controller_trigger_click)
    {
        Command_context context{
            *this,
            m_last_controller_trigger_click ? uint32_t{1} : uint32_t{0},
            glm::vec2{m_last_controller_trackpad_x, m_last_controller_trackpad_y},
            glm::vec2{0.0f, 0.0f}
        };

        for (auto& binding : m_controller_trigger_bindings)
        {
            if (!binding->is_command_host_enabled())
            {
                continue;
            }

            if (binding->get_type() == Command_binding::Type::Controller_trigger_drag)
            {
                auto*    drag_binding = reinterpret_cast<Controller_trigger_drag_binding*>(binding.get());
                Command* command      = binding->get_command();
                if (command->get_command_state() == State::Disabled)
                {
                    continue;
                }

                drag_binding->on_trigger_update(context);
            }
        }
    }
}

auto Commands::get_command_priority(Command* const command) const -> int
{
    auto* host = command->get_host();
    if (host && !host->is_enabled())
    {
        return 0; // Disabled command host -> minimum priority for command
    }

    // Give priority for active mouse / cpntroller trigger commands
    if (command == m_active_mouse_command)
    {
        return 10000; // TODO max priority
    }
    if (command == m_active_trigger_command)
    {
        return 10000; // TODO max priority
    }
    return command->get_priority();
}

void Commands::sort_mouse_bindings()
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
            return get_command_priority(lhs_command) > get_command_priority(rhs_command);
        }
    );
}

void Commands::sort_trigger_bindings()
{
    std::sort(
        m_controller_trigger_bindings.begin(),
        m_controller_trigger_bindings.end(),
        [this](
            const std::unique_ptr<Controller_trigger_binding>& lhs,
            const std::unique_ptr<Controller_trigger_binding>& rhs
        ) -> bool
        {
            auto* const lhs_command = lhs.get()->get_command();
            auto* const rhs_command = rhs.get()->get_command();
            ERHE_VERIFY(lhs_command != nullptr);
            ERHE_VERIFY(rhs_command != nullptr);
            return get_command_priority(lhs_command) > get_command_priority(rhs_command);
        }
    );
}

void Commands::inactivate_ready_commands()
{
    //std::lock_guard<std::mutex> lock{m_command_mutex};

    Command_context context{*this};
    for (auto* command : m_commands)
    {
        if (command->get_command_state() == State::Ready)
        {
            command->set_inactive(context);
        }
    }
}

auto Commands::last_mouse_button_bits() const -> uint32_t
{
    return m_last_mouse_button_bits;
}

auto Commands::last_mouse_position() const -> glm::dvec2
{
    return m_last_mouse_position;
}

auto Commands::last_mouse_position_delta() const -> glm::dvec2
{
    return m_last_mouse_position_delta;
}

auto Commands::last_mouse_wheel_delta() const -> glm::dvec2
{
    return m_last_mouse_wheel_delta;
}

auto Commands::last_controller_trigger_value() const -> float
{
    return m_last_controller_trigger_value;
}

auto Commands::last_controller_trackpad_x() const -> float
{
    return m_last_controller_trackpad_x;
}

auto Commands::last_controller_trackpad_y() const -> float
{
    return m_last_controller_trackpad_y;
}

void Commands::update_active_mouse_command(
    Command* const command
)
{
    inactivate_ready_commands();

    if (
        (command->get_command_state() == State::Active) &&
        (m_active_mouse_command != command)
    )
    {
        ERHE_VERIFY(m_active_mouse_command == nullptr);
        m_active_mouse_command = command;
    }
    else if (
        (command->get_command_state() != State::Active) &&
        (m_active_mouse_command == command)
    )
    {
        m_active_mouse_command = nullptr;
    }
}

void Commands::update_active_trigger_command(
    Command* const command
)
{
    inactivate_ready_commands();

    if (
        (command->get_command_state() == State::Active) &&
        (m_active_trigger_command != command)
    )
    {
        ERHE_VERIFY(m_active_trigger_command == nullptr);
        m_active_trigger_command = command;
    }
    else if (
        (command->get_command_state() != State::Active) &&
        (m_active_trigger_command == command)
    )
    {
        m_active_trigger_command = nullptr;
    }
}

void Commands::on_mouse_click(
    const erhe::toolkit::Mouse_button button,
    const int                         count
)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    sort_mouse_bindings();

    const uint32_t bit_mask = (1 << button);
    if (count == 0)
    {
        m_last_mouse_button_bits = m_last_mouse_button_bits & ~bit_mask;
    }
    else
    {
        m_last_mouse_button_bits = m_last_mouse_button_bits | bit_mask;
    }

    Command_context context{
        *this,
        m_last_mouse_button_bits,
        m_last_mouse_position
    };
    for (const auto& binding : m_mouse_bindings)
    {
        if (!binding->is_command_host_enabled())
        {
            continue;
        }

        auto* const command = binding->get_command();
        ERHE_VERIFY(command != nullptr);
        if (binding->on_button(context, button, count))
        {
            update_active_mouse_command(command);
            break;
        }
    }
}

void Commands::on_mouse_wheel(const double x, const double y)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    sort_mouse_bindings();

    m_last_mouse_wheel_delta.x = x;
    m_last_mouse_wheel_delta.y = y;

    Command_context context{
        *this,
        m_last_mouse_button_bits,
        m_last_mouse_position,
        m_last_mouse_wheel_delta
    };
    for (const auto& binding : m_mouse_wheel_bindings)
    {
        if (!binding->is_command_host_enabled())
        {
            continue;
        }

        auto* const command = binding->get_command();
        ERHE_VERIFY(command != nullptr);
        binding->on_wheel(context); // does not set active mouse command - each wheel event is one shot
    }
}

void Commands::on_mouse_move(const double x, const double y)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    glm::dvec2 new_mouse_position{x, y};
    m_last_mouse_position_delta = m_last_mouse_position - new_mouse_position;

    m_last_mouse_position = new_mouse_position;
    Command_context context{
        *this,
        m_last_mouse_button_bits,
        m_last_mouse_position,
        m_last_mouse_position_delta
    };

    for (const auto& binding : m_mouse_bindings)
    {
        if (!binding->is_command_host_enabled())
        {
            continue;
        }

        auto* const command = binding->get_command();
        ERHE_VERIFY(command != nullptr);
        if (binding->on_motion(context))
        {
            update_active_mouse_command(command);
            break;
        }
    }
}

void Commands::on_controller_trigger_click(
    const bool click
)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    sort_trigger_bindings();

    m_last_controller_trigger_click = click;

    Command_context context{
        *this,
        (click ? uint32_t{1} : uint32_t{0}),
        glm::dvec2{0.0, 0.0}
    };

    for (auto& binding : m_controller_trigger_bindings)
    {
        if (!binding->is_command_host_enabled())
        {
            continue;
        }

        auto* const command = binding->get_command();
        ERHE_VERIFY(command != nullptr);
        if (binding->on_trigger_click(context, click))
        {
            update_active_trigger_command(command);
            break;
        }
    }

    log_input->trace("trigger click was not handled");
}

void Commands::on_controller_trigger_value(
    const float trigger_value
)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    sort_trigger_bindings();

    Command_context context{
        *this,
        0,
        glm::dvec2{trigger_value, 0.0}
    };

    for (auto& binding : m_controller_trigger_bindings)
    {
        if (!binding->is_command_host_enabled())
        {
            continue;
        }

        binding->on_trigger_value(context);
    }
}

void Commands::on_controller_trackpad_touch(
    const float x,
    const float y,
    const bool  touch
)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    m_last_controller_trackpad_touch = touch;

    Command_context context{
        *this,
        touch ? uint32_t{1} : uint32_t{0},
        glm::dvec2{x, y}
    };

    for (auto& binding : m_controller_trackpad_bindings)
    {
        if (!binding.is_command_host_enabled())
        {
            continue;
        }

        if (binding.get_type() == Command_binding::Type::Controller_trackpad_touched)
        {
            auto* command = binding.get_command();
            if (command != nullptr)
            {
                binding.on_trackpad(context);
            }
        }
    }
}

void Commands::on_controller_trackpad_click(
    const float x,
    const float y,
    const bool  click
)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    m_last_controller_trackpad_click = click;

    Command_context context{
        *this,
        click ? uint32_t{1} : uint32_t{0},
        glm::dvec2{x, y}
    };

    for (auto& binding : m_controller_trackpad_bindings)
    {
        if (!binding.is_command_host_enabled())
        {
            continue;
        }

        if (binding.get_type() == Command_binding::Type::Controller_trackpad_clicked)
        {
            auto* command = binding.get_command();
            if (command != nullptr)
            {
                binding.on_trackpad(context);
            }
        }
    }
}

void Commands::commands(const State filter)
{
    for (auto* command : m_commands)
    {
        if (command->get_command_state() == filter)
        {
            const auto* host = command->get_host();
            const std::string label = fmt::format(
                "{} {} : {} {}",
                command->get_priority(),
                command->get_name(),
                host ? host->get_description() : "",
                host ? host->get_priority() : 0
            );
            ImGui::TreeNodeEx(
                label.c_str(),
                ImGuiTreeNodeFlags_NoTreePushOnOpen |
                ImGuiTreeNodeFlags_Leaf
            );
        }
    }
}

void Commands::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    std::lock_guard<std::mutex> lock{m_command_mutex};

    ImGui::Text(
        "Active mouse command: %s",
        (m_active_mouse_command != nullptr)
            ? m_active_mouse_command->get_name()
            : "(none)"
    );
    ImGui::Text(
        "Active trigger command: %s",
        (m_active_trigger_command != nullptr)
            ? m_active_trigger_command->get_name()
            : "(none)"
    );

    if (ImGui::TreeNodeEx("Commands", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::TreeNodeEx("Active", ImGuiTreeNodeFlags_DefaultOpen))
        {
            commands(State::Active);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Ready", ImGuiTreeNodeFlags_DefaultOpen))
        {
            commands(State::Ready);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Inactive", ImGuiTreeNodeFlags_DefaultOpen))
        {
            commands(State::Inactive);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Disabled", ImGuiTreeNodeFlags_DefaultOpen))
        {
            commands(State::Disabled);
            ImGui::TreePop();
        }

        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Registered Commands"))
    {
        for (const auto& command : m_commands)
        {
            ImGui::Text("%d %s", command->get_priority(), command->get_name());
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Key Bindings"))
    {
        for (const auto& binding : m_key_bindings)
        {
            ImGui::Text(
                "%d %s <- %s %s %s",
                binding.get_command()->get_priority(),
                binding.get_command()->get_name(),
                Command_binding::c_type_strings[static_cast<int>(binding.get_type())],
                erhe::toolkit::c_str(binding.get_keycode()),
                binding.get_pressed()
                    ? "pressed"
                    : "released"
            );
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Update Bindings"))
    {
        for (const auto& binding : m_update_bindings)
        {
            ImGui::Text(
                "%d %s <- %s",
                binding.get_command()->get_priority(),
                binding.get_command()->get_name(),
                Command_binding::c_type_strings[static_cast<int>(binding.get_type())]
            );
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Mouse Bindings"))
    {
        {
            for (const auto& binding : m_mouse_bindings)
            {
                const auto type = binding->get_type();
                switch (type)
                {
                    default:
                    {
                        break;
                    }
                    case erhe::application::Command_binding::Type::Mouse_click:
                    {
                        const auto click_binding = reinterpret_cast<erhe::application::Mouse_click_binding*>(binding.get());
                        ImGui::Text(
                            "%d %s <- %s %d",
                            binding->get_command()->get_priority(),
                            binding->get_command()->get_name(),
                            Command_binding::c_type_strings[static_cast<int>(binding->get_type())],
                            click_binding->get_button()
                        );
                        break;
                    }
                    case erhe::application::Command_binding::Type::Mouse_drag:
                    {
                        const auto drag_binding = reinterpret_cast<erhe::application::Mouse_drag_binding*>(binding.get());
                        ImGui::Text(
                            "%d %s <- %s %d",
                            binding->get_command()->get_priority(),
                            binding->get_command()->get_name(),
                            Command_binding::c_type_strings[static_cast<int>(binding->get_type())],
                            drag_binding->get_button()
                        );
                        break;
                    }
                    case erhe::application::Command_binding::Type::Mouse_motion:
                    {
                        ImGui::Text(
                            "%d %s ,_ %s",
                            binding->get_command()->get_priority(),
                            binding->get_command()->get_name(),
                            Command_binding::c_type_strings[static_cast<int>(binding->get_type())]
                        );
                        break;
                    }
                }

            }
        }
        {
            for (const auto& binding : m_mouse_wheel_bindings)
            {
                ImGui::Text(
                    "%d %s <- %s",
                    binding->get_command()->get_priority(),
                    binding->get_command()->get_name(),
                    Command_binding::c_type_strings[static_cast<int>(binding->get_type())]
                );
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Controller Trackpad Bindings"))
    {
        for (const auto& binding : m_controller_trackpad_bindings)
        {
            ImGui::Text(
                "%d %s <- %s",
                binding.get_command()->get_priority(),
                binding.get_command()->get_name(),
                Command_binding::c_type_strings[static_cast<int>(binding.get_type())]
            );
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Controller Trigger Bindings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (const auto& binding : m_controller_trigger_bindings)
        {
            ImGui::Text(
                "%d %s: <- %s",
                binding->get_command()->get_priority(),
                binding->get_command()->get_name(),
                Command_binding::c_type_strings[static_cast<int>(binding->get_type())]
            );
        }
        ImGui::TreePop();
    }

#endif
}

void Commands::sort_bindings()
{
    sort_mouse_bindings();
    sort_trigger_bindings();
}

}  // namespace editor
