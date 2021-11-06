#include "erhe/scene/camera.hpp"
#include "erhe/scene/log.hpp"

#include <gsl/gsl>

namespace erhe::scene
{

auto is_camera(const Node* const node) -> bool
{
    if (node == nullptr)
    {
        return false;
    }
    return (node->flag_bits() & Node::c_flag_bit_is_camera) == Node::c_flag_bit_is_camera;
}

auto is_camera(const std::shared_ptr<Node>& node) -> bool
{
    return is_camera(node.get());
}

auto as_camera(Node* const node) -> Camera*
{
    if (node == nullptr)
    {
        return nullptr;
    }
    if ((node->flag_bits() & Node::c_flag_bit_is_camera) == 0)
    {
        return nullptr;
    }
    return reinterpret_cast<Camera*>(node);
}

auto as_camera(const std::shared_ptr<Node>& node) -> std::shared_ptr<Camera>
{
    if (!node)
    {
        return {};
    }
    if ((node->flag_bits() & Node::c_flag_bit_is_camera) == 0)
    {
        return {};
    }
    return std::dynamic_pointer_cast<Camera>(node);
}

auto Camera::node_type() const -> const char*
{
    return "Camera";
}

ICamera::ICamera(const std::string_view name)
    : Node{name}
{
    m_flag_bits |= (Node::c_flag_bit_is_camera | Node::c_flag_bit_is_transform);
}

Camera::Camera(const std::string_view name)
    : ICamera{name}
{
    m_flag_bits |= (Node::c_flag_bit_is_camera | Node::c_flag_bit_is_transform);
}

Camera::~Camera() = default;

void Camera::update(const Viewport viewport)
{
    //log.trace("{} Camera::update()\n", name());

    // Update clip from view transform / view from clip
    m_projection.update(m_camera_transforms.clip_from_node, viewport);

    // Update clip from world / world from clip
    m_camera_transforms.clip_from_world.set(
        clip_from_node() * node_from_world(),
        world_from_node() * node_from_clip()
    );
}

auto Camera::projection() -> Projection*
{
    return &m_projection;
}

auto Camera::projection() const -> const Projection*
{
    return &m_projection;
}

auto Camera::clip_from_node() const -> glm::mat4
{
    return m_camera_transforms.clip_from_node.matrix();
}

auto Camera::clip_from_world() const -> glm::mat4
{
    return m_camera_transforms.clip_from_world.matrix();
}

auto Camera::node_from_clip() const -> glm::mat4
{
    return m_camera_transforms.clip_from_node.inverse_matrix();
}

auto Camera::world_from_clip() const -> glm::mat4
{
    return m_camera_transforms.clip_from_world.inverse_matrix();
}

} // namespace erhe::scene
