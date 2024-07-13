// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_commands/commands.hpp"
#include "erhe_commands/commands_log.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_commands/mouse_button_binding.hpp"
#include "erhe_commands/mouse_drag_binding.hpp"
#include "erhe_commands/mouse_motion_binding.hpp"
#include "erhe_commands/mouse_wheel_binding.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "erhe_commands/xr_boolean_binding.hpp"
#   include "erhe_commands/xr_float_binding.hpp"
#   include "erhe_commands/xr_vector2f_binding.hpp"
#   include "erhe_xr/xr_action.hpp"
#endif
#include "erhe_commands/update_binding.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::commands {

Commands::Commands()
{
}

Commands::~Commands() noexcept
{
    m_commands.clear();
    // TODO Are any of these actually needed?
    m_key_bindings.clear();
    m_mouse_bindings.clear();
    m_mouse_wheel_bindings.clear();
    m_controller_axis_bindings.clear();
    m_controller_button_bindings.clear();
#if defined(ERHE_XR_LIBRARY_OPENXR)
    m_xr_boolean_bindings.clear();
    m_xr_float_bindings.clear();
    m_xr_vector2f_bindings.clear();
#endif
    m_update_bindings.clear();
}

void Commands::register_command(Command* const command)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    m_commands.push_back(command);
}

auto Commands::get_commands() const -> const std::vector<Command*>&
{
    return m_commands;
}

auto Commands::get_key_bindings() const -> const std::vector<Key_binding>&
{
    return m_key_bindings;
}

auto Commands::get_menu_bindings() const -> const std::vector<Menu_binding>&
{
    return m_menu_bindings;
}

auto Commands::get_mouse_bindings() const -> const std::vector<std::unique_ptr<Mouse_binding>>&
{
    return m_mouse_bindings;
}

auto Commands::get_mouse_wheel_bindings() const -> const std::vector<std::unique_ptr<Mouse_wheel_binding>>&
{
    return m_mouse_wheel_bindings;
}

auto Commands::get_controller_axis_bindings() const -> const std::vector<Controller_axis_binding>&
{
    return m_controller_axis_bindings;
}

auto Commands::get_controller_button_bindings() const -> const std::vector<Controller_button_binding>&
{
    return m_controller_button_bindings;
}

#if defined(ERHE_XR_LIBRARY_OPENXR)
auto Commands::get_xr_boolean_bindings() const -> const std::vector<Xr_boolean_binding>&
{
    return m_xr_boolean_bindings;
}

auto Commands::get_xr_float_bindings() const -> const std::vector<Xr_float_binding>&
{
    return m_xr_float_bindings;
}

auto Commands::get_xr_vector2f_bindings() const -> const std::vector<Xr_vector2f_binding>&
{
    return m_xr_vector2f_bindings;
}
#endif

auto Commands::get_update_bindings() const -> const std::vector<Update_binding>&
{
    return m_update_bindings;
}

void Commands::bind_command_to_menu(Command* command, std::string_view menu_path, std::function<bool()> enabled_callback)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};
    m_menu_bindings.emplace_back(command, menu_path, enabled_callback);
}

void Commands::bind_command_to_key(
    Command* const                command,
    const erhe::window::Keycode   code,
    const bool                    pressed,
    const std::optional<uint32_t> modifier_mask
)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};
    m_key_bindings.emplace_back(command, code, pressed, modifier_mask);
}

void Commands::bind_command_to_mouse_button(
    Command* const                   command,
    const erhe::window::Mouse_button button,
    const bool                       trigger_on_pressed,
    const std::optional<uint32_t>    modifier_mask
)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};
    m_mouse_bindings.push_back(
        std::make_unique<Mouse_button_binding>(command, button, trigger_on_pressed, modifier_mask)
    );
}

void Commands::bind_command_to_mouse_wheel(Command* const command, const std::optional<uint32_t> modifier_mask)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};
    m_mouse_wheel_bindings.push_back(
        std::make_unique<Mouse_wheel_binding>(command, modifier_mask)
    );
}

void Commands::bind_command_to_mouse_motion(Command* const command, const std::optional<uint32_t> modifier_mask)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};
    m_mouse_bindings.push_back(
        std::make_unique<Mouse_motion_binding>(command, modifier_mask)
    );
}

void Commands::bind_command_to_mouse_drag(
    Command* const                   command,
    const erhe::window::Mouse_button button,
    const bool                       call_on_button_down_without_motion,
    const std::optional<uint32_t>    modifier_mask
)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};
    m_mouse_bindings.push_back(
        std::make_unique<Mouse_drag_binding>(command, button, call_on_button_down_without_motion, modifier_mask)
    );
}

void Commands::bind_command_to_controller_axis(Command* command, const int axis, std::optional<uint32_t> modifier_mask)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};
    m_controller_axis_bindings.emplace_back(command, axis, modifier_mask);
}

void Commands::bind_command_to_controller_button(
    Command* const                   command,
    const erhe::window::Mouse_button button,
    const Button_trigger             button_trigger,
    const std::optional<uint32_t>    modifier_mask
)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};
    m_controller_button_bindings.emplace_back(command, button, button_trigger, modifier_mask);
}

#if defined(ERHE_XR_LIBRARY_OPENXR)
void Commands::bind_command_to_xr_boolean_action(
    Command* const                     command,
    erhe::xr::Xr_action_boolean* const xr_action,
    const Button_trigger               button_trigger
)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};
    m_xr_boolean_bindings.emplace_back(command, xr_action, button_trigger);
}

void Commands::bind_command_to_xr_float_action(Command* const command, erhe::xr::Xr_action_float* const xr_action)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};
    m_xr_float_bindings.emplace_back(command, xr_action);
}

void Commands::bind_command_to_xr_vector2f_action(Command* const command, erhe::xr::Xr_action_vector2f* const xr_action)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};
    m_xr_vector2f_bindings.emplace_back(command, xr_action);
}
#endif

void Commands::bind_command_to_update(Command* const command)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};
    m_update_bindings.emplace_back(command);
}

//void Commands::remove_command_binding(
//    const erhe::Unique_id<Command_binding>::id_type binding_id
//)
//{
//    std::lock_guard<std::mutex> lock{m_command_mutex};
//
//    m_key_bindings.erase(
//        std::remove_if(
//            m_key_bindings.begin(),
//            m_key_bindings.end(),
//            [binding_id](const Key_binding& binding) {
//                return binding.get_id() == binding_id;
//            }
//        ),
//        m_key_bindings.end()
//    );
//    m_mouse_bindings.erase(
//        std::remove_if(
//            m_mouse_bindings.begin(),
//            m_mouse_bindings.end(),
//            [binding_id](const std::unique_ptr<Mouse_binding>& binding) {
//                return binding.get()->get_id() == binding_id;
//            }
//        ),
//        m_mouse_bindings.end()
//    );
//    m_mouse_wheel_bindings.erase(
//        std::remove_if(
//            m_mouse_wheel_bindings.begin(),
//            m_mouse_wheel_bindings.end(),
//            [binding_id](const std::unique_ptr<Mouse_wheel_binding>& binding) {
//                return binding.get()->get_id() == binding_id;
//            }
//        ),
//        m_mouse_wheel_bindings.end()
//    );
//#if defined(ERHE_XR_LIBRARY_OPENXR)
//    m_xr_boolean_bindings.erase(
//        std::remove_if(
//            m_xr_boolean_bindings.begin(),
//            m_xr_boolean_bindings.end(),
//            [binding_id](const Xr_boolean_binding& binding) {
//                return binding.get_id() == binding_id;
//            }
//        ),
//        m_xr_boolean_bindings.end()
//    );
//    m_xr_float_bindings.erase(
//        std::remove_if(
//            m_xr_float_bindings.begin(),
//            m_xr_float_bindings.end(),
//            [binding_id](const Xr_float_binding& binding) {
//                return binding.get_id() == binding_id;
//            }
//        ),
//        m_xr_float_bindings.end()
//    );
//    m_xr_vector2f_bindings.erase(
//        std::remove_if(
//            m_xr_vector2f_bindings.begin(),
//            m_xr_vector2f_bindings.end(),
//            [binding_id](const Xr_vector2f_binding& binding) {
//                return binding.get_id() == binding_id;
//            }
//        ),
//        m_xr_vector2f_bindings.end()
//    );
//#endif
//    m_update_bindings.erase(
//        std::remove_if(
//            m_update_bindings.begin(),
//            m_update_bindings.end(),
//            [binding_id](const Update_binding& binding) {
//                return binding.get_id() == binding_id;
//            }
//        ),
//        m_update_bindings.end()
//    );
//}

void Commands::command_inactivated(Command* const command)
{
    // std::lock_guard<std::mutex> lock{m_command_mutex};

    if (m_active_mouse_command == command) {
        m_active_mouse_command = nullptr;
    }
}

auto Commands::has_active_mouse() const -> bool
{
    return m_active_mouse_command != nullptr;
}

auto Commands::on_key(const erhe::window::Keycode code, const uint32_t modifier_mask, const bool pressed) -> bool
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    m_last_modifier_mask = modifier_mask;

    Input_arguments context;

    for (auto& binding : m_key_bindings) {
        if (!binding.is_command_host_enabled()) {
            continue;
        }
        if (binding.on_key(context, pressed, code, modifier_mask)) {
            return true;
        }
    }

    log_input_event_filtered->trace(
        "key {} {} not consumed",
        erhe::window::c_str(code),
        pressed ? "press" : "release"
    );
    return false;
}

auto Commands::on_idle() -> bool
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    for (auto& binding : m_update_bindings) {
        if (!binding.is_command_host_enabled()) {
            continue;
        }
        binding.on_update();
    }

    // Call mouse drag bindings if buttons are being held down
    if (m_last_mouse_button_bits != 0) {
        Input_arguments dummy_input{
            .modifier_mask = m_last_modifier_mask,
            .variant = {
                .vector2{
                    .absolute_value{0.0f, 0.0f},
                    .relative_value{0.0f, 0.0f}
                }
            }
        };

        for (auto& binding : m_mouse_bindings) {
            if (!binding->is_command_host_enabled()) {
                continue;
            }

            if (binding->get_type() == Command_binding::Type::Mouse_drag) {
                auto*      drag_binding = static_cast<Mouse_drag_binding*>(binding.get());
                Command*   command      = binding->get_command();
                const auto state        = command->get_command_state();
                if ((state == State::Ready) || (state == State::Active)) {
                    const auto     button = drag_binding->get_button();
                    const uint32_t bit    = (1 << button);
                    if ((m_last_mouse_button_bits & bit) == bit) {
                        drag_binding->on_motion(dummy_input);
                    }
                }
            }
        }
    }

    return false;
}

auto Commands::get_command_priority(Command* const command) const -> int
{
    auto* host = command->get_host();
    if (host && !host->is_enabled()) {
        return 0; // Disabled command host -> minimum priority for command
    }

    // Give priority for active mouse / cpntroller trigger commands
    if (command == m_active_mouse_command) {
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
        ) -> bool {
            auto* const lhs_command = lhs.get()->get_command();
            auto* const rhs_command = rhs.get()->get_command();
            ERHE_VERIFY(lhs_command != nullptr);
            ERHE_VERIFY(rhs_command != nullptr);
            const auto lhs_priority = get_command_priority(lhs_command);
            const auto rhs_priority = get_command_priority(rhs_command);
            // log_input->trace(
            //     "lhs = {} {}, rhs = {} {}, is_higher = {}",
            //     lhs_command->get_name(), lhs_priority,
            //     rhs_command->get_name(), rhs_priority,
            //     is_higher
            // );

            // Sort higher priority first
            if (lhs_priority != rhs_priority) {
                return lhs_priority > rhs_priority;
            }

            const std::optional<uint32_t> lmask = lhs->get_modifier_mask();
            const std::optional<uint32_t> rmask = rhs->get_modifier_mask();
            // Sort one with modifiers set first
            if (lmask.has_value() && !rmask.has_value()) {
                return true;
            }
            if (!lmask.has_value()) {
                return false;
            }
            // Sort one with more bits set first
            const uint32_t lmask_value = lmask.value();
            const uint32_t rmask_value = rmask.value();
            return (lmask_value != rmask_value) && (lmask_value & rmask_value) == rmask_value;
        }
    );
    // log_input->trace("Mouse bindings after sort:");
    // for (const auto& binding : m_mouse_bindings) {
    //     auto* const command = binding->get_command();
    //     const auto priority = get_command_priority(command);
    //     log_input->trace("{} {}", priority, command->get_name());
    // }
}

void Commands::sort_controller_bindings()
{
    std::sort(
        m_controller_axis_bindings.begin(),
        m_controller_axis_bindings.end(),
        [this](const Controller_axis_binding& lhs, const Controller_axis_binding& rhs) -> bool {
            auto* const lhs_command = lhs.get_command();
            auto* const rhs_command = rhs.get_command();
            ERHE_VERIFY(lhs_command != nullptr);
            ERHE_VERIFY(rhs_command != nullptr);
            return get_command_priority(lhs_command) > get_command_priority(rhs_command);
        }
    );
    std::sort(
        m_controller_button_bindings.begin(),
        m_controller_button_bindings.end(),
        [this](const Controller_button_binding& lhs, const Controller_button_binding& rhs) -> bool {
            auto* const lhs_command = lhs.get_command();
            auto* const rhs_command = rhs.get_command();
            ERHE_VERIFY(lhs_command != nullptr);
            ERHE_VERIFY(rhs_command != nullptr);
            return get_command_priority(lhs_command) > get_command_priority(rhs_command);
        }
    );
}

#if defined(ERHE_XR_LIBRARY_OPENXR)
void Commands::sort_xr_bindings()
{
    std::sort(
        m_xr_boolean_bindings.begin(),
        m_xr_boolean_bindings.end(),
        [this](const Xr_boolean_binding& lhs, const Xr_boolean_binding& rhs) -> bool {
            auto* const lhs_command = lhs.get_command();
            auto* const rhs_command = rhs.get_command();
            ERHE_VERIFY(lhs_command != nullptr);
            ERHE_VERIFY(rhs_command != nullptr);
            return get_command_priority(lhs_command) > get_command_priority(rhs_command);
        }
    );
    std::sort(
        m_xr_float_bindings.begin(),
        m_xr_float_bindings.end(),
        [this](const Xr_float_binding& lhs, const Xr_float_binding& rhs) -> bool {
            auto* const lhs_command = lhs.get_command();
            auto* const rhs_command = rhs.get_command();
            ERHE_VERIFY(lhs_command != nullptr);
            ERHE_VERIFY(rhs_command != nullptr);
            return get_command_priority(lhs_command) > get_command_priority(rhs_command);
        }
    );
    std::sort(
        m_xr_vector2f_bindings.begin(),
        m_xr_vector2f_bindings.end(),
        [this]( const Xr_vector2f_binding& lhs, const Xr_vector2f_binding& rhs) -> bool {
            auto* const lhs_command = lhs.get_command();
            auto* const rhs_command = rhs.get_command();
            ERHE_VERIFY(lhs_command != nullptr);
            ERHE_VERIFY(rhs_command != nullptr);
            return get_command_priority(lhs_command) > get_command_priority(rhs_command);
        }
    );
}
#endif

void Commands::inactivate_ready_commands()
{
    //std::lock_guard<std::mutex> lock{m_command_mutex};

    for (auto* command : m_commands) {
        if (command->get_command_state() == State::Ready) {
            command->set_inactive();
        }
    }
}

auto Commands::last_mouse_button_bits() const -> uint32_t
{
    return m_last_mouse_button_bits;
}

auto Commands::last_mouse_position() const -> glm::vec2
{
    return m_last_mouse_position;
}

auto Commands::last_mouse_position_delta() const -> glm::vec2
{
    return m_last_mouse_position_delta;
}

void Commands::update_active_mouse_command(Command* const command)
{
    inactivate_ready_commands();

    if (
        (command->get_command_state() == State::Active) &&
        (m_active_mouse_command != command)
    ) {
        ERHE_VERIFY(m_active_mouse_command == nullptr);
        log_input->trace("Set active mouse command = {}", command->get_name());
        m_active_mouse_command = command;
    } else if (
        (command->get_command_state() != State::Active) &&
        (m_active_mouse_command == command)
    ) {
        log_input->trace("reset active mouse command");
        m_active_mouse_command = nullptr;
    }
}

auto Commands::on_mouse_button(const erhe::window::Mouse_button button, const bool pressed, const uint32_t modifier_mask) -> bool
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    m_last_modifier_mask = modifier_mask;

    sort_mouse_bindings();

    const uint32_t bit_mask = (1 << button);
    if (pressed) {
        m_last_mouse_button_bits = m_last_mouse_button_bits | bit_mask;
    } else {
        m_last_mouse_button_bits = m_last_mouse_button_bits & ~bit_mask;
    }

    Input_arguments input{
        .modifier_mask = modifier_mask,
        .variant = {
            .button_pressed = pressed
        }
    };

    const char* button_name = erhe::window::c_str(button);
    log_input->trace("Mouse button {} {}", button_name, pressed ? "pressed" : "released");
    for (const auto& binding : m_mouse_bindings) {
        const bool button_match = binding->get_button() == button;
        log_input->trace(
            "  {}/{} {} {} {}",
            binding->get_command()->get_priority(),
            get_command_priority(binding->get_command()),
            binding->is_command_host_enabled() ? "host enabled" : "host disabled",
            binding->get_command()->get_name(),
            button_match ? "button match" : "button mismatch"
        );
        if (!button_match) {
            continue;
        }
        auto* const command = binding->get_command();
        ERHE_VERIFY(command != nullptr);
        if (!binding->is_command_host_enabled()) {
            log_input->trace("Mouse button {} {} host not enabled {}", button_name, pressed, command->get_name());
            continue;
        }

        const bool consumed = binding->on_button(input);
        if (binding->get_command()->get_command_state() == State::Active) {
            update_active_mouse_command(command);
        }

        if (consumed) {
            log_input->trace("Mouse button {} {} consumed by {}", button_name, pressed, command->get_name());
            return true;
        } else {
            log_input->trace("Mouse button {} {} not consumed by {}", button_name, pressed, command->get_name());
        }
    }
    log_input->trace("Mouse button {} {} was not consumed", button_name, pressed);
    return false;
}

auto Commands::on_mouse_wheel(const float x, const float y, const uint32_t modifier_mask) -> bool
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    m_last_modifier_mask = modifier_mask;

    sort_mouse_bindings();

    Input_arguments input{
        .modifier_mask = modifier_mask,
        .variant = {
            .vector2 {
                .absolute_value = glm::vec2{x, y},
                .relative_value = glm::vec2{x, y}
            }
        }
    };
    for (const auto& binding : m_mouse_wheel_bindings) {
        if (!binding->is_command_host_enabled()) {
            continue;
        }

        auto* const command = binding->get_command();
        ERHE_VERIFY(command != nullptr);

        if (binding->on_wheel(input)) { // does not set active mouse command - each wheel event is one shot
            return true;
        }
    }
    return false;
}

auto Commands::on_controller_axis(const int axis, const float value, const uint32_t modifier_mask) -> bool
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    m_last_modifier_mask = modifier_mask;

    sort_controller_bindings();

    Input_arguments input{
        .modifier_mask = modifier_mask,
        .variant = {
            .float_value = value
        }
    };

    for (auto& binding : m_controller_axis_bindings) {
        if (binding.get_axis() != axis) {
            continue;
        }
        if (!binding.is_command_host_enabled()) {
            continue;
        }

        auto* const command = binding.get_command();
        ERHE_VERIFY(command != nullptr);
        if (binding.on_value_changed(input)) {
            return true;
        }
    }

    log_input->trace("Controller axis input action was not consumed");
    return false;
}

auto Commands::on_controller_button(const int button, const bool value, const uint32_t modifier_mask) -> bool
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    m_last_modifier_mask = modifier_mask;

    Input_arguments input{
        .variant = {
            .button_pressed = value
        }
    };

    log_input->trace("controller_button {}: {}", button, value ? "pressed" : "released");
    for (auto& binding : m_controller_button_bindings) {
        if (binding.get_button() != button) {
            continue;
        }
        //log_input->trace(
        //    "  {} {} {} <- {} {}",
        //    binding.get_command()->get_priority(),
        //    binding.is_command_host_enabled() ? "host enabled" : "host disabled",
        //    binding.get_command()->get_name(),
        //    binding.xr_action->name.c_str(),
        //    Command_binding::c_type_strings[static_cast<int>(binding.get_type())]
        //);
        if (!binding.is_command_host_enabled()) {
            continue;
        }

        auto* const command = binding.get_command();
        ERHE_VERIFY(command != nullptr);
        if (binding.on_value_changed(input)) {
            log_input->trace("controller button {} {} consumed by {}", button, value, command->get_name());
            return true;
        }
    }

    log_input->trace("controller button {} {} was not consumed", button, value);
    return false;
}

auto Commands::on_mouse_move(float absolute_x, float absolute_y, const float relative_x, const float relative_y, const uint32_t modifier_mask) -> bool
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    m_last_modifier_mask = modifier_mask;

    glm::vec2 new_mouse_position{absolute_x, absolute_y};
    m_last_mouse_position_delta = glm::vec2{relative_x, relative_y}; //m_last_mouse_position - new_mouse_position;

    m_last_mouse_position = new_mouse_position;
    Input_arguments input{
        .modifier_mask = modifier_mask,
        .variant = {
            .vector2{
                .absolute_value = new_mouse_position,
                .relative_value = m_last_mouse_position_delta
            }
        }
    };

    for (const auto& binding : m_mouse_bindings) {
        if (!binding->is_command_host_enabled()) {
            continue;
        }

        auto* const command = binding->get_command();
        ERHE_VERIFY(command != nullptr);
        // Mouse motion bindings never return "consumed".
        binding->on_motion(input);

        // However, the commands may transition to "active" state
        if (binding->get_command()->get_command_state() == State::Active) {
            update_active_mouse_command(command);
        }
    }
    return false;
}

#if defined(ERHE_XR_LIBRARY_OPENXR)
void Commands::on_xr_action(erhe::xr::Xr_action_boolean& xr_action)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    sort_xr_bindings();

    const bool state = xr_action.state.currentState == XR_TRUE;
    Input_arguments input{
        .variant = {
            .button_pressed = state
        }
    };

    log_input->trace("{}: {}", xr_action.name, state);
    for (auto& binding : m_xr_boolean_bindings) {
        if (binding.xr_action != &xr_action) {
            continue;
        }
        log_input->trace(
            "  {} {} {} <- {} {}",
            binding.get_command()->get_priority(),
            binding.is_command_host_enabled() ? "host enabled" : "host disabled",
            binding.get_command()->get_name(),
            binding.xr_action->name.c_str(),
            Command_binding::c_type_strings[static_cast<int>(binding.get_type())]
        );
        if (!binding.is_command_host_enabled()) {
            continue;
        }

        auto* const command = binding.get_command();
        ERHE_VERIFY(command != nullptr);
        if (binding.on_value_changed(input)) {
            log_input->trace("XR bool {} consumed by {}", state, command->get_name());
            return;
        }
    }

    log_input->trace("OpenXR bool {} was not consumed", state);
}

void Commands::on_xr_action(erhe::xr::Xr_action_float& xr_action)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    sort_xr_bindings();

    Input_arguments input{
        .variant = {
            .float_value = xr_action.state.currentState
        }
    };

    for (auto& binding : m_xr_float_bindings) {
        if (binding.xr_action != &xr_action) {
            continue;
        }
        if (!binding.is_command_host_enabled()) {
            continue;
        }

        auto* const command = binding.get_command();
        ERHE_VERIFY(command != nullptr);
        if (binding.on_value_changed(input)) {
            return;
        }
    }

    log_input->trace("OpenXR float input action was not consumed");
}

void Commands::on_xr_action(erhe::xr::Xr_action_vector2f& xr_action)
{
    std::lock_guard<std::mutex> lock{m_command_mutex};

    sort_xr_bindings();

    Input_arguments context{
        .variant = {
            .vector2{
                .absolute_value = glm::vec2{
                    xr_action.state.currentState.x,
                    xr_action.state.currentState.y
                },
                .relative_value = glm::vec2{0.0f, 0.0f}
            }
        }
    };

    for (auto& binding : m_xr_vector2f_bindings) {
        if (binding.xr_action != &xr_action) {
            continue;
        }
        if (!binding.is_command_host_enabled()) {
            continue;
        }

        auto* const command = binding.get_command();
        ERHE_VERIFY(command != nullptr);
        if (binding.on_value_changed(context)) {
            log_input->trace("XR vector2f consumed by {}", command->get_name());
            return;
        }
    }

    log_input->trace("OpenXR vector2f input action was not consumed");
}
#endif

void Commands::sort_bindings()
{
    sort_mouse_bindings();
}

}  // erhe::commands
