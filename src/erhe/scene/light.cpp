#include "erhe/scene/light.hpp"
#include "erhe/scene/camera.hpp"

#include <gsl/gsl>

namespace erhe::scene
{

Light::Light(std::string_view name)
    : m_name{name}
{
}

Light::~Light() = default;

auto Light::name() const -> const std::string&
{
    return m_name;
}

void Light::on_attach(Node& node)
{
    m_node = node.shared_from_this();
}

void Light::on_detach(Node& node)
{
    m_node.reset();
}

void Light::update(Viewport viewport)
{
    Expects(m_node != nullptr);

    // Update clip from view transform / view from clip
    m_projection.update(m_transforms.clip_from_node, viewport);

    // TODO(tksuoran@gmail.com): Implement&use cache
    m_node->update();

    // Update clip from world / world from clip
    m_transforms.clip_from_world.set(clip_from_node() * m_node->node_from_world(),
                                     m_node->world_from_node() * node_from_clip());

    m_transforms.texture_from_world.set(texture_from_clip * clip_from_world(),
                                        world_from_clip() * clip_from_texture);
}

auto Light::node() const -> const std::shared_ptr<Node>&
{
    return m_node;
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
    return m_transforms.clip_from_node.matrix();
}

auto Light::clip_from_world() const -> glm::mat4
{
    return m_transforms.clip_from_world.matrix();
}

auto Light::node_from_clip() const -> glm::mat4
{
    return m_transforms.clip_from_node.inverse_matrix();
}

auto Light::world_from_clip() const -> glm::mat4
{
    return m_transforms.clip_from_world.inverse_matrix();
}

auto Light::texture_from_world() const -> glm::mat4
{
    return m_transforms.texture_from_world.matrix();
}

auto Light::world_from_texture() const -> glm::mat4
{
    return m_transforms.texture_from_world.inverse_matrix();
}

} // namespace erhe::scene
