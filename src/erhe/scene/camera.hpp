#pragma once

#include "erhe/scene/viewport.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/projection.hpp"
#include "erhe/scene/transform.hpp"

#include <glm/glm.hpp>

#include <string>

namespace erhe::scene
{

struct Viewport;

class ICamera
{
public:
    virtual void update         (Viewport viewport) = 0;
    virtual auto node           () const -> Node* = 0;
    virtual auto projection     () -> Projection* = 0;
    virtual auto projection     () const -> const Projection* = 0;
    virtual auto clip_from_node () const -> glm::mat4 = 0;
    virtual auto clip_from_world() const -> glm::mat4 = 0;
    virtual auto node_from_clip () const -> glm::mat4 = 0;
    virtual auto world_from_clip() const -> glm::mat4 = 0;
};

class Camera
    : public ICamera
{
public:
    Camera(const std::string& name, 
           Node*              node)
        : m_name(name)
        , m_node(node)
    {
    }

    virtual ~Camera() = default;

    void update(Viewport viewport) override;

    auto name() const -> const std::string&
    {
        return m_name;
    }

    auto node() const -> Node* override
    {
        return m_node;
    }

    auto projection() -> Projection* override
    {
        return &m_projection;
    }

    auto projection() const -> const Projection* override
    {
        return &m_projection;
    }

    auto clip_from_node() const -> glm::mat4 override
    {
        return m_transforms.clip_from_node.matrix();
    }

    auto clip_from_world() const -> glm::mat4 override
    {
        return m_transforms.clip_from_world.matrix();
    }

    auto node_from_clip() const -> glm::mat4 override
    {
        return m_transforms.clip_from_node.inverse_matrix();
    }

    auto world_from_clip() const -> glm::mat4 override
    {
        return m_transforms.clip_from_world.inverse_matrix();
    }

    std::string m_name;
    Node*       m_node{nullptr};
    Projection  m_projection;

    struct Transforms
    {
        Transform clip_from_node;
        Transform clip_from_world;
    };

    Transforms m_transforms;
};

} // namespace erhe::scene
