#pragma once

#include "erhe/scene/viewport.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/projection.hpp"
#include "erhe/scene/transform.hpp"

#include <glm/glm.hpp>

#include <string>

namespace erhe::scene
{

class Viewport;

class ICamera
    : public Node
{
public:
    ICamera(const std::string_view name);

    virtual ~ICamera() {}
    virtual void update         (Viewport viewport) = 0;
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
    explicit Camera(const std::string_view name);
    ~Camera() override;

    auto node_type() const -> const char* override;

    void update(const Viewport viewport) override;

    // Implements ICamera
    auto projection     () -> Projection*                        override;
    auto projection     () const -> const Projection*            override;
    auto clip_from_node () const -> glm::mat4                    override;
    auto clip_from_world() const -> glm::mat4                    override;
    auto node_from_clip () const -> glm::mat4                    override;
    auto world_from_clip() const -> glm::mat4                    override;

private:
    Projection m_projection;

    class Transforms
    {
    public:
        Transform clip_from_node;
        Transform clip_from_world;
    };

    Transforms m_camera_transforms;
};

auto is_camera(const Node* const node) -> bool;
auto is_camera(const std::shared_ptr<Node>& node) -> bool;
auto as_camera(Node* const node) -> Camera*;
auto as_camera(const std::shared_ptr<Node>& node) -> std::shared_ptr<Camera>;

} // namespace erhe::scene
