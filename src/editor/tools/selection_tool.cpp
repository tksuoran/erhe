#include "tools/selection_tool.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_scenes.hpp"
#include "input_state.hpp"
#include "graphics/icon_set.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"
#include "scene/scene_root.hpp"
#include "tools/clipboard.hpp"
#include "tools/tools.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_hash/xxhash.hpp"
#include "erhe_verify/verify.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/xr_action.hpp"
#   include "erhe_xr/headset.hpp"
#endif

namespace editor {

using glm::mat4;
using glm::vec3;
using glm::vec4;

#pragma region Range_selection
Range_selection::Range_selection(Selection& selection)
    : m_selection{selection}
{
}

void Range_selection::set_terminator(const std::shared_ptr<erhe::Item_base>& item)
{
    if (!m_primary_terminator) {
        log_selection->trace(
            "setting primary terminator to '{}'",
            item->describe()
        );

        m_primary_terminator = item;
        m_edited = true;
        return;
    }
    if (item == m_primary_terminator) {
        log_selection->trace(
            "ignoring setting terminator to '{}' because it is already the primary terminator",
            item->describe()
        );
        return;
    }
    if (item == m_secondary_terminator) {
        log_selection->trace(
            "ignoring setting terminator to '{}' because it is already the secondary terminator",
            item->describe()
        );
        return;
    }

    log_selection->trace("setting secondary terminator to '{}'", item->describe());
    m_secondary_terminator = item;
    m_edited = true;
}

void Range_selection::entry(const std::shared_ptr<erhe::Item_base>& item)
{
    m_entries.push_back(item);
}

void Range_selection::begin()
{
    m_edited = false;
    m_entries.clear();
}

void Range_selection::end()
{
    if (m_entries.empty() || !m_edited || !m_primary_terminator || !m_secondary_terminator) {
        m_entries.clear();
        return;
    }
    log_selection->trace("setting selection since range was modified");

    std::vector<std::shared_ptr<erhe::Item_base>> selection;
    bool between_terminators{false};

    auto primary_terminator   = m_primary_terminator;
    auto secondary_terminator = m_secondary_terminator;

    for (const auto& item : m_entries) {
        const bool match_primary   = item == primary_terminator;
        const bool match_secondary = item == secondary_terminator;
        if (match_primary || match_secondary) {
            log_selection->trace("   T. {}", item->describe());
            selection.push_back(item);
            between_terminators = !between_terminators;
            primary_terminator   = m_primary_terminator;
            secondary_terminator = m_secondary_terminator;
            continue;
        }
        if (between_terminators) {
            log_selection->trace("    + {}", item->describe());
            selection.push_back(item);
        } else {
            log_selection->trace("    - {}", item->describe());
        }
    }
    if (selection.empty()) {
        m_entries.clear();
        return;
    }

    m_selection.set_selection(selection);
    m_entries.clear();
}

void Range_selection::reset()
{
    log_selection->trace("resetting range selection");
    if (m_primary_terminator && m_secondary_terminator) {
        m_selection.clear_selection();
    }
    m_primary_terminator.reset();
    m_secondary_terminator.reset();
}

#pragma endregion Range_selection

#pragma region Commands
Viewport_select_command::Viewport_select_command(erhe::commands::Commands& commands, Editor_context& editor_context)
    : Command  {commands, "Selection.viewport_select"}
    , m_context{editor_context}
{
}

void Viewport_select_command::try_ready()
{
    if (m_context.selection->on_viewport_select_try_ready()) {
        log_selection->trace("Selection set ready");
        set_ready();
    }
    log_selection->trace("Not setting selection ready");
}

auto Viewport_select_command::try_call() -> bool
{
    if (get_command_state() != erhe::commands::State::Ready) {
        log_selection->trace("Selection not in ready state");
        return false;
    }

    const bool consumed = m_context.selection->on_viewport_select();
    set_inactive();
    return consumed;
}

//

Viewport_select_toggle_command::Viewport_select_toggle_command(erhe::commands::Commands& commands, Editor_context& editor_context)
    : Command  {commands, "Selection.viewport_select_toggle"}
    , m_context{editor_context}
{
}

void Viewport_select_toggle_command::try_ready()
{
    if (m_context.selection->on_viewport_select_try_ready()) {
        set_ready();
    }
}

auto Viewport_select_toggle_command::try_call() -> bool
{
    if (get_command_state() != erhe::commands::State::Ready) {
        log_selection->trace("Selection not in ready state");
        return false;
    }

    const bool consumed = m_context.selection->on_viewport_select_toggle();
    set_inactive();
    return consumed;
}

//

Selection_delete_command::Selection_delete_command(erhe::commands::Commands& commands, Editor_context& editor_context)
    : Command  {commands, "Selection.delete"}
    , m_context{editor_context}
{
}

auto Selection_delete_command::try_call() -> bool
{
    return m_context.selection->delete_selection();
}

//

Selection_cut_command::Selection_cut_command(erhe::commands::Commands& commands, Editor_context& editor_context)
    : Command  {commands, "Selection.cut"}
    , m_context{editor_context}
{
}

auto Selection_cut_command::try_call() -> bool
{
    return m_context.selection->cut_selection();
}

//

Selection_copy_command::Selection_copy_command(erhe::commands::Commands& commands, Editor_context& editor_context)
    : Command  {commands, "Selection.copy"}
    , m_context{editor_context}
{
}

auto Selection_copy_command::try_call() -> bool
{
    return m_context.selection->copy_selection();
}

//

Selection_duplicate_command::Selection_duplicate_command(erhe::commands::Commands& commands, Editor_context& editor_context)
    : Command  {commands, "Selection.duplicate"}
    , m_context{editor_context}
{
}

auto Selection_duplicate_command::try_call() -> bool
{
    return m_context.selection->duplicate_selection();
}


#pragma endregion Commands

Selection_tool::Selection_tool(Editor_context& editor_context, Icon_set& icon_set, Tools& tools)
    : Tool{editor_context}
{
    ERHE_PROFILE_FUNCTION();

    set_base_priority(c_priority);
    set_description  ("Selection Tool");
    set_flags        (Tool_flags::toolbox | Tool_flags::secondary);
    set_icon         (icon_set.icons.select);
    tools.register_tool(this);
}

Selection::Selection(erhe::commands::Commands& commands, Editor_context& editor_context, Editor_message_bus& editor_message_bus)
    : m_context                       {editor_context}
    , m_viewport_select_command       {commands, editor_context}
    , m_viewport_select_toggle_command{commands, editor_context}
    , m_delete_command                {commands, editor_context}
    , m_cut_command                   {commands, editor_context}
    , m_copy_command                  {commands, editor_context}
    , m_duplicate_command             {commands, editor_context}
    , m_range_selection               {*this}
{
    commands.register_command            (&m_viewport_select_command);
    commands.register_command            (&m_delete_command);
    commands.register_command            (&m_cut_command);
    commands.register_command            (&m_copy_command);
    commands.register_command            (&m_duplicate_command);
    commands.bind_command_to_mouse_button(&m_viewport_select_command, erhe::window::Mouse_button_left, false);
    commands.bind_command_to_key         (&m_delete_command,          erhe::window::Key_delete,        true);
    commands.bind_command_to_key         (&m_cut_command,             erhe::window::Key_x,             true, erhe::window::Key_modifier_bit_ctrl);
    commands.bind_command_to_key         (&m_copy_command,            erhe::window::Key_insert,        true, erhe::window::Key_modifier_bit_ctrl);
    commands.bind_command_to_key         (&m_copy_command,            erhe::window::Key_c,             true, erhe::window::Key_modifier_bit_ctrl);
    commands.bind_command_to_key         (&m_duplicate_command,       erhe::window::Key_d,             true, erhe::window::Key_modifier_bit_ctrl);

    commands.bind_command_to_menu(&m_delete_command,    "Edit.Delete");
    commands.bind_command_to_menu(&m_cut_command,       "Edit.Cut");
    commands.bind_command_to_menu(&m_copy_command,      "Edit.Copy");
    commands.bind_command_to_menu(&m_duplicate_command, "Edit.Duplicate");
    commands.bind_command_to_menu(&m_duplicate_command, "Edit.Paster");

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            using namespace erhe::bit;
            if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view)) {
                m_hover_scene_view = message.scene_view;
            }
        }
    );

    m_viewport_select_command.set_host(this);
    m_delete_command.set_host(this);
}

#if defined(ERHE_XR_LIBRARY_OPENXR)
void Selection::setup_xr_bindings(erhe::commands::Commands& commands, Headset_view& headset_view)
{
    erhe::xr::Headset* headset = headset_view.get_headset();
    if (headset == nullptr) {
        return;
    }
    erhe::xr::Xr_actions* xr_left  = headset->get_actions_left();
    erhe::xr::Xr_actions* xr_right = headset->get_actions_right();
    if (xr_right != nullptr) {
        commands.bind_command_to_xr_boolean_action(&m_viewport_select_command,        xr_right->trigger_click, erhe::commands::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_boolean_action(&m_viewport_select_toggle_command, xr_right->a_click,       erhe::commands::Button_trigger::Button_pressed);
    }
    if (xr_left != nullptr) {
        commands.bind_command_to_xr_boolean_action(&m_viewport_select_command,        xr_left ->x_click,       erhe::commands::Button_trigger::Button_pressed);
    }
}
#endif

void Selection_tool::handle_priority_update(int old_priority, int new_priority)
{
    if (new_priority < old_priority) {
        m_context.selection->clear_selection();
    }
}

auto Selection::get_selection() const -> const std::vector<std::shared_ptr<erhe::Item_base>>&
{
    return m_selection;
}

auto Selection::delete_selection() -> bool
{
    if (m_selection.empty()) {
        return false;
    }

    Compound_operation::Parameters compound_parameters;
    std::vector<std::shared_ptr<erhe::Item_base>> recursive_selection;
    auto collect_item = [&recursive_selection](erhe::Hierarchy& item) -> bool {
        recursive_selection.push_back(item.shared_from_this());
        return true;
    };
    for (const std::shared_ptr<erhe::Item_base>& item : m_selection) {
        const std::shared_ptr<erhe::Hierarchy> hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
        if (!hierarchy) {
            continue;
        }
        erhe::Hierarchy& hierarchy_reference = *hierarchy.get();
        hierarchy_reference.for_each<erhe::Hierarchy>(collect_item);
    }

    // Sort deepest first
    std::sort(
        recursive_selection.begin(),
        recursive_selection.end(),
        [](const std::shared_ptr<erhe::Item_base>& lhs, const std::shared_ptr<erhe::Item_base>& rhs)
        {
            const std::shared_ptr<erhe::Hierarchy> lhs_hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(lhs);
            const std::shared_ptr<erhe::Hierarchy> rhs_hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(rhs);
            size_t lhs_depth = lhs_hierarchy ? lhs_hierarchy->get_depth() : 0;
            size_t rhs_depth = rhs_hierarchy ? rhs_hierarchy->get_depth() : 0;
            return lhs_depth > rhs_depth;
        }
    );

    //for (auto& item : m_selection) {
    for (auto& item : recursive_selection) {
        // TODO
        const auto hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
        if (!hierarchy) {
            continue;
        }

        compound_parameters.operations.push_back(
            std::make_shared<Item_insert_remove_operation>(
                Item_insert_remove_operation::Parameters{
                    .context = m_context,
                    .item    = hierarchy,
                    .parent  = hierarchy->get_parent().lock(),
                    .mode    = Item_insert_remove_operation::Mode::remove,
                }
            )
        );
    }
    if (compound_parameters.operations.empty()) {
        return false;
    }

    const auto op = std::make_shared<Compound_operation>(std::move(compound_parameters));

    m_context.operation_stack->queue(op);
    return true;
}

auto Selection::cut_selection() -> bool
{
    if (m_selection.empty()) {
        return false;
    }

    m_context.clipboard->set_contents(m_selection);
    return delete_selection();
}

auto Selection::copy_selection() -> bool
{
    if (m_selection.empty()) {
        return false;
    }

    std::vector<std::shared_ptr<erhe::Item_base>> selection_clone;
    for (const auto& item : m_selection) {
        selection_clone.push_back(item->clone());
    }
    m_context.clipboard->set_contents(selection_clone);
    return true;
}

auto Selection::duplicate_selection() -> bool
{
    if (m_selection.empty()) {
        return false;
    }

    Compound_operation::Parameters compound_parameters{};

    for (const auto& item : m_selection) {
        const auto& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
        if (hierarchy) {
            compound_parameters.operations.push_back(
                std::make_shared<Item_insert_remove_operation>(
                    Item_insert_remove_operation::Parameters{
                        .context         = m_context,
                        .item            = std::dynamic_pointer_cast<erhe::Hierarchy>(hierarchy->clone()),
                        .parent          = hierarchy->get_parent().lock(),
                        .mode            = Item_insert_remove_operation::Mode::insert,
                        .index_in_parent = hierarchy->get_index_in_parent() + 1
                    }
                )
            );
        }
            //const auto& node_attachment = std::dynamic_pointer_cast<erhe::scene::Node_attachment>(item);
            //if (node_attachment) {
            //    compound_parameters.operations.push_back(
            //        std::make_shared<Node_attach_operation>(
            //            node_attachment,
            //            target_node
            //        )
            //    );
            //}
        //}
    }
    if (compound_parameters.operations.empty()) {
        return false;
    }
    m_context.operation_stack->queue(
        std::make_shared<Compound_operation>(std::move(compound_parameters))
    );

    return true; // TODO
}

auto Selection::range_selection() -> Range_selection&
{
    return m_range_selection;
}

auto Selection::get(erhe::Item_filter filter, const std::size_t index) -> std::shared_ptr<erhe::Item_base>
{
    std::size_t i = 0;
    for (const auto& item : m_selection) {
        if (filter(item->get_type())) {
            if (i == index) {
                return item;
            } else {
                ++i;
            }
        }
    }
    return {};
}


template <typename T>
[[nodiscard]] auto is_in(const T& item, const std::vector<T>& items) -> bool
{
    return std::find(
        items.begin(),
        items.end(),
        item
    ) != items.end();
}

auto item_set_sort_predicate(const std::shared_ptr<erhe::Item_base>& lhs, const std::shared_ptr<erhe::Item_base>& rhs) -> bool {
    const auto lhs_hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(lhs);
    const auto rhs_hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(rhs);
    if (lhs_hierarchy && rhs_hierarchy) {
        const auto lhs_depth = lhs_hierarchy->get_depth();
        const auto rhs_depth = rhs_hierarchy->get_depth();
        if (lhs_depth != rhs_depth) {
            return lhs_depth < rhs_depth;
        }
    }

    const auto lhs_type = lhs->get_type();
    const auto rhs_type = rhs->get_type();
    if (lhs_type != rhs_type) {
        return lhs_type < rhs_type;
    }

    const auto lhs_id = lhs->get_id();
    const auto rhs_id = rhs->get_id();
    return lhs_id < rhs_id;
}

[[nodiscard]] auto get_sorted(const std::vector<std::shared_ptr<erhe::Item_base>>& in_items) -> std::vector<std::shared_ptr<erhe::Item_base>>
{
    std::vector<std::shared_ptr<erhe::Item_base>> out_items = in_items;
    std::sort(out_items.begin(), out_items.end(), item_set_sort_predicate);
    return out_items;
}

void Selection::set_selection(const std::vector<std::shared_ptr<erhe::Item_base>>& selection)
{
    Scoped_selection_change selection_change{*this};

    for (auto& item : m_selection) {
        if (item->is_selected() && !is_in(item, selection)) {
            item->set_selected(false);
        }
    }
    for (auto& item : selection) {
        item->set_selected(true);
        update_last_selected(item);
    }

    m_selection = selection;
}

Scoped_selection_change::Scoped_selection_change(Selection& selection)
    : selection{selection}
{
    selection.begin_selection_change();
}

Scoped_selection_change::~Scoped_selection_change()
{
    selection.end_selection_change();
}

void Selection::begin_selection_change()
{
    ++m_selection_change_depth;
    if (m_selection_change_depth == 1) {
        m_begin_selection_change_state = m_selection;
    }
}

void Selection::end_selection_change()
{
    --m_selection_change_depth;
    ERHE_VERIFY(m_selection_change_depth >= 0);
    if (m_selection_change_depth > 0) {
        return;
    }
    const auto sorted_old = get_sorted(m_begin_selection_change_state);
    const auto sorted_new = get_sorted(m_selection);

    Editor_message selection_changed_message{
        .update_flags = Message_flag_bit::c_flag_bit_selection,
    };

    std::set_difference(
        sorted_old.begin(), sorted_old.end(),
        sorted_new.begin(), sorted_new.end(),
        std::back_inserter(selection_changed_message.no_longer_selected),
        item_set_sort_predicate
    );

    std::set_difference(
        sorted_new.begin(), sorted_new.end(),
        sorted_old.begin(), sorted_old.end(),
        std::back_inserter(selection_changed_message.newly_selected),
        item_set_sort_predicate
    );

    m_context.editor_message_bus->send_message(selection_changed_message);
}

auto Selection::on_viewport_select_try_ready() -> bool
{
    if (!m_hover_scene_view) {
        log_selection->trace("Selection has no hover scene view");
        return false;
    }

    const auto& content      = m_hover_scene_view->get_hover(Hover_entry::content_slot);
    const auto& tool         = m_hover_scene_view->get_hover(Hover_entry::tool_slot);
    const auto& rendertarget = m_hover_scene_view->get_hover(Hover_entry::rendertarget_slot);
    m_hover_mesh    = content.scene_mesh;
    m_hover_content = content.valid;
    m_hover_tool    = tool.valid;

    if (m_hover_content && !m_hover_tool && !rendertarget.valid) {
        log_selection->trace("Can select");
        return true;
    } else {
        if (!m_hover_content) {
            log_selection->trace("Cannot select: Not hovering over content");
        }
        if (!m_hover_tool) {
            log_selection->trace("Cannot select: Hovering over tool");
        }
        if (rendertarget.valid) {
            log_selection->trace("Cannot select: Hovering over rendertarget");
        }
    }
    return false;
}

auto Selection::on_viewport_select() -> bool
{
    auto shared_hover_mesh = std::static_pointer_cast<erhe::scene::Mesh>(m_hover_mesh->shared_from_this());
    auto shared_hover_node = m_hover_mesh->get_node()->shared_from_this();
    const bool was_selected = is_in_selection(shared_hover_mesh) || is_in_selection(shared_hover_node);
    if (m_context.input_state->control) {
        if (m_hover_content) {
            toggle_mesh_selection(shared_hover_mesh, was_selected, false);
            return true;
        }
        return false;
    }

    if (!m_hover_content) {
        clear_selection();
    } else {
        m_range_selection.reset();
        toggle_mesh_selection(shared_hover_mesh, was_selected, true);
    }
    return true;
}

auto Selection::on_viewport_select_toggle() -> bool
{
    auto shared_hover_mesh = std::static_pointer_cast<erhe::scene::Mesh>(m_hover_mesh->shared_from_this());
    auto shared_hover_node = m_hover_mesh->get_node()->shared_from_this();
    if (m_hover_content) {
        const bool was_selected = is_in_selection(shared_hover_mesh) || is_in_selection(shared_hover_node);
        toggle_mesh_selection(shared_hover_mesh, was_selected, false);
        return true;
    }
    return false;
}

auto Selection::clear_selection() -> bool
{
    Scoped_selection_change selection_change{*this};

    if (m_selection.empty()) {
        return false;
    }

    for (const auto& item : m_selection) {
        ERHE_VERIFY(item);
        if (!item) {
            continue;
        }
        item->set_selected(false);
    }

    log_selection->trace("Clearing selection ({} items were selected)", m_selection.size());
    m_selection.clear();
    m_range_selection.reset();
#if !defined(NDEBUG)
    sanity_check();
#endif

    return true;
}

void Selection::toggle_mesh_selection(const std::shared_ptr<erhe::scene::Mesh>& mesh, const bool was_selected, const bool clear_others)
{
    Scoped_selection_change selection_change{*this};

    const bool mesh_lock_viewport_select = erhe::bit::test_all_rhs_bits_set(
        mesh->get_flag_bits(),
        erhe::Item_flags::lock_viewport_selection
    );
    if (mesh_lock_viewport_select) {
        return;
    }

    erhe::scene::Node* const node = mesh->get_node();
    if (node == nullptr) {
        return;
    }

    const bool node_lock_viewport_select = erhe::bit::test_all_rhs_bits_set(
        node->get_flag_bits(),
        erhe::Item_flags::lock_viewport_selection
    );
    if (node_lock_viewport_select) {
        return;
    }

    bool add{false};
    bool remove{false};
    if (clear_others) {
        clear_selection();
        if (!was_selected && mesh) {
            add = true;
        }
    } else if (mesh) {
        if (was_selected) {
            remove = true;
        } else {
            add = true;
        }
    }

    ERHE_VERIFY(!add || !remove);

    const auto item = node->shared_from_this();

    if (add) {
        add_to_selection(item);
        m_range_selection.set_terminator(item);
    } else if (remove) {
        remove_from_selection(item);
    }
}

auto Selection::is_in_selection(const std::shared_ptr<erhe::Item_base>& item) const -> bool
{
    if (!item) {
        return false;
    }

    return std::find(m_selection.begin(), m_selection.end(), item) != m_selection.end();
}

auto Selection::add_to_selection(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    Scoped_selection_change selection_change{*this};

    if (!item) {
        log_selection->warn("Trying to add empty item to selection");
        return false;
    }

    update_last_selected(item);

    item->set_selected(true);

    if (!is_in_selection(item)) {
        log_selection->trace("Adding {} to selection", item->get_name());
        m_selection.push_back(item);
        return true;
    }

    log_selection->warn("Adding {} to selection failed - was already in selection", item->get_name());
    return false;
}

auto Selection::remove_from_selection(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    Scoped_selection_change selection_change{*this};

    if (!item) {
        log_selection->warn("Trying to remove empty item from selection");
        return false;
    }

    item->set_selected(false);

    const auto i = std::remove(m_selection.begin(), m_selection.end(), item);
    if (i != m_selection.end()) {
        log_selection->trace("Removing item {} from selection", item->get_name());
        m_selection.erase(i, m_selection.end());
        return true;
    }

    log_selection->trace("Removing item {} from selection failed - was not in selection", item->get_name());
    return false;
}

void Selection::update_selection_from_scene_item(const std::shared_ptr<erhe::Item_base>& item, const bool added)
{
    Scoped_selection_change selection_change{*this};

    if (item->is_selected() && added) {
        if (!is_in(item, m_selection)) {
            m_selection.push_back(item);
            update_last_selected(item);
        }
    } else {
        if (is_in(item, m_selection)) {
            const auto i = std::remove(m_selection.begin(), m_selection.end(), item);
            if (i != m_selection.end()) {
                m_selection.erase(i, m_selection.end());
            }
        }
    }
}

void Selection::sanity_check()
{
#if !defined(NDEBUG)
    std::size_t error_count{0};

    const auto& scene_roots = m_context.editor_scenes->get_scene_roots();
    for (const auto& scene_root : scene_roots) {
        const auto& scene = scene_root->get_scene();
        const auto& flat_nodes = scene.get_flat_nodes();
        for (const auto& node : flat_nodes) {
            const auto item = std::static_pointer_cast<erhe::Item_base>(node);
            if (
                node->is_selected() &&
                !is_in(item, m_selection)
            ) {
                log_selection->error("Node has selection flag set without being in selection");
                ++error_count;
            } else if (
                !node->is_selected() &&
                is_in(item, m_selection)
            ) {
                log_selection->error("Node does not have selection flag set while being in selection");
                ++error_count;
            }
        }
    }

    if (error_count > 0) {
        log_selection->error("Selection errors: {}", error_count);
    }
#endif
}

//// void Selection::imgui()
//// {
//// #if defined(ERHE_GUI_LIBRARY_IMGUI)
////     for (const auto& item : m_selection) {
////         if (!item) {
////             ImGui::BulletText("(empty)");
////         } else {
////             ImGui::BulletText(
////                 "%s %s %s",
////                 item->get_type_name(),
////                 item->get_name().c_str(),
////                 item->is_selected() ? "Ok" : "?!"
////             );
////         }
////     }
//// #endif
//// }

void Selection_tool::viewport_toolbar(bool& hovered)
{
    ImGui::PushID("Selection_tool::viewport_toolbar");
    const auto& icon_rasterication = m_context.icon_set->get_small_rasterization();

    int boost = get_priority_boost();
    const auto mode = boost > 0 ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal;

    erhe::imgui::begin_button_style(mode);
    const bool button_pressed = icon_rasterication.icon_button(
        ERHE_HASH("select"),
        m_context.icon_set->icons.select,
        glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
        glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        false
    );
    erhe::imgui::end_button_style(mode);
    if (ImGui::IsItemHovered()) {
        hovered = true;
        ImGui::SetTooltip(boost > 0 ? "De-prioritize Selection Tool" : "Prioritize Selection Tool");
    }
    if (button_pressed) {
        set_priority_boost(boost == 0 ? 10 : 0);
    }
    ImGui::PopID();
}

void Selection::update_last_selected(const std::shared_ptr<erhe::Item_base>& item)
{
    if (item->get_type() == erhe::Item_type::content_library_node) {
        const auto node = std::dynamic_pointer_cast<Content_library_node>(item);
        if (node) {
            const auto node_item = node->item;
            if (node_item) {
                m_last_selected_by_type[node_item->get_type()] = node_item;
            }
        }
    }

    m_last_selected_by_type[item->get_type()] = item;
}

auto Selection::get_last_selected(const uint64_t type) -> std::shared_ptr<erhe::Item_base>
{
    auto i = m_last_selected_by_type.find(type);
    if (i == m_last_selected_by_type.end()) {
        return {};
    }
    std::shared_ptr<erhe::Item_base> item = (*i).second.lock();
    return item;
}

}
