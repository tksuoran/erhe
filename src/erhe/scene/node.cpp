#include "erhe/scene/node.hpp"
#include "erhe/log/log.hpp"

namespace erhe::scene
{

void Node::update(uint32_t update_serial, bool cache_enable)
{
    if (cache_enable)
    {
        if (m_updated)
        {
            return;
        }

        if (m_last_update_serial == update_serial)
        {
            return;
        }
    }

    m_last_update_serial = update_serial;
    VERIFY(parent != this);

    if (parent != nullptr)
    {
        parent->update(update_serial, cache_enable);
        transforms.world_from_node.set(parent->world_from_node() * parent_from_node(),
                                       node_from_parent() * parent->node_from_world());
    }
    else
    {
        transforms.world_from_node.set(parent_from_node(),
                                       node_from_parent());
    }
}

} // namespace erhe::scene
