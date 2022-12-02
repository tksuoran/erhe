#include "erhe/scene/mesh.hpp"

#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/scene/scene_host.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"

namespace erhe::scene
{


Mesh::Mesh()
{
    m_flag_bits |= (Scene_item_flags::mesh);
}

Mesh::Mesh(const std::string_view name)
    : Node_attachment{name}
{
    enable_flag_bits(Scene_item_flags::mesh);
}

Mesh::Mesh(
    const std::string_view           name,
    const erhe::primitive::Primitive primitive
)
    : Node_attachment{name}
{
    enable_flag_bits(Scene_item_flags::mesh);
    mesh_data.primitives.emplace_back(primitive);
}

Mesh::~Mesh() noexcept
{
}

auto Mesh::type_name() const -> const char*
{
    return "Mesh";
}

void Mesh::handle_node_scene_host_update(
    Scene_host* old_scene_host,
    Scene_host* new_scene_host
)
{
    if (old_scene_host)
    {
        Scene* scene = old_scene_host->get_scene();
        if (scene != nullptr)
        {
            scene->unregister_mesh(
                std::static_pointer_cast<Mesh>(shared_from_this())
            );
        }
    }
    if (new_scene_host)
    {
        Scene* scene = new_scene_host->get_scene();
        if (scene != nullptr)
        {
            scene->register_mesh(
                std::static_pointer_cast<Mesh>(shared_from_this())
            );
        }
    }
}

auto operator<(const Mesh& lhs, const Mesh& rhs) -> bool
{
    return lhs.m_id.get_id() < rhs.m_id.get_id();
}

auto is_mesh(const Scene_item* const scene_item) -> bool
{
    if (scene_item == nullptr)
    {
        return false;
    }
    using namespace erhe::toolkit;
    return test_all_rhs_bits_set(scene_item->get_flag_bits(), Scene_item_flags::mesh);
}

auto is_mesh(const std::shared_ptr<Scene_item>& scene_item) -> bool
{
    return is_mesh(scene_item.get());
}

auto as_mesh(Scene_item* const scene_item) -> Mesh*
{
    if (scene_item == nullptr)
    {
        return nullptr;
    }
    using namespace erhe::toolkit;
    if (!test_all_rhs_bits_set(scene_item->get_flag_bits(), Scene_item_flags::mesh))
    {
        return nullptr;
    }
    return reinterpret_cast<Mesh*>(scene_item);
}

auto as_mesh(const std::shared_ptr<Scene_item>& scene_item) -> std::shared_ptr<Mesh>
{
    if (!scene_item)
    {
        return {};
    }
    using namespace erhe::toolkit;
    if (!test_all_rhs_bits_set(scene_item->get_flag_bits(), Scene_item_flags::mesh))
    {
        return {};
    }
    return std::dynamic_pointer_cast<Mesh>(scene_item);
}

auto get_mesh(
    const erhe::scene::Node* const node
) -> std::shared_ptr<Mesh>
{
    for (const auto& attachment : node->attachments())
    {
        auto mesh = as_mesh(attachment);
        if (mesh)
        {
            return mesh;
        }
    }
    return {};
}

} // namespace erhe::scene

