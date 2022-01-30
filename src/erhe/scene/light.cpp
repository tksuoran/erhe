#include "erhe/scene/light.hpp"
#include "erhe/scene/camera.hpp"

#include <gsl/gsl>

namespace erhe::scene
{

Light::Light(const std::string_view name)
    : ICamera{name}
{
    m_flag_bits |= (Node::c_flag_bit_is_light | Node::c_flag_bit_is_transform);
}

Light::~Light() = default;

auto Light::node_type() const -> const char*
{
    return "Light";
}

auto Light::projection_transforms(const Viewport& viewport) const -> Projection_transforms
{
    const auto clip_from_node = m_projection.clip_from_node_transform(viewport);
    return Projection_transforms{
        clip_from_node,
        Transform{
            clip_from_node.matrix() * node_from_world(),
            world_from_node() * clip_from_node.inverse_matrix()
        }
    };
}

auto Light::get_exposure() const -> float
{
    return 1.0f;
}

void Light::set_exposure(const float value)
{
    static_cast<void>(value);
}

auto Light::texture_transform(const Transform& clip_from_world) const -> Transform
{
    return Transform{
        texture_from_clip * clip_from_world.matrix(),
        clip_from_world.inverse_matrix() * clip_from_texture
    };
}

auto Light::projection() -> Projection*
{
    return &m_projection;
}

auto Light::projection() const -> const Projection*
{
    return &m_projection;
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

auto as_light(const std::shared_ptr<Node>& node) -> std::shared_ptr<Light>
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
