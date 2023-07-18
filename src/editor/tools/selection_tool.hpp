#pragma once

#include "tools/tool.hpp"

#include "erhe/commands/command.hpp"
#include "erhe/imgui/imgui_window.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/toolkit/bit_helpers.hpp"

#include <memory>
#include <vector>

namespace erhe::commands {
    class Commands;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::scene {
    class Mesh;
    class Scene;
}

namespace editor
{

class Editor_context;
class Editor_message_bus;
class Editor_scenes;
class Editor_settings;
class Icon_set;
class Input_state;
class Operation_stack;
class Selection;
class Selection_tool;
class Tools;

class Selection_delete_command
    : public erhe::commands::Command
{
public:
    Selection_delete_command(
        erhe::commands::Commands& commands,
        Editor_context&           context
    );
    auto try_call() -> bool override;

private:
    Editor_context& m_context;
};

class Viewport_select_command
    : public erhe::commands::Command
{
public:
    Viewport_select_command(
        erhe::commands::Commands& commands,
        Editor_context&           context
    );
    void try_ready() override;
    auto try_call () -> bool override;

private:
    Editor_context& m_context;
};

class Viewport_select_toggle_command
    : public erhe::commands::Command
{
public:
    Viewport_select_toggle_command(
        erhe::commands::Commands& commands,
        Editor_context&           context
    );
    void try_ready() override;
    auto try_call () -> bool override;

private:
    Editor_context& m_context;
};

class Range_selection
{
public:
    explicit Range_selection(Selection& selection);

    void set_terminator(const std::shared_ptr<erhe::scene::Item>& item);
    void entry         (const std::shared_ptr<erhe::scene::Item>& item);
    void begin         ();
    void end           ();
    void reset         ();

private:
    Selection&                                      m_selection;
    std::shared_ptr<erhe::scene::Item>              m_primary_terminator;
    std::shared_ptr<erhe::scene::Item>              m_secondary_terminator;
    bool                                            m_edited{false};
    std::vector<std::shared_ptr<erhe::scene::Item>> m_entries;
};

#if defined(ERHE_XR_LIBRARY_OPENXR)
class Headset_view;
#endif

class Selection_tool
    : public Tool
{
public:
    static constexpr int c_priority{3};

    Selection_tool(
        Editor_context& editor_context,
        Icon_set&       icon_set,
        Tools&          tools
    );

    // Implements Tool
    void handle_priority_update(int old_priority, int new_priority) override;

    // Implements Imgui_window
    //void imgui() override;

    void viewport_toolbar(bool& hovered);
};

class Selection
    : public erhe::commands::Command_host
{
public:
    Selection(
        erhe::commands::Commands& commands,
        Editor_context&           editor_context,
        Editor_message_bus&       editor_message_bus
    );

#if defined(ERHE_XR_LIBRARY_OPENXR)
    void setup_xr_bindings(
        erhe::commands::Commands& commands,
        Headset_view&             headset_view
    );
#endif

    // Public API
    [[nodiscard]] auto get_selection     () const -> const std::vector<std::shared_ptr<erhe::scene::Item>>&;
    [[nodiscard]] auto is_in_selection   (const std::shared_ptr<erhe::scene::Item>& item) const -> bool;
    [[nodiscard]] auto range_selection   () -> Range_selection&;

    template <typename T>
    [[nodiscard]] auto get(std::size_t index = 0) -> std::shared_ptr<T>;

    template <typename T>
    [[nodiscard]] auto count() -> std::size_t;

    [[nodiscard]] auto get(erhe::scene::Item_filter filter, std::size_t index = 0) -> std::shared_ptr<erhe::scene::Item>;

    void set_selection                   (const std::vector<std::shared_ptr<erhe::scene::Item>>& selection);
    auto add_to_selection                (const std::shared_ptr<erhe::scene::Item>& item) -> bool;
    auto clear_selection                 () -> bool;
    auto remove_from_selection           (const std::shared_ptr<erhe::scene::Item>& item) -> bool;
    void update_selection_from_scene_item(const std::shared_ptr<erhe::scene::Item>& item, const bool added);
    void sanity_check                    ();

    // Commands
    auto on_viewport_select_try_ready() -> bool;
    auto on_viewport_select          () -> bool;
    auto on_viewport_select_toggle   () -> bool;

    auto delete_selection() -> bool;

private:
    void send_selection_change_message() const;
    void toggle_mesh_selection(
        const std::shared_ptr<erhe::scene::Mesh>& mesh,
        bool                                      was_selected,
        bool                                      clear_others
    );

    Editor_context&                m_context;

    Viewport_select_command        m_viewport_select_command;
    Viewport_select_toggle_command m_viewport_select_toggle_command;
    Selection_delete_command       m_delete_command;

    Scene_view*                                     m_hover_scene_view{nullptr};
    std::vector<std::shared_ptr<erhe::scene::Item>> m_selection;
    Range_selection                                 m_range_selection;
    std::shared_ptr<erhe::scene::Mesh>              m_hover_mesh;
    bool                                            m_hover_content{false};
    bool                                            m_hover_tool   {false};
};

template <typename T>
auto Selection::get(const std::size_t index) -> std::shared_ptr<T>
{
    std::size_t i = 0;
    for (const auto& item : m_selection) {
        if (!item) {
            continue;
        }
        if (!erhe::toolkit::test_all_rhs_bits_set(item->get_type(), T::get_static_type())) {
            continue;
        }
        if (i == index) {
            return std::static_pointer_cast<T>(item);
        } else {
            ++i;
        }
    }
    return {};
}

template <typename T>
auto Selection::count() -> std::size_t
{
    std::size_t i = 0;
    for (const auto& item : m_selection) {
        if (!item) {
            continue;
        }
        if (!erhe::toolkit::test_all_rhs_bits_set(item->get_type(), T::get_static_type())) {
            continue;
        }
        ++i;
    }
    return i;
}

} // namespace editor
