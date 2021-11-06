#include "erhe/scene/light.hpp"
#include "erhe/scene/camera.hpp"

#include <gsl/gsl>

namespace erhe::scene
{

Light::Light(const std::string_view name)
    : ICamera{name}
{
    m_flag_bits |= (Node::c_flag_bit_is_camera | Node::c_flag_bit_is_transform);
}

Light::~Light() = default;

auto Light::node_type() const -> const char*
{
    return "Light";
}

void Light::update(const Viewport viewport)
{
    // Update clip from view transform / view from clip
    m_projection.update(m_light_transforms.clip_from_node, viewport);

    // Update clip from world / world from clip
    m_light_transforms.clip_from_world.set(
        clip_from_node() * node_from_world(),
        world_from_node() * node_from_clip()
    );

    m_light_transforms.texture_from_world.set(
        texture_from_clip * clip_from_world(),
        world_from_clip() * clip_from_texture
    );
}

auto Light::projection() -> Projection*
{
    return &m_projection;
}

auto Light::projection() const -> const Projection*
{
    return &m_projection;
}

auto Light::clip_from_node() const -> glm::mat4
{
    return m_light_transforms.clip_from_node.matrix();
}

auto Light::clip_from_world() const -> glm::mat4
{
    return m_light_transforms.clip_from_world.matrix();
}

auto Light::node_from_clip() const -> glm::mat4
{
    return m_light_transforms.clip_from_node.inverse_matrix();
}

auto Light::world_from_clip() const -> glm::mat4
{
    return m_light_transforms.clip_from_world.inverse_matrix();
}

auto Light::texture_from_world() const -> glm::mat4
{
    return m_light_transforms.texture_from_world.matrix();
}

auto Light::world_from_texture() const -> glm::mat4
{
    return m_light_transforms.texture_from_world.inverse_matrix();
}

auto is_light(const Node* const node) -> bool
{
    if (node == nullptr)
    {
        return false;
    }
    return (node->flag_bits() & Node::c_flag_bit_is_light) == Node::c_flag_bit_is_light;
}

auto is_light(const std::shared_ptr<Node>& node) -> bool
{
    return is_light(node.get());
}

auto as_light(Node* const node) -> Light*
{
    if (node == nullptr)
    {
        return nullptr;
    }
    if ((node->flag_bits() & Node::c_flag_bit_is_light) == 0)
    {
        return nullptr;
    }
    return reinterpret_cast<Light*>(node);
}

auto as_light(std::shared_ptr<Node> node) -> std::shared_ptr<Light>
{
    if (!node)
    {
        return {};
    }
    if ((node->flag_bits() & Node::c_flag_bit_is_light) == 0)
    {
        return {};
    }
    return std::dynamic_pointer_cast<Light>(node);
}

} // namespace erhe::scene
