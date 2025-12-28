#pragma once

#include "content_library/content_library.hpp"
#include "tools/tool.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <memory>
#include <unordered_map>
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

namespace editor {

class App_context;
class App_message_bus;
class App_scenes;
class Icon_set;
class Selection;
class Tools;

class Selection_delete_command : public erhe::commands::Command
{
public:
    Selection_delete_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Selection_cut_command : public erhe::commands::Command
{
public:
    Selection_cut_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Selection_copy_command : public erhe::commands::Command
{
public:
    Selection_copy_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Selection_duplicate_command : public erhe::commands::Command
{
public:
    Selection_duplicate_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Viewport_select_command : public erhe::commands::Command
{
public:
    Viewport_select_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready() override;
    auto try_call () -> bool override;

private:
    App_context& m_context;
};

class Viewport_select_toggle_command : public erhe::commands::Command
{
public:
    Viewport_select_toggle_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready() override;
    auto try_call () -> bool override;

private:
    App_context& m_context;
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

class Selection_tool : public Tool
{
public:
    static constexpr int c_priority{3};

    Selection_tool(App_context& app_context, Icon_set& icon_set, Tools& tools);

    // Implements Tool
    void handle_priority_update(int old_priority, int new_priority) override;

    // Implements Imgui_window
    //void imgui() override;

    void viewport_toolbar(bool& hovered);
};

class Scoped_selection_change
{
public:
    explicit Scoped_selection_change(Selection& selection);
    ~Scoped_selection_change() noexcept;

private:
    Selection& selection;
};

class Selection : public erhe::commands::Command_host
{
public:
    Selection(erhe::commands::Commands& commands, App_context& context, App_message_bus& app_message_bus);

#if defined(ERHE_XR_LIBRARY_OPENXR)
    void setup_xr_bindings(erhe::commands::Commands& commands, Headset_view& headset_view);
#endif

    // Public API
    [[nodiscard]] auto get_selected_items() const -> const std::vector<std::shared_ptr<erhe::Item_base>>&;
    [[nodiscard]] auto is_in_selection   (const std::shared_ptr<erhe::Item_base>& item) const -> bool;
    [[nodiscard]] auto range_selection   () -> Range_selection&;
    [[nodiscard]] auto get_last_selected (uint64_t type) -> std::shared_ptr<erhe::Item_base>;

    template <typename T>
    [[nodiscard]] auto get_last_selected() const -> std::shared_ptr<T>;

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

    void update_last_selected(const std::shared_ptr<erhe::Item_base>& item);

private:
    void toggle_mesh_selection(const std::shared_ptr<erhe::scene::Mesh>& mesh, bool was_selected, bool clear_others);

    App_context&                   m_context;

    Viewport_select_command        m_viewport_select_command;
    Viewport_select_toggle_command m_viewport_select_toggle_command;
    Selection_delete_command       m_delete_command;
    Selection_cut_command          m_cut_command;
    Selection_copy_command         m_copy_command;
    Selection_duplicate_command    m_duplicate_command;

    Scene_view*                                   m_hover_scene_view{nullptr};
    std::vector<std::shared_ptr<erhe::Item_base>> m_selection;
    Range_selection                               m_range_selection;
    std::weak_ptr<erhe::scene::Mesh>              m_hover_mesh   {};
    bool                                          m_hover_content{false};
    bool                                          m_hover_tool   {false};

    int                                           m_selection_change_depth{0};
    std::vector<std::shared_ptr<erhe::Item_base>> m_begin_selection_change_state;
    std::unordered_map<uint64_t, std::weak_ptr<erhe::Item_base>> m_last_selected_by_type;
};

template <typename T>
auto Selection::get_last_selected() const -> std::shared_ptr<T>
{
    auto i = m_last_selected_by_type.find(T::get_static_type());
    if (i == m_last_selected_by_type.end()) {
        return {};
    }
    std::shared_ptr<erhe::Item_base> item = (*i).second.lock();
    return std::dynamic_pointer_cast<T>(item);
}

}
