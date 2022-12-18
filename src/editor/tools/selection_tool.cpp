#include "tools/selection_tool.hpp"

#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "operations/compound_operation.hpp"
#include "operations/insert_operation.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/render_context.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/tools.hpp"
#include "tools/trs_tool.hpp"
#include "windows/node_tree_window.hpp"
#include "windows/viewport_config.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#endif

#include "erhe/application/commands/commands.hpp"
#include "erhe/application/commands/command_binding.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/time.hpp"
#include "erhe/application/view.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/math_util.hpp"

namespace editor
{

using glm::mat4;
using glm::vec3;
using glm::vec4;

Range_selection::Range_selection(Selection_tool& selection_tool)
    : m_selection_tool{selection_tool}
{
}

void Range_selection::set_terminator(
    const std::shared_ptr<erhe::scene::Item>& item
)
{
    if (!m_primary_terminator)
    {
        log_selection->trace(
            "setting primary terminator to {} {}",
            item->type_name(),
            item->get_name()
        );

        m_primary_terminator = item;
        m_edited = true;
        return;
    }
    if (item == m_primary_terminator)
    {
        log_selection->trace(
            "ignoring setting terminator to {} - {} because it is already the primary terminator",
            item->type_name(),
            item->get_name()
        );
        return;
    }
    if (item == m_secondary_terminator)
    {
        log_selection->trace(
            "ignoring setting terminator to {} - {} because it is already the secondary terminator",
            item->type_name(),
            item->get_name()
        );
        return;
    }

    log_selection->trace("setting secondary terminator to {} {}", item->type_name(), item->get_name());
    m_secondary_terminator = item;
    m_edited = true;
}

void Range_selection::entry(
    const std::shared_ptr<erhe::scene::Item>& item,
    const bool                                attachments_expanded
)
{
    m_entries.push_back(item);
    if (!attachments_expanded)
    {
        const auto& node = as_node(item);
        if (node && !attachments_expanded)
        {
            for (const auto& attachment : node->attachments())
            {
                m_entries.push_back(attachment);
            }
        }
    }
}

void Range_selection::begin()
{
    m_edited = false;
    m_entries.clear();
}

void Range_selection::end(const bool attachments_expanded)
{
    if (m_entries.empty() || !m_edited || !m_primary_terminator || !m_secondary_terminator)
    {
        m_entries.clear();
        return;
    }
    log_selection->trace("setting selection since range was modified");

    std::vector<std::shared_ptr<erhe::scene::Item>> selection;
    bool between_terminators{false};

    auto primary_terminator   = m_primary_terminator;
    auto secondary_terminator = m_secondary_terminator;
    auto primary_node         = as_node(m_primary_terminator);
    auto secondary_node       = as_node(m_secondary_terminator);

    for (const auto& item : m_entries)
    {
        const bool match_primary   = item == primary_terminator;
        const bool match_secondary = item == secondary_terminator;
        if (match_primary || match_secondary)
        {
            log_selection->trace("   T. {} {} {}", item->type_name(), item->get_name(), item->get_id());
            selection.push_back(item);
            between_terminators = !between_terminators;
            if (!attachments_expanded && between_terminators)
            {
                if (match_secondary && primary_node && !primary_node->attachments().empty())
                {
                    primary_terminator = primary_node->attachments().back();
                }
                if (match_primary && secondary_node && !secondary_node->attachments().empty())
                {
                    secondary_terminator = secondary_node->attachments().back();
                }
            }
            else
            {
                primary_terminator   = m_primary_terminator;
                secondary_terminator = m_secondary_terminator;
            }
            continue;
        }
        if (between_terminators)
        {
            log_selection->trace("    + {} {} {}", item->type_name(), item->get_name(), item->get_id());
            selection.push_back(item);
        }
        else
        {
            log_selection->trace("    - {} {} {}", item->type_name(), item->get_name(), item->get_id());
        }
    }
    if (selection.empty())
    {
        m_entries.clear();
        return;
    }

    m_selection_tool.set_selection(selection);
    m_entries.clear();
}

void Range_selection::reset()
{
    log_selection->trace("resetting range selection");
    if (m_primary_terminator && m_secondary_terminator)
    {
        m_selection_tool.clear_selection();
    }
    m_primary_terminator.reset();
    m_secondary_terminator.reset();
}

void Selection_tool_select_command::try_ready(
    erhe::application::Command_context& context
)
{
    if (!m_selection_tool.is_enabled())
    {
        log_selection->trace("selection tool not enabled");
        return;
    }
    if (m_selection_tool.on_select_try_ready())
    {
        set_ready(context);
    }
}

auto Selection_tool_select_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    if (!m_selection_tool.is_enabled())
    {
        log_selection->trace("selection tool not enabled");
        return false;
    }

    if (get_command_state() != erhe::application::State::Ready)
    {
        log_selection->trace("selection tool not in ready state");
        return false;
    }

    const bool consumed = m_selection_tool.on_select();
    set_inactive(context);
    return consumed;
}

auto Selection_tool_delete_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    static_cast<void>(context);

    return m_selection_tool.delete_selection();
}

auto Selection_tool::delete_selection() -> bool
{
    if (m_selection.empty())
    {
        return false;
    }

    //const auto& scene_root = m_editor_scenes->get_scene_root();
    Compound_operation::Parameters compound_parameters;
    for (auto& item : m_selection)
    {
        auto node = as_node(item);
        if (!node)
        {
            // TODO
            continue;
        }
        compound_parameters.operations.push_back(
            std::make_shared<Node_insert_remove_operation>(
                Node_insert_remove_operation::Parameters{
                    .selection_tool = this,
                    .node           = node,
                    .parent         = node->parent().lock(),
                    .mode           = Scene_item_operation::Mode::remove,
                }
            )
        );
    }
    if (compound_parameters.operations.empty())
    {
        return false;
    }

    const auto op = std::make_shared<Compound_operation>(std::move(compound_parameters));
    get<Operation_stack>()->push(op);
    return true;
}

auto Selection_tool::range_selection() -> Range_selection&
{
    return m_range_selection;
}

auto Selection_tool::get_first_selected_node() -> std::shared_ptr<erhe::scene::Node>
{
    for (const auto& i : m_selection)
    {
        if (is_node(i))
        {
            return as_node(i);
        }
    }
    return {};
}

auto Selection_tool::get_first_selected_scene() -> std::shared_ptr<erhe::scene::Scene>
{
    for (const auto& i : m_selection)
    {
        if (is_scene(i))
        {
            return as_scene(i);
        }
    }
    return {};
}

Selection_tool::Selection_tool()
    : erhe::application::Imgui_window{c_title}
    , erhe::components::Component    {c_type_name}
    , m_select_command               {*this}
    , m_delete_command               {*this}
    , m_range_selection              {*this}
{
}

Selection_tool::~Selection_tool() noexcept
{
}

void Selection_tool::declare_required_components()
{
    require<erhe::application::Commands>();
    require<erhe::application::Imgui_windows>();
    require<Tools>();
}

void Selection_tool::initialize_component()
{
    get<Tools>()->register_tool(this);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
    hide();

    const auto commands = get<erhe::application::Commands>();
    commands->register_command                        (&m_select_command);
    commands->register_command                        (&m_delete_command);
    commands->bind_command_to_mouse_click             (&m_select_command, erhe::toolkit::Mouse_button_left);
    commands->bind_command_to_controller_trigger_click(&m_select_command);
    commands->bind_command_to_key                     (&m_delete_command, erhe::toolkit::Key_delete, true);
}

void Selection_tool::post_initialize()
{
    m_line_renderer_set  = get<erhe::application::Line_renderer_set>();
    m_editor_message_bus = get<Editor_message_bus>();
    m_editor_scenes      = get<Editor_scenes   >();
#if defined(ERHE_XR_LIBRARY_OPENXR)
    m_headset_view       = get<Headset_view    >();
#endif
    m_node_tree_window   = get<Node_tree_window>();
    m_viewport_config    = get<Viewport_config >();
    m_viewport_windows   = get<Viewport_windows>();
}

auto Selection_tool::description() -> const char*
{
    return c_title.data();
}

void Selection_tool::on_inactived()
{
}

auto Selection_tool::selection() const -> const std::vector<std::shared_ptr<erhe::scene::Item>>&
{
    return m_selection;
}

template <typename T>
[[nodiscard]] auto is_in(
    const T&              item,
    const std::vector<T>& items
) -> bool
{
    return std::find(
        items.begin(),
        items.end(),
        item
    ) != items.end();
}

void Selection_tool::set_selection(const std::vector<std::shared_ptr<erhe::scene::Item>>& selection)
{
    for (auto& item : m_selection)
    {
        if (
            item->is_selected() &&
            !is_in(item, selection)
        )
        {
            item->set_selected(false);
        }
    }
    for (auto& item : selection)
    {
        item->set_selected(true);
    }
    m_selection = selection;
    send_selection_change_message();
}

auto Selection_tool::on_select_try_ready() -> bool
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (m_headset_view)
    {
        const auto& content      = m_headset_view->get_hover(Hover_entry::content_slot);
        const auto& tool         = m_headset_view->get_hover(Hover_entry::tool_slot);
        const auto& rendertarget = m_headset_view->get_hover(Hover_entry::rendertarget_slot);
        m_hover_mesh    = content.mesh;
        m_hover_content = content.valid;
        m_hover_tool    = tool.valid;

        if (m_hover_content && !m_hover_tool && !rendertarget.valid)
        {
            log_selection->trace("Can select");
            return true;
        }
        else
        {
            if (!m_hover_content)
            {
                log_selection->trace("Cannot select: Not hovering over content");
            }
            if (!m_hover_tool)
            {
                log_selection->trace("Cannot select: Hovering over tool");
            }
            if (rendertarget.valid)
            {
                log_selection->trace("Cannot select: Hovering over rendertarget");
            }
        }
    }
#endif

    const auto viewport_window = m_viewport_windows->hover_window();
    if (!viewport_window)
    {
        return false;
    }

    const auto& content = viewport_window->get_hover(Hover_entry::content_slot);
    const auto& tool    = viewport_window->get_hover(Hover_entry::tool_slot);
    m_hover_mesh    = content.mesh;
    m_hover_content = content.valid;
    m_hover_tool    = tool.valid;

    return m_hover_content;
}

auto Selection_tool::on_select() -> bool
{
    log_selection->trace("on select");

    const bool was_selected = is_in_selection(m_hover_mesh);
    if (m_viewport_windows->control_key_down())
    {
        if (m_hover_content)
        {
            toggle_mesh_selection(m_hover_mesh, was_selected, false);
            return true;
        }
        return false;
    }

    if (!m_hover_content)
    {
        clear_selection();
    }
    else
    {
        m_range_selection.reset();
        toggle_mesh_selection(m_hover_mesh, was_selected, true);
    }
    return true;
}

auto Selection_tool::clear_selection() -> bool
{
    if (m_selection.empty())
    {
        return false;
    }

    for (const auto& item : m_selection)
    {
        ERHE_VERIFY(item);
        if (!item)
        {
            continue;
        }
        item->set_selected(false);
    }

    log_selection->trace("Clearing selection ({} items were selected)", m_selection.size());
    m_selection.clear();
    m_range_selection.reset();
    sanity_check();
    send_selection_change_message();
    return true;
}

void Selection_tool::toggle_mesh_selection(
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    const bool                                was_selected,
    const bool                                clear_others
)
{
    bool add{false};
    bool remove{false};
    if (clear_others)
    {
        clear_selection();
        if (!was_selected && mesh)
        {
            add = true;
        }
    }
    else if (mesh)
    {
        if (was_selected)
        {
            remove = true;
        }
        else
        {
            add = true;
        }
    }

    ERHE_VERIFY(!add || !remove);

    if (add)
    {
        if (m_node_tree_window->expand_attachments())
        {
            add_to_selection(mesh);
            m_range_selection.set_terminator(mesh);
        }
        else
        {
            erhe::scene::Node* const node = mesh->get_node();
            const auto node_item = node->shared_from_this();
            add_to_selection(node_item);
            m_range_selection.set_terminator(node_item);
            for (auto& attachment : node->attachments())
            {
                add_to_selection(attachment);
                m_range_selection.set_terminator(attachment);
            }
        }
    }
    else if (remove)
    {
        if (m_node_tree_window->expand_attachments())
        {
            remove_from_selection(mesh);
        }
        else
        {
            erhe::scene::Node* const node = mesh->get_node();
            const auto node_item = node->shared_from_this();
            remove_from_selection(node_item);
            m_range_selection.set_terminator(node_item);
            for (auto& attachment : node->attachments())
            {
                remove_from_selection(attachment);
            }
        }
    }

    send_selection_change_message();
}

auto Selection_tool::is_in_selection(
    const std::shared_ptr<erhe::scene::Item>& item
) const -> bool
{
    if (!item)
    {
        return false;
    }

    return std::find(
        m_selection.begin(),
        m_selection.end(),
        item
    ) != m_selection.end();
}

    auto Selection_tool::add_to_selection(
    const std::shared_ptr<erhe::scene::Item>& item
) -> bool
{
    if (!item)
    {
        log_selection->warn("Trying to add empty item to selection");
        return false;
    }

    item->set_selected(true);

    if (!is_in_selection(item))
    {
        log_selection->trace("Adding {} to selection", item->get_name());
        m_selection.push_back(item);
        send_selection_change_message();
        return true;
    }

    log_selection->warn("Adding {} to selection failed - was already in selection", item->get_name());
    return false;
}

auto Selection_tool::remove_from_selection(
    const std::shared_ptr<erhe::scene::Item>& item
) -> bool
{
    if (!item)
    {
        log_selection->warn("Trying to remove empty item from selection");
        return false;
    }

    item->set_selected(false);

    const auto i = std::remove(
        m_selection.begin(),
        m_selection.end(),
        item
    );
    if (i != m_selection.end())
    {
        log_selection->trace("Removing item {} from selection", item->get_name());
        m_selection.erase(i, m_selection.end());
        send_selection_change_message();
        return true;
    }

    log_selection->trace("Removing item {} from selection failed - was not in selection", item->get_name());
    return false;
}

void Selection_tool::update_selection_from_scene_item(
    const std::shared_ptr<erhe::scene::Item>& item,
    const bool                                added
)
{
    if (item->is_selected() && added)
    {
        if (!is_in(item, m_selection))
        {
            m_selection.push_back(item);
            send_selection_change_message();
        }
    }
    else
    {
        if (is_in(item, m_selection))
        {
            const auto i = std::remove(
                m_selection.begin(),
                m_selection.end(),
                item
            );
            if (i != m_selection.end())
            {
                m_selection.erase(i, m_selection.end());
                send_selection_change_message();
            }
        }
    }
}

void Selection_tool::sanity_check()
{
#if !defined(NDEBUG)
    std::size_t error_count{0};

    const auto& scene_roots = m_editor_scenes->get_scene_roots();
    for (const auto& scene_root : scene_roots)
    {
        const auto& scene = scene_root->scene();
        const auto& flat_nodes = scene.get_flat_nodes();
        for (const auto& node : flat_nodes)
        {
            const auto item = std::static_pointer_cast<erhe::scene::Item>(node);
            if (
                node->is_selected() &&
                !is_in(item, m_selection)
            )
            {
                log_selection->error("Node has selection flag set without being in selection");
                ++error_count;
            }
            else if (
                !node->is_selected() &&
                is_in(item, m_selection)
            )
            {
                log_selection->error("Node does not have selection flag set while being in selection");
                ++error_count;
            }
        }
    }

    if (error_count > 0)
    {
        log_selection->error("Selection errors: {}", error_count);
    }
#endif
}

void Selection_tool::send_selection_change_message() const
{
    m_editor_message_bus->send_message(
        Editor_message{
            .update_flags = Message_flag_bit::c_flag_bit_selection
        }
    );
}

void Selection_tool::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    for (const auto& item : m_selection)
    {
        if (!item)
        {
            ImGui::BulletText("(empty)");
        }
        else
        {
            ImGui::BulletText(
                "%s %s %s",
                item->type_name(),
                item->get_name().c_str(),
                item->is_selected() ? "Ok" : "?!"
            );
        }
    }
#endif
}

}
