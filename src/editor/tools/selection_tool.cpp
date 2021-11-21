#include "tools/selection_tool.hpp"
#include "log.hpp"
#include "tools.hpp"
#include "tools/pointer_context.hpp"
#include "tools/trs_tool.hpp"
#include "renderers/text_renderer.hpp"
#include "scene/scene_manager.hpp"
#include "scene/node_physics.hpp"

#include "erhe/scene/mesh.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"

#include <imgui.h>

namespace editor
{

using namespace erhe::toolkit;

Selection_tool::Selection_tool()
    : erhe::components::Component{c_name}
{
}

Selection_tool::~Selection_tool() = default;

void Selection_tool::connect()
{
    m_scene_manager = get<Scene_manager>();
}

void Selection_tool::initialize_component()
{
    get<Editor_tools>()->register_tool(this);
}

auto Selection_tool::description() -> const char*
{
    return c_name.data();
}

auto Selection_tool::state() const -> State
{
    return m_state;
}

Selection_tool::Subcription Selection_tool::subscribe_selection_change_notification(On_selection_changed callback)
{
    const int handle = m_next_selection_change_subscription++;
    m_selection_change_subscriptions.push_back({callback, handle});
    return Subcription(this, handle);
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

void Selection_tool::cancel_ready()
{
    m_state = State::Passive;
}

auto Selection_tool::update(Pointer_context& pointer_context) -> bool
{
    if (pointer_context.priority_action != Action::select)
    {
        if (m_state != State::Passive)
        {
            cancel_ready();
        }
        return false;
    }

    if (!pointer_context.scene_view_focus ||
        !pointer_context.pointer_in_content_area())
    {
        return false;
    }
    m_hover_mesh     = pointer_context.hover_mesh;
    m_hover_position = glm::vec3{
        pointer_context.pointer_x,
        pointer_context.pointer_y,
        pointer_context.pointer_z
    };
    m_hover_content  = pointer_context.hover_content;
    m_hover_tool     = pointer_context.hover_tool;

    if (m_state == State::Passive)
    {
        if (m_hover_content && pointer_context.mouse_button[Mouse_button_left].pressed)
        {
            m_state = State::Ready;
            return true;
        }
        return false;
    }

    if (m_state == State::Passive)
    {
        return false;
    }

    if (pointer_context.mouse_button[Mouse_button_left].released)
    {
        if (pointer_context.shift)
        {
            if (!m_hover_content)
            {
                return false;
            }
            toggle_selection(m_hover_mesh, false);
            m_state = State::Passive;
            return true;
        }
        if (!m_hover_content)
        {
            clear_selection();
            m_state = State::Passive;
            return true;
        }
        toggle_selection(m_hover_mesh, true);
        m_state = State::Passive;
        return true;
    }
    return false;
}

auto Selection_tool::clear_selection() -> bool
{
    if (m_selection.empty())
    {
        return false;
    }

    for (auto item : m_selection)
    {
        VERIFY(item);
        if (!item)
        {
            continue;
        }
        item->visibility_mask() &= ~erhe::scene::Node::c_visibility_selected;
    }

    log_selection.trace("Clearing selection ({} items were selected)\n", m_selection.size());
    m_selection.clear();
    call_selection_change_subscriptions();
    return true;
}

void Selection_tool::toggle_selection(
    std::shared_ptr<erhe::scene::Node> item,
    const bool                         clear_others
)
{
    if (clear_others)
    {
        const bool was_selected = is_in_selection(item);

        clear_selection();
        if (!was_selected && item)
        {
            add_to_selection(item);
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

auto Selection_tool::is_in_selection(std::shared_ptr<erhe::scene::Node> item) -> bool
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

auto Selection_tool::add_to_selection(std::shared_ptr<erhe::scene::Node> item) -> bool
{
    if (!item)
    {
        log_selection.warn("Trying to add empty item to selection\n");
        return false;
    }

    item->visibility_mask() |= erhe::scene::Node::c_visibility_selected;

    if (!is_in_selection(item))
    {
        log_selection.trace("Adding {} to selection\n", item->name());
        m_selection.push_back(item);
        return true;
    }

    log_selection.warn("Adding {} to selection failed - was already in selection\n", item->name());
    return false;
}

auto Selection_tool::remove_from_selection(std::shared_ptr<erhe::scene::Node> item) -> bool
{
    if (!item)
    {
        log_selection.warn("Trying to remove empty item from selection\n");
        return false;
    }

    item->visibility_mask() &= ~erhe::scene::Node::c_visibility_selected;

    const auto i = std::remove(
        m_selection.begin(),
        m_selection.end(),
        item
    );
    if (i != m_selection.end())
    {
        log_selection.trace("Removing item {} from selection\n", item->name());
        m_selection.erase(i, m_selection.end());
        call_selection_change_subscriptions();
        return true;
    }

    log_selection.info("Removing item {} from selection failed - was not in selection\n", item->name());
    return false;
}

void Selection_tool::call_selection_change_subscriptions()
{
    for (const auto& entry : m_selection_change_subscriptions)
    {
        entry.callback(m_selection);
    }
}

void Selection_tool::render(const Render_context& render_context)
{
    static_cast<void>(render_context);
}

}
