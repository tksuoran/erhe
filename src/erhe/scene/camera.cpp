#include "erhe/scene/camera.hpp"
#include "erhe/scene/log.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <gsl/gsl>

namespace erhe::scene
{

auto is_icamera(const Node* const node) -> bool
{
    if (node == nullptr)
    {
        return false;
    }
    return (node->flag_bits() & Node_flag_bit::is_icamera) == Node_flag_bit::is_icamera;
}

auto is_icamera(const std::shared_ptr<Node>& node) -> bool
{
    return is_icamera(node.get());
}

auto as_icamera(Node* const node) -> ICamera*
{
    if (node == nullptr)
    {
        return nullptr;
    }
    if ((node->flag_bits() & Node_flag_bit::is_icamera) == 0)
    {
        return nullptr;
    }
    return reinterpret_cast<ICamera*>(node);
}

auto as_icamera(const std::shared_ptr<Node>& node) -> std::shared_ptr<ICamera>
{
    if (!node)
    {
        return {};
    }
    if ((node->flag_bits() & Node_flag_bit::is_icamera) == 0)
    {
        return {};
    }
    return std::dynamic_pointer_cast<ICamera>(node);
}
//
auto is_camera(const Node* const node) -> bool
{
    if (node == nullptr)
    {
        return false;
    }
    return (node->flag_bits() & Node_flag_bit::is_camera) == Node_flag_bit::is_camera;
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
    if ((node->flag_bits() & Node_flag_bit::is_camera) == 0)
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
    if ((node->flag_bits() & Node_flag_bit::is_camera) == 0)
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
    node_data.flag_bits |= (Node_flag_bit::is_icamera | Node_flag_bit::is_transform);
}

Camera::Camera(const std::string_view name)
    : ICamera{name}
{
    node_data.flag_bits |= (Node_flag_bit::is_camera | Node_flag_bit::is_transform);
}

Camera::~Camera() = default;

auto Camera::projection_transforms(const Viewport& viewport) const -> Projection_transforms
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

auto Camera::get_exposure() const -> float
{
    return m_exposure;
}

void Camera::set_exposure(const float value)
{
    m_exposure = value;
}

auto Camera::projection() -> Projection*
{
    return &m_projection;
}

auto Camera::projection() const -> const Projection*
{
    return &m_projection;
}

} // namespace erhe::scene
