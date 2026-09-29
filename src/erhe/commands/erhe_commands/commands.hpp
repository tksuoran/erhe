#pragma once

#include "erhe_commands/binding_desc.hpp"
#include "erhe_commands/controller_axis_binding.hpp"
#include "erhe_commands/controller_button_binding.hpp"
#include "erhe_commands/key_binding.hpp"
#include "erhe_commands/menu_binding.hpp"
#include "erhe_commands/mouse_binding.hpp"
#include "erhe_commands/mouse_wheel_binding.hpp"
#include "erhe_commands/update_binding.hpp"
#include "erhe_commands/xr_boolean_binding.hpp"
#include "erhe_commands/xr_float_binding.hpp"
#include "erhe_commands/xr_vector2f_binding.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_window/window_event_handler.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::xr {
    class Xr_instance;
    class Xr_action_boolean;
    class Xr_action_float;
    class Xr_action_vector2f;
    class Xr_action_pose;
}

namespace erhe::commands {

class Command;
class Command_binding;
class Xr_boolean_binding;
class Xr_float_binding;
class Xr_vector2f_binding;
class Key_binding;
class Menu_binding;
class Mouse_binding;
class Mouse_button_binding;
class Mouse_drag_binding;
class Mouse_motion_binding;
class Mouse_wheel_binding;
class Update_binding;

// A user's replacement for the default bindings of one command, keyed by the
// command name (persistence form).
class Binding_override
{
public:
    std::string               command_name;
    std::vector<Binding_desc> bindings;
};

// Two commands with bindings that some input event would fire both
// (Binding_desc::overlaps()). Host priority decides which one consumes the
// event; the pair is reported so the UI can show it.
class Binding_conflict
{
public:
    Command*     command;
    Command*     other_command;
    Binding_desc binding;
};

// Bindings of the key, mouse (button, drag, motion, wheel) and controller
// (axis, button) kinds are user-editable. The bind_command_to_*() calls
// declare a command's default bindings; set_binding_override() replaces all
// bindings of one command. The dispatch tables are derived state, rebuilt
// from "override if present, else defaults" at the start of the next tick()
// (or by sort_bindings()) after any change - never per frame. Menu, update and
// XR bindings are not editable and are unaffected.
//
// tick() runs commands with the command mutex held, and a command may call
// back into Commands on the same thread (e.g. a tool switch calls
// sort_bindings()), so the mutex is recursive. While tick() dispatches, the
// dispatch tables are being iterated: sort_bindings() then only records the
// request, and tick() applies it between events.
class Commands : public erhe::window::Input_event_handler
{
public:
    ~Commands() noexcept override;

    // Public API

    // Command names are unique: they identify the command in persisted
    // binding overrides. Registering a second command with the same name is
    // a fatal error.
    void register_command(Command* command);
    void sort_bindings   ();

    [[nodiscard]] auto get_commands                  () const -> const std::vector<Command*>&;
    [[nodiscard]] auto find_command                  (std::string_view name) const -> Command*;
    [[nodiscard]] auto get_key_bindings              () const -> const std::vector<Key_binding>&;
    [[nodiscard]] auto get_menu_bindings             () const -> const std::vector<Menu_binding>&;
    [[nodiscard]] auto get_mouse_bindings            () const -> const std::vector<std::unique_ptr<Mouse_binding>>&;
    [[nodiscard]] auto get_mouse_wheel_bindings      () const -> const std::vector<std::unique_ptr<Mouse_wheel_binding>>&;
    [[nodiscard]] auto get_controller_axis_bindings  () const -> const std::vector<Controller_axis_binding>&;
    [[nodiscard]] auto get_controller_button_bindings() const -> const std::vector<Controller_button_binding>&;
    [[nodiscard]] auto get_xr_boolean_bindings       () const -> const std::vector<Xr_boolean_binding>&;
    [[nodiscard]] auto get_xr_float_bindings         () const -> const std::vector<Xr_float_binding>&;
    [[nodiscard]] auto get_xr_vector2f_bindings      () const -> const std::vector<Xr_vector2f_binding>&;
    [[nodiscard]] auto get_update_bindings           () const -> const std::vector<Update_binding>&;

    void bind_command_to_key(
        Command*                command,
        erhe::window::Keycode   code,
        Button_trigger          trigger       = Button_trigger::Button_pressed,
        std::optional<uint32_t> modifier_mask = {}
    );

    void bind_command_to_mouse_button(
        Command*                   command,
        erhe::window::Mouse_button button,
        Button_trigger             trigger,
        std::optional<uint32_t>    modifier_mask = {}
    );

    void bind_command_to_mouse_wheel(Command* command, std::optional<uint32_t> modifier_mask = {});
    void bind_command_to_mouse_motion(Command* command, std::optional<uint32_t> modifier_mask = {});

    void bind_command_to_menu(Command* command, std::string_view menu_path, std::function<bool()> enabled_callback = {});

    void bind_command_to_controller_axis(Command* command, int axis, std::optional<uint32_t> modifier_mask = {});
    void bind_command_to_controller_button(
        Command*                   command,
        erhe::window::Mouse_button button,
        Button_trigger             button_trigger,
        std::optional<uint32_t>    modifier_mask = {}
    );

    void bind_command_to_mouse_drag(
        Command*                   command,
        erhe::window::Mouse_button button,
        bool                       call_on_button_down_without_motion,
        std::optional<uint32_t>    modifier_mask = {}
    );

    void bind_command_to_xr_boolean_action (Command* command, erhe::xr::Xr_action_boolean* xr_action, Button_trigger button_trigger);
    void bind_command_to_xr_float_action   (Command* command, erhe::xr::Xr_action_float* xr_action);
    void bind_command_to_xr_vector2f_action(Command* command, erhe::xr::Xr_action_vector2f* xr_action);

    void bind_command_to_update(Command* command);

    // User-editable bindings. Commands are passed by reference and must be
    // registered. All of these are cold-path (UI edits, load / save).
    [[nodiscard]] auto get_input_kind        (const Command& command) const -> Input_kind;
    [[nodiscard]] auto has_binding_override  (const Command& command) const -> bool;
    void               get_default_bindings  (const Command& command, std::vector<Binding_desc>& out) const;
    void               get_effective_bindings(const Command& command, std::vector<Binding_desc>& out) const;

    // Replaces all editable bindings of the command; an empty span unbinds
    // it. Returns false (and changes nothing) when a binding does not match
    // the command's input kind or is otherwise invalid; error receives why.
    auto set_binding_override       (Command& command, std::span<const Binding_desc> bindings, std::string* error = nullptr) -> bool;
    void clear_binding_override     (Command& command);
    void clear_all_binding_overrides();

    // Persistence: every override, including entries naming commands this
    // build does not register (kept as they were loaded so they round-trip).
    void get_binding_overrides  (std::vector<Binding_override>& out) const;
    // Replaces all overrides. Entries for unknown commands are kept for
    // get_binding_overrides(); invalid entries are logged and dropped.
    void apply_binding_overrides(std::span<const Binding_override> overrides);

    // Conflicting pairs among the effective bindings, as of the last rebuild.
    [[nodiscard]] auto get_binding_conflicts() const -> const std::vector<Binding_conflict>&;

    // Called at the end of every dispatch table rebuild (from tick() or
    // sort_bindings(), with the command mutex held: the callback must not
    // call back into Commands). The callback must stay valid for the
    // lifetime of Commands.
    void add_bindings_changed_callback(std::function<void()> callback);

    [[nodiscard]] auto accept_mouse_command(const Command* command) const -> bool
    {
        return
            (m_active_mouse_command == nullptr) ||
            (m_active_mouse_command == command);
    }

    void command_inactivated(Command* command);

    [[nodiscard]] auto last_mouse_button_bits   () const -> uint32_t;
    [[nodiscard]] auto last_mouse_position      () const -> glm::vec2;
    [[nodiscard]] auto last_mouse_position_delta() const -> glm::vec2;

    void tick(int64_t timestamp_ns, std::vector<erhe::window::Input_event>& input_events);

    // Implements Input_event_handler
    auto on_key_event              (const erhe::window::Input_event& input_event) -> bool override;
    auto on_mouse_move_event       (const erhe::window::Input_event& input_event) -> bool override;
    auto on_mouse_button_event     (const erhe::window::Input_event& input_event) -> bool override;
    auto on_mouse_wheel_event      (const erhe::window::Input_event& input_event) -> bool override;
    auto on_controller_axis_event  (const erhe::window::Input_event& input_event) -> bool override;
    auto on_controller_button_event(const erhe::window::Input_event& input_event) -> bool override;

#if defined(ERHE_XR_LIBRARY_OPENXR)
    void dispatch_xr_events(erhe::xr::Xr_instance& instance, void* session);
#endif

    [[nodiscard]] auto get_command_priority(Command* command) const -> int;
    [[nodiscard]] auto get_active_mouse_command() -> Command* { return m_active_mouse_command; }

private:
    auto on_xr_boolean_event (const erhe::window::Input_event&) -> bool override;
    auto on_xr_float_event   (const erhe::window::Input_event&) -> bool override;
    auto on_xr_vector2f_event(const erhe::window::Input_event&) -> bool override;

    class Binding_entry
    {
    public:
        Command*     command;
        Binding_desc desc;
    };
    class Command_override
    {
    public:
        Command*                  command;
        std::vector<Binding_desc> bindings;
    };

    void record_default_binding     (Command* command, const Binding_desc& desc);
    void rebuild_bindings_if_dirty  ();
    void add_dispatch_binding       (Command* command, const Binding_desc& desc);
    void update_binding_conflicts   ();
    void update_menu_shortcut_labels();
    void mark_bindings_changed      (Command* command);
    [[nodiscard]] auto find_override       (const Command* command) const -> const Command_override*;
    [[nodiscard]] auto get_input_kind_nolock(const Command& command) const -> Input_kind;
    auto set_binding_override_nolock(Command& command, std::span<const Binding_desc> bindings, std::string* error) -> bool;
    void get_effective_bindings_nolock(const Command& command, std::vector<Binding_desc>& out) const;

    void sort_dispatch_bindings     ();
    void sort_mouse_bindings        ();
    void sort_mouse_wheel_bindings  ();
    void sort_controller_bindings   ();
    void sort_xr_bindings           ();
    void inactivate_ready_commands  ();
    void update_active_mouse_command(Command* command);

    mutable ERHE_PROFILE_MUTEX(std::recursive_mutex, m_command_mutex);
    Command*   m_active_mouse_command     {nullptr}; // does not tell if command(s) is/are ready
    uint32_t   m_last_mouse_button_bits   {0u};
    glm::vec2  m_last_mouse_position      {0.0f, 0.0f};
    glm::vec2  m_last_mouse_position_delta{0.0f, 0.0f};
    uint32_t   m_last_modifier_mask       {0};

    std::vector<Command*>                             m_commands;
    std::vector<Key_binding>                          m_key_bindings;
    std::vector<Menu_binding>                         m_menu_bindings;
    std::vector<Controller_axis_binding>              m_controller_axis_bindings;
    std::vector<Controller_button_binding>            m_controller_button_bindings;
    std::vector<std::unique_ptr<Mouse_binding>>       m_mouse_bindings;
    std::vector<std::unique_ptr<Mouse_wheel_binding>> m_mouse_wheel_bindings;
    std::vector<Xr_boolean_binding>                   m_xr_boolean_bindings;
    std::vector<Xr_float_binding>                     m_xr_float_bindings;
    std::vector<Xr_vector2f_binding>                  m_xr_vector2f_bindings;
    std::vector<Update_binding>                       m_update_bindings;

    // Editable bindings: the declared defaults in declaration order, the
    // user's overrides, and the dispatch tables above derived from them.
    std::vector<Binding_entry>                        m_default_bindings;
    std::vector<Binding_entry>                        m_effective_bindings;
    std::vector<Command_override>                     m_overrides;
    std::vector<Binding_override>                     m_unresolved_overrides;
    std::vector<Binding_conflict>                     m_binding_conflicts;
    std::vector<Command*>                             m_changed_commands;
    std::vector<std::function<void()>>                m_bindings_changed_callbacks;
    bool                                              m_bindings_dirty{false};
    bool                                              m_dispatching   {false};
    bool                                              m_sort_requested{false};
};

} // namespace erhe::commands
