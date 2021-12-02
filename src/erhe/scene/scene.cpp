#include "erhe/scene/scene.hpp"
#include "erhe/scene/log.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/toolkit/profile.hpp"

#include <algorithm>

namespace erhe::scene
{

Mesh_layer::Mesh_layer(const std::string_view name)
    : name{name}
{
}

Light_layer::Light_layer(const std::string_view name)
    : name{name}
{
}

void Scene::sanity_check() const
{
    for (auto& node : nodes)
    {
        node->sanity_check();
    }
}

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
    ERHE_PROFILE_FUNCTION

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

