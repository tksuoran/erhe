#include "erhe_scene/mesh.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_bit/bit_helpers.hpp"

namespace erhe::scene
{

Mesh::Mesh()
    : Node_attachment{erhe::Unique_id<Mesh>{}.get_id()}
{
}

Mesh::Mesh(const std::string_view name)
    : Node_attachment{name, erhe::Unique_id<Mesh>{}.get_id()}
{
}

Mesh::Mesh(
    const std::string_view           name,
    const erhe::primitive::Primitive primitive
)
    : Node_attachment{name, erhe::Unique_id<Mesh>{}.get_id()}
{
    mesh_data.primitives.emplace_back(primitive);
}

Mesh::~Mesh() noexcept = default;

auto Mesh::get_static_type() -> uint64_t
{
    return Item_type::node_attachment | erhe::Item_type::mesh;
}

auto Mesh::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Mesh::get_type_name() const -> std::string_view
{
    return static_type_name;
}

void Mesh::handle_item_host_update(
    erhe::Item_host* const old_item_host,
    erhe::Item_host* const new_item_host
)
{
    const auto shared_this = std::static_pointer_cast<Mesh>(shared_from_this()); // keep alive

    Scene_host* old_scene_host = static_cast<Scene_host*>(old_item_host);
    Scene_host* new_scene_host = static_cast<Scene_host*>(new_item_host);

    if (old_scene_host) {
        old_scene_host->unregister_mesh(shared_this);
    }
    if (new_scene_host) {
        new_scene_host->register_mesh(shared_this);
    }
}

auto operator<(const Mesh& lhs, const Mesh& rhs) -> bool
{
    return lhs.get_id() < rhs.get_id();
}

auto is_mesh(const Item* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    return erhe::bit::test_all_rhs_bits_set(item->get_type(), erhe::Item_type::mesh);
}

auto is_mesh(const std::shared_ptr<erhe::Item>& item) -> bool
{
    return is_mesh(item.get());
}

auto get_mesh(const erhe::scene::Node* const node) -> std::shared_ptr<Mesh>
{
    if (node == nullptr) {
        return {};
    }
    for (const auto& attachment : node->get_attachments()) {
        auto mesh = as<Mesh>(attachment);
        if (mesh) {
            return mesh;
        }
    }
    return {};
}

} // namespace erhe::scene

