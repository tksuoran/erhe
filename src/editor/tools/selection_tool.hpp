#pragma once

#include "scene/content_library.hpp"
#include "tools/tool.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_bit/bit_helpers.hpp"

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

class Selection_cut_command
    : public erhe::commands::Command
{
public:
    Selection_cut_command(
        erhe::commands::Commands& commands,
        Editor_context&           context
    );
    auto try_call() -> bool override;

private:
    Editor_context& m_context;
};

class Selection_copy_command
    : public erhe::commands::Command
{
public:
    Selection_copy_command(
        erhe::commands::Commands& commands,
        Editor_context&           context
    );
    auto try_call() -> bool override;

private:
    Editor_context& m_context;
};

class Selection_duplicate_command
    : public erhe::commands::Command
{
public:
    Selection_duplicate_command(
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

    void set_terminator(const std::shared_ptr<erhe::Item_base>& item);
    void entry         (const std::shared_ptr<erhe::Item_base>& item);
    void begin         ();
    void end           ();
    void reset         ();

private:
    Selection&                                    m_selection;
    std::shared_ptr<erhe::Item_base>              m_primary_terminator;
    std::shared_ptr<erhe::Item_base>              m_secondary_terminator;
    bool                                          m_edited{false};
    std::vector<std::shared_ptr<erhe::Item_base>> m_entries;
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

class Scoped_selection_change
{
public:
    Scoped_selection_change(Selection& selection);
    ~Scoped_selection_change();

private:
    Selection& selection;
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
    [[nodiscard]] auto get_selection  () const -> const std::vector<std::shared_ptr<erhe::Item_base>>&;
    [[nodiscard]] auto is_in_selection(const std::shared_ptr<erhe::Item_base>& item) const -> bool;
    [[nodiscard]] auto range_selection() -> Range_selection&;

    template <typename T>
    [[nodiscard]] auto get(std::size_t index = 0) -> std::shared_ptr<T>;

    template <typename T>
    [[nodiscard]] auto count() -> std::size_t;

    [[nodiscard]] auto get(erhe::Item_filter filter, std::size_t index = 0) -> std::shared_ptr<erhe::Item_base>;

    void set_selection                   (const std::vector<std::shared_ptr<erhe::Item_base>>& selection);
    auto add_to_selection                (const std::shared_ptr<erhe::Item_base>& item) -> bool;
    auto clear_selection                 () -> bool;
    auto remove_from_selection           (const std::shared_ptr<erhe::Item_base>& item) -> bool;
    void update_selection_from_scene_item(const std::shared_ptr<erhe::Item_base>& item, const bool added);
    void sanity_check                    ();

    // Commands
    auto on_viewport_select_try_ready() -> bool;
    auto on_viewport_select          () -> bool;
    auto on_viewport_select_toggle   () -> bool;

    auto delete_selection   () -> bool;
    auto cut_selection      () -> bool;
    auto copy_selection     () -> bool;
    auto duplicate_selection() -> bool;

    void begin_selection_change();
    void end_selection_change();

private:
    void toggle_mesh_selection(
        const std::shared_ptr<erhe::scene::Mesh>& mesh,
        bool                                      was_selected,
        bool                                      clear_others
    );

    Editor_context&                m_context;

    Viewport_select_command        m_viewport_select_command;
    Viewport_select_toggle_command m_viewport_select_toggle_command;
    Selection_delete_command       m_delete_command;
    Selection_cut_command          m_cut_command;
    Selection_copy_command         m_copy_command;
    Selection_duplicate_command    m_duplicate_command;

    Scene_view*                                   m_hover_scene_view{nullptr};
    std::vector<std::shared_ptr<erhe::Item_base>> m_selection;
    Range_selection                               m_range_selection;
    erhe::scene::Mesh*                            m_hover_mesh   {nullptr};
    bool                                          m_hover_content{false};
    bool                                          m_hover_tool   {false};

    int                                           m_selection_change_depth{0};
    std::vector<std::shared_ptr<erhe::Item_base>> m_begin_selection_change_state;
};

template <typename T>
auto Selection::get(const std::size_t index) -> std::shared_ptr<T>
{
    std::size_t i = 0;
    for (const auto& item : m_selection) {
        if (!item) {
            continue;
        }
        if (item->get_type() == erhe::Item_type::content_library_node) {
            const auto node = std::dynamic_pointer_cast<Content_library_node>(item);
            if (node) {
                const auto node_item = node->item;
                if (node_item) {
                    if (!erhe::bit::test_all_rhs_bits_set(node_item->get_type(), T::get_static_type())) {
                        continue;
                    }
                    if (i == index) {
                        return std::static_pointer_cast<T>(node_item);
                    }
                    ++i;
                }
            }
        } else {
            if (!erhe::bit::test_all_rhs_bits_set(item->get_type(), T::get_static_type())) {
                continue;
            }
            if (i == index) {
                return std::static_pointer_cast<T>(item);
            }
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
        if (!erhe::bit::test_all_rhs_bits_set(item->get_type(), T::get_static_type())) {
            continue;
        }
        ++i;
    }
    return i;
}

} // namespace editor
