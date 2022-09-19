#include "tools/selection_tool.hpp"

#include "editor_log.hpp"
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
#include "windows/viewport_config.hpp"
#include "xr/headset_view.hpp"

#include "erhe/application/commands/commands.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/time.hpp"
#include "erhe/application/view.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
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
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    if (!m_primary_terminator)
    {
        log_selection->trace("setting primary terminator to {} {}", node->node_type(), node->name());
        m_primary_terminator = node;
        m_edited = true;
        return;
    }
    if (node == m_primary_terminator)
    {
        log_selection->trace(
            "ignoring setting terminator to {} - {} because it is already the primary terminator",
            node->node_type(),
            node->name()
        );
        return;
    }
    if (node == m_secondary_terminator)
    {
        log_selection->trace(
            "ignoring setting terminator to {} - {} because it is already the secondary terminator",
            node->node_type(),
            node->name()
        );
        return;
    }
    log_selection->trace("setting secondary terminator to {} {}", node->node_type(), node->name());
    m_secondary_terminator = node;
    m_edited = true;
}

void Range_selection::entry(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    m_entries.push_back(node);
}

void Range_selection::begin()
{
    m_edited = false;
    m_entries.clear();
}

void Range_selection::end()
{
    if (m_entries.empty() || !m_edited || !m_primary_terminator || !m_secondary_terminator)
    {
        m_entries.clear();
        return;
    }
    log_selection->trace("setting selection since range was modified");

    std::vector<std::shared_ptr<erhe::scene::Node>> selection;
    bool                                            between_terminators{false};
    for (const auto& node : m_entries)
    {
        if (
            (node == m_primary_terminator) ||
            (node == m_secondary_terminator)
        )
        {
            log_selection->trace("   T. {} {} {}", node->node_type(), node->name(), node->get_id());
            selection.push_back(node);
            between_terminators = !between_terminators;
            continue;
        }
        if (between_terminators)
        {
            log_selection->trace("    + {} {} {}", node->node_type(), node->name(), node->get_id());
            selection.push_back(node);
        }
        else
        {
            log_selection->trace("    - {} {} {}", node->node_type(), node->name(), node->get_id());
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
    if (m_selection_tool.mouse_select_try_ready())
    {
        set_ready(context);
    }
}

auto Selection_tool_select_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    if (state() != erhe::application::State::Ready)
    {
        return false;
    }

    const bool consumed = m_selection_tool.on_mouse_select();
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
    for (auto& node : m_selection)
    {
        // TODO Handle all node types
        auto* scene_root = reinterpret_cast<Scene_root*>(node->node_data.host);
        if (is_mesh(node))
        {
            const auto mesh = as_mesh(node);
            compound_parameters.operations.push_back(
                std::make_shared<Mesh_insert_remove_operation>(
                    Mesh_insert_remove_operation::Parameters{
                        .scene_root     = scene_root,
                        .mesh           = mesh,
                        .node_physics   = get_physics_node(mesh.get()),
                        .node_raytrace  = get_raytrace(mesh.get()),
                        .parent         = mesh->parent().lock(),
                        .mode           = Scene_item_operation::Mode::remove,
                    }
                )
            );
        }
        else if (is_light(node))
        {
            const auto light = as_light(node);
            compound_parameters.operations.push_back(
                std::make_shared<Light_insert_remove_operation>(
                    Light_insert_remove_operation::Parameters{
                        .scene_root = scene_root,
                        .light      = light,
                        .parent     = light->parent().lock(),
                        .mode       = Scene_item_operation::Mode::remove,
                    }
                )
            );
        }
        else if (is_camera(node))
        {
            const auto camera = as_camera(node);
            compound_parameters.operations.push_back(
                std::make_shared<Camera_insert_remove_operation>(
                    Camera_insert_remove_operation::Parameters{
                        .scene_root = scene_root,
                        .camera     = camera,
                        .parent     = camera->parent().lock(),
                        .mode       = Scene_item_operation::Mode::remove,
                    }
                )
            );
        }
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

Selection_tool::Selection_tool()
    : erhe::application::Imgui_window{c_title, c_type_name}
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

    commands->register_command                  (&m_select_command);
    commands->register_command                  (&m_delete_command);
    commands->bind_command_to_mouse_click       (&m_select_command, erhe::toolkit::Mouse_button_left);
    commands->bind_command_to_controller_trigger(&m_select_command, 0.5f, 1.0f);
    commands->bind_command_to_key               (&m_delete_command, erhe::toolkit::Key_delete, true);
}

void Selection_tool::post_initialize()
{
    m_line_renderer_set = get<erhe::application::Line_renderer_set>();
    m_editor_scenes     = get<Editor_scenes   >();
    m_headset_view      = get<Headset_view    >();
    m_viewport_config   = get<Viewport_config >();
    m_viewport_windows  = get<Viewport_windows>();
}

auto Selection_tool::description() -> const char*
{
    return c_title.data();
}

auto Selection_tool::subscribe_selection_change_notification(
    On_selection_changed callback
) -> Selection_tool::Subcription
{
    const int handle = m_next_selection_change_subscription++;
    m_selection_change_subscriptions.push_back({callback, handle});
    return Subcription(this, handle);
}

auto Selection_tool::selection() const -> const Selection&
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

void Selection_tool::set_selection(const Selection& selection)
{
    for (auto& node : m_selection)
    {
        if (
            node->is_selected() &&
            !is_in(node, selection)
        )
        {
            const auto mask = node->get_visibility_mask();
            node->set_visibility_mask(mask & ~erhe::scene::Node_visibility::selected);
        }
    }
    for (auto& node : selection)
    {
        const auto mask = node->get_visibility_mask();
        node->set_visibility_mask(mask | erhe::scene::Node_visibility::selected);
    }
    m_selection = selection;
    call_selection_change_subscriptions();
}

void Selection_tool::unsubscribe_selection_change_notification(int handle)
{
    m_selection_change_subscriptions.erase(
        std::remove_if(
            m_selection_change_subscriptions.begin(),
            m_selection_change_subscriptions.end(),
            [=](Subscription_entry& entry) -> bool
            {
                return entry.handle == handle;
            }
        ),
        m_selection_change_subscriptions.end()
    );
}

auto Selection_tool::mouse_select_try_ready() -> bool
{
    if (m_headset_view)
    {
        const auto& content = m_headset_view->get_hover(Hover_entry::content_slot);
        const auto& tool    = m_headset_view->get_hover(Hover_entry::tool_slot);
        m_hover_mesh    = content.mesh;
        m_hover_content = content.valid;
        m_hover_tool    = tool.valid;

        return m_hover_content;
    }

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

auto Selection_tool::on_mouse_select() -> bool
{
    if (m_viewport_windows->control_key_down())
    {
        if (m_hover_content)
        {
            toggle_selection(m_hover_mesh, false);
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
        toggle_selection(m_hover_mesh, true);
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
        const auto mask = item->get_visibility_mask();
        item->set_visibility_mask(mask & ~erhe::scene::Node_visibility::selected);
    }

    log_selection->trace("Clearing selection ({} items were selected)", m_selection.size());
    m_selection.clear();
    m_range_selection.reset();
    sanity_check();
    call_selection_change_subscriptions();
    return true;
}

void Selection_tool::toggle_selection(
    const std::shared_ptr<erhe::scene::Node>& item,
    const bool                                clear_others
)
{
    if (clear_others)
    {
        const bool was_selected = is_in_selection(item);

        clear_selection();
        if (!was_selected && item)
        {
            add_to_selection(item);
            m_range_selection.set_terminator(item);
        }
    }
    else if (item)
    {
        if (is_in_selection(item))
        {
            remove_from_selection(item);
        }
        else
        {
            add_to_selection(item);
        }
    }

    call_selection_change_subscriptions();
}

auto Selection_tool::is_in_selection(
    const std::shared_ptr<erhe::scene::Node>& item
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
    const std::shared_ptr<erhe::scene::Node>& item
) -> bool
{
    if (!item)
    {
        log_selection->warn("Trying to add empty item to selection");
        return false;
    }

    const auto mask = item->get_visibility_mask();
    item->set_visibility_mask(
        mask | erhe::scene::Node_visibility::selected
    );

    if (!is_in_selection(item))
    {
        log_selection->trace("Adding {} to selection", item->name());
        m_selection.push_back(item);
        call_selection_change_subscriptions();
        return true;
    }

    log_selection->warn("Adding {} to selection failed - was already in selection", item->name());
    return false;
}

auto Selection_tool::remove_from_selection(
    const std::shared_ptr<erhe::scene::Node>& item
) -> bool
{
    if (!item)
    {
        log_selection->warn("Trying to remove empty item from selection");
        return false;
    }

    const auto mask = item->get_visibility_mask();
    item->set_visibility_mask(mask & ~erhe::scene::Node_visibility::selected);

    const auto i = std::remove(
        m_selection.begin(),
        m_selection.end(),
        item
    );
    if (i != m_selection.end())
    {
        log_selection->trace("Removing item {} from selection", item->name());
        m_selection.erase(i, m_selection.end());
        call_selection_change_subscriptions();
        return true;
    }

    log_selection->info("Removing item {} from selection failed - was not in selection", item->name());
    return false;
}

void Selection_tool::update_selection_from_node(
    const std::shared_ptr<erhe::scene::Node>& node,
    const bool                                added
)
{
    if (node->is_selected() && added)
    {
        if (!is_in(node, m_selection))
        {
            m_selection.push_back(node);
            call_selection_change_subscriptions();
        }
    }
    else
    {
        if (is_in(node, m_selection))
        {
            const auto i = std::remove(
                m_selection.begin(),
                m_selection.end(),
                node
            );
            if (i != m_selection.end())
            {
                m_selection.erase(i, m_selection.end());
                call_selection_change_subscriptions();
            }
        }
    }
}

void Selection_tool::sanity_check()
{
    std::size_t error_count{0};

    const auto& scene_roots = m_editor_scenes->get_scene_roots();
    for (const auto& scene_root : scene_roots)
    {
        const auto& scene = scene_root->scene();
        for (const auto& node : scene.flat_node_vector)
        {
            if (
                node->is_selected() &&
                !is_in(node, m_selection)
            )
            {
                log_selection->error("Node has selection flag set without being in selection");
                ++error_count;
            }
            else if (
                !node->is_selected() &&
                is_in(node, m_selection)
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
}

void Selection_tool::call_selection_change_subscriptions() const
{
    for (const auto& entry : m_selection_change_subscriptions)
    {
        entry.callback(m_selection);
    }
}

void Selection_tool::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    for (const auto& node : m_selection)
    {
        if (!node)
        {
            ImGui::BulletText("(empty)");
        }
        else
        {
            ImGui::BulletText(
                "%s %s %s",
                node->node_type(),
                node->name().c_str(),
                node->is_selected() ? "Ok" : "?!"
            );
        }
    }
#endif
}

}
