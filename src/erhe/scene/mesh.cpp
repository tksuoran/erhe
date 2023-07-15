#include "erhe/scene/mesh.hpp"

#include "erhe/scene/scene_host.hpp"
#include "erhe/toolkit/bit_helpers.hpp"

namespace erhe::scene
{


Mesh::Mesh()
{
}

Mesh::Mesh(const std::string_view name)
    : Node_attachment{name}
{
}

Mesh::Mesh(
    const std::string_view           name,
    const erhe::primitive::Primitive primitive
)
    : Node_attachment{name}
{
    mesh_data.primitives.emplace_back(primitive);
}

Mesh::~Mesh() noexcept
{
}

auto Mesh::static_type() -> uint64_t
{
    return Item_type::node_attachment | Item_type::mesh;
}

auto Mesh::static_type_name() -> const char*
{
    return "Mesh";
}

auto Mesh::get_type() const -> uint64_t
{
    return static_type();
}

auto Mesh::type_name() const -> const char*
{
    return static_type_name();
}

void Mesh::handle_node_scene_host_update(
    Scene_host* old_scene_host,
    Scene_host* new_scene_host
)
{
    if (old_scene_host) {
        old_scene_host->unregister_mesh(
            std::static_pointer_cast<Mesh>(shared_from_this())
        );
    }
    if (new_scene_host) {
        new_scene_host->register_mesh(
            std::static_pointer_cast<Mesh>(shared_from_this())
        );
    }
}

auto operator<(const Mesh& lhs, const Mesh& rhs) -> bool
{
    return lhs.m_id.get_id() < rhs.m_id.get_id();
}

using namespace erhe::toolkit;

auto is_mesh(const Item* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    return test_all_rhs_bits_set(item->get_type(), Item_type::mesh);
}

auto is_mesh(const std::shared_ptr<Item>& item) -> bool
{
    return is_mesh(item.get());
}

auto as_mesh(Item* const item) -> Mesh*
{
    if (item == nullptr) {
        return nullptr;
    }
    if (!test_all_rhs_bits_set(item->get_type(), Item_type::mesh)) {
        return nullptr;
    }
    return static_cast<Mesh*>(item);
}

auto as_mesh(const std::shared_ptr<Item>& item) -> std::shared_ptr<Mesh>
{
    if (!item) {
        return {};
    }
    if (!test_all_rhs_bits_set(item->get_type(), Item_type::mesh)) {
        return {};
    }
    return std::static_pointer_cast<Mesh>(item);
}

auto get_mesh(const erhe::scene::Node* const node) -> std::shared_ptr<Mesh>
{
    if (node == nullptr) {
        return {};
    }
    for (const auto& attachment : node->attachments()) {
        auto mesh = as_mesh(attachment);
        if (mesh) {
            return mesh;
        }
    }
    return {};
}

} // namespace erhe::scene

