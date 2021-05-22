#include "erhe/scene/camera.hpp"

#include <gsl/gsl>

namespace erhe::scene
{

Camera::Camera(const std::string& name)
    : m_name(name)
{
}

auto Camera::name() const -> const std::string&
{
    return m_name;
}

void Camera::on_attach(Node& node)
{
    m_node = node.shared_from_this();
}

void Camera::on_detach(Node& node)
{
    m_node.reset();
}

void Camera::update(Viewport viewport)
{
    Expects(m_node != nullptr);

    // Update clip from view transform / view from clip
    m_projection.update(m_transforms.clip_from_node, viewport);

    // TODO(tksuoran@gmail.com): Implement&use cache
    m_node->update();

    // Update clip from world / world from clip
    m_transforms.clip_from_world.set(clip_from_node() * m_node->node_from_world(),
                                     m_node->world_from_node() * node_from_clip());
}

} // namespace erhe::scene
