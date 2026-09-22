#pragma once

#include "content_library/content_library.hpp"
#include "tools/tool.hpp"

#include "erhe_commands/command.hpp"
#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <memory>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
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
class Scene_root;
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

    // Host-scoped reset: collapses the range (including reset()'s
    // selection-clearing side effect, restricted to `host`) only when a
    // terminator belongs to `host`. A range running in another host's tree
    // is left untouched, so interacting with one scene never cancels a
    // range selection in another.
    void reset(erhe::Item_host* host);

    // Drop the terminators (without the selection-clearing side effect of
    // reset()) when either one is hosted by `host`. Used by the host-scoped
    // Selection::clear_selection(Item_host*) so clearing one scene's
    // selection does not disturb a range selection running in another
    // scene's tree (and without re-entering clear_selection).
    void reset_terminators_for_host(erhe::Item_host* host);

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

    void viewport_toolbar();
};

class Scoped_selection_change
{
public:
    explicit Scoped_selection_change(Selection& selection);
    ~Scoped_selection_change() noexcept;

private:
    Selection& selection;
};

// What Selection::clear_selection(Item_host*) does to the active item.
enum class Active_item : unsigned int {
    keep = 0,      // a plain click / a scoped re-selection: the active item stays
    forget_hosted  // a closing scene: an active item hosted by it is forgotten
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
    // Monotonic count of Selection_message dispatches. Lets a test assert
    // that a batched prune dispatches ONCE
    // (doc/editor/import_undo_reference_clearing.md).
    [[nodiscard]] auto get_selection_change_count() const -> std::size_t;

    // Content removed without a scene closing (undo of a glTF import): drop
    // the removed items from the selection - in ONE batched change - and
    // forget them as "last selected". The last-selected map is weak, but the
    // undo history keeps a removed item alive for redo, so without this the
    // parts that re-resolve get_last_selected() every frame (Operations'
    // make-mesh material) resurrect the reference on the next frame
    // (doc/editor/import_undo_reference_clearing.md).
    void on_items_removed(const Removed_items& removed);
    [[nodiscard]] auto is_in_selection   (const std::shared_ptr<erhe::Item_base>& item) const -> bool;
    [[nodiscard]] auto range_selection   () -> Range_selection&;
    [[nodiscard]] auto get_last_selected (uint64_t type) -> std::shared_ptr<erhe::Item_base>;

    // Per-host selection view: the items of the current selection hosted by
    // `host` (nullptr = items with no Item_host, e.g. content library
    // entries). The authoritative selection is the union returned by
    // get_selected_items(); this clears and refills the host-owned bucket
    // (capacity retained) from it at query time, so a host change while
    // selected (e.g. a cross-scene reparent) can never leave a stale view.
    [[nodiscard]] auto get_hosted_selection(erhe::Item_host* host) -> const std::vector<std::shared_ptr<erhe::Item_base>>&;

    // Remove from the selection all items hosted by `host` (nullptr = items
    // with no Item_host). Other hosts' selections are left untouched.
    // Returns true when anything was removed.
    //
    // Active_item says what happens to the active item (doc/editor/active_item.md
    // D2): keep leaves it as it is (a plain click, an MCP select_items reset -
    // the active item survives a cleared selection), forget_hosted drops it
    // when this host hosts it, which is what a closing scene needs.
    auto clear_selection(erhe::Item_host* host, Active_item active_item = Active_item::keep) -> bool;

    // The active item: the one reference item of the selection
    // (doc/editor/active_item.md D1). It may be empty, and it may be an item
    // that is not selected.
    [[nodiscard]] auto get_active_item() const -> std::shared_ptr<erhe::Item_base>;

    // Make `item` the active item; an empty `item` forgets the active item.
    // The item need not be in the selection (D3.3).
    void set_active_item(const std::shared_ptr<erhe::Item_base>& item);

    // "Belongs to host" for selection scoping: prim hosting, extended (R5.6)
    // with the asset manager's defining-container lookup - a selected
    // resource another container defines is held by this scene's tree, and
    // a selected resource of a closing scene must leave the selection with
    // it either way.
    [[nodiscard]] auto is_hosted_or_defined_by(const erhe::Item_base& item, const erhe::Item_host* host) const -> bool;

    // The items commands act on (operation scoping policy): the ACTIVE
    // scene's items plus non-hosted items. Selection in other scenes
    // persists but is never an invisible participant in a command.
    // Cleared and refilled (capacity retained) at call time.
    [[nodiscard]] auto get_command_target_selection() -> const std::vector<std::shared_ptr<erhe::Item_base>>&;

    // Active scene: the scene commands targeting scene-hosted items act on;
    // the UI highlights its windows. Explicit tracked state, updated by
    // selection changes (the changed scene becomes active) and by focusing a
    // scene's viewport / hierarchy window - never by hover alone. The getter
    // falls back to the last hovered scene view's scene, then to the single
    // open scene, when nothing is explicitly active.
    [[nodiscard]] auto get_active_scene_root() -> std::shared_ptr<Scene_root>;
    void set_active_scene_root(const std::shared_ptr<Scene_root>& scene_root);

    template <typename T>
    [[nodiscard]] auto get_last_selected() const -> std::shared_ptr<T>;

    // The one reference item of a command (doc/editor/active_item.md D6): the
    // active item when it is of type T.
    // The active item counts as a reference only when it is non-hosted or
    // hosted by the active scene; an active item hosted by another scene
    // answers empty, matching the command target scoping. Whether the active
    // item is selected does not matter.
    template <typename T>
    [[nodiscard]] auto get_active_item_as() -> std::shared_ptr<T>;

    // The scene rule of get_active_item_as(): non-hosted, or hosted by the
    // active scene.
    [[nodiscard]] auto is_command_reference_host(const erhe::Item_base& item) -> bool;

    [[nodiscard]] auto get(erhe::Item_filter filter, std::size_t index = 0) -> std::shared_ptr<erhe::Item_base>;

    void set_selection                   (const std::vector<std::shared_ptr<erhe::Item_base>>& selection);
    // Selection plus an explicit active item (D3.2). The active item need not
    // be one of `selection`; an empty `active` keeps the current active item.
    void set_selection                   (const std::vector<std::shared_ptr<erhe::Item_base>>& selection, const std::shared_ptr<erhe::Item_base>& active);
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
    // Queue a recursive delete of the given items (the operation delete_selection
    // performs on the command target selection). Each item's subtree is deleted;
    // lock_edit items and their subtrees survive, except inside a prefab
    // instance: the instance is always deleted as a whole (its interior is
    // sealed with lock_edit to be non-editable, not to outlive the instance).
    auto delete_items       (const std::vector<std::shared_ptr<erhe::Item_base>>& items) -> bool;
    auto cut_selection      () -> bool;
    auto copy_selection     () -> bool;
    auto duplicate_selection() -> bool;

    void begin_selection_change();
    void end_selection_change();

    void update_last_selected(const std::shared_ptr<erhe::Item_base>& item);

private:
    void toggle_mesh_selection(const std::shared_ptr<erhe::scene::Mesh>& mesh, bool was_selected, bool clear_others);

    // Bone selection mode: the click acts on the JOINT NODE a hovered bone
    // proxy stands for, through the ordinary selection, so Properties, undo and
    // the transform gizmo all work on it unchanged. `toggle` is the Ctrl
    // variant; without it a plain click replaces the scene's selection and an
    // empty-space click clears it.
    [[nodiscard]] auto is_bone_mode           () const -> bool;
    auto               on_viewport_select_bone(bool toggle) -> bool;

    erhe::message_bus::Subscription<Hover_scene_view_message> m_hover_scene_view_subscription;
    App_context&                   m_context;

    Viewport_select_command        m_viewport_select_command;
    Viewport_select_toggle_command m_viewport_select_toggle_command;
    Selection_delete_command       m_delete_command;
    Selection_cut_command          m_cut_command;
    Selection_copy_command         m_copy_command;
    Selection_duplicate_command    m_duplicate_command;

    Scene_view*                                   m_hover_scene_view{nullptr};
    std::vector<std::shared_ptr<erhe::Item_base>> m_selection;
    std::vector<std::shared_ptr<erhe::Item_base>> m_non_hosted_selection; // get_hosted_selection(nullptr) bucket
    std::vector<std::shared_ptr<erhe::Item_base>> m_command_target_selection; // get_command_target_selection() scratch
    std::weak_ptr<Scene_root>                     m_active_scene_root;
    Range_selection                               m_range_selection;
    std::weak_ptr<erhe::scene::Mesh>              m_hover_mesh   {};
    // Joint node behind the hovered bone proxy; resolved in try_ready while bone
    // mode is active, empty otherwise.
    std::weak_ptr<erhe::scene::Node>              m_hover_bone_joint{};
    bool                                          m_hover_content{false};
    bool                                          m_hover_tool   {false};

    // Applied by end_selection_change / set_active_item, never written
    // directly: write_active_item maintains the Item_flags::active_item bit
    // and m_active_item_before records the state a pending change started
    // from, so exactly one message is sent per change.
    void write_active_item(const std::shared_ptr<erhe::Item_base>& item);
    void send_active_item_message(const std::shared_ptr<erhe::Item_base>& old_item, const std::shared_ptr<erhe::Item_base>& new_item);

    std::weak_ptr<erhe::Item_base>                m_active_item{};
    std::weak_ptr<erhe::Item_base>                m_active_item_before{};

    int                                           m_selection_change_depth{0};
    std::size_t                                   m_selection_change_count{0};
    erhe::message_bus::Subscription<Items_removed_message> m_items_removed_subscription;
    std::vector<std::shared_ptr<erhe::Item_base>> m_begin_selection_change_state;
    std::unordered_map<uint64_t, std::weak_ptr<erhe::Item_base>> m_last_selected_by_type;
    // Scratch membership set of set_selection(); holds no ownership and is
    // cleared after use so nothing is kept alive between calls.
    std::unordered_set<const erhe::Item_base*>    m_set_selection_lookup;
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

template <typename T>
auto Selection::get_active_item_as() -> std::shared_ptr<T>
{
    const std::shared_ptr<erhe::Item_base> item = m_active_item.lock();
    if (!item) {
        return {};
    }
    if (!is_command_reference_host(*item)) {
        return {};
    }
    if (erhe::utility::test_all_rhs_bits_set(item->get_type(), T::get_static_type())) {
        return std::static_pointer_cast<T>(item);
    }
    return {};
}

}
