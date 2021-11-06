#include "erhe/scene/scene.hpp"
#include "erhe/scene/log.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/toolkit/tracy_client.hpp"

#include <algorithm>

namespace erhe::scene
{

void Scene::sort_transform_nodes()
{
    log.trace("sorting {} nodes\n", nodes.size());

    std::sort(
        nodes.begin(),
        nodes.end(),
        [](const auto& lhs, const auto& rhs)
        {
            return lhs->depth() < rhs->depth();
        }
    );
    nodes_sorted = true;
}

auto Scene::transform_update_serial() -> uint64_t
{
    return ++m_transform_update_serial;
}

void Scene::update_node_transforms()
{
    ZoneScoped;

    if (!nodes_sorted)
    {
        sort_transform_nodes();
    }

    const auto serial = transform_update_serial();

    for (auto& node : nodes)
    {
        node->update_transform(serial);
    }
}

} // namespace erhe::scene

