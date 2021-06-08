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
    virtual ~ICamera() {}
    virtual void update         (Viewport viewport) = 0;
    virtual auto node           () const -> const std::shared_ptr<Node>& = 0;
    virtual auto projection     () -> Projection* = 0;
    virtual auto projection     () const -> const Projection* = 0;
    virtual auto clip_from_node () const -> glm::mat4 = 0;
    virtual auto clip_from_world() const -> glm::mat4 = 0;
    virtual auto node_from_clip () const -> glm::mat4 = 0;
    virtual auto world_from_clip() const -> glm::mat4 = 0;
};

class Camera
    : public ICamera
    , public erhe::scene::INode_attachment
    , public std::enable_shared_from_this<Camera>
{
public:
    explicit Camera(std::string_view name);
    ~Camera() override;

    void update(Viewport viewport) override;

    // Implements INode_attachment
    auto name     () const -> const std::string&;
    void on_attach(Node& node);
    void on_detach(Node& node);

    // Implements ICamera
    auto node           () const -> const std::shared_ptr<Node>& override;
    auto projection     () -> Projection*                        override;
    auto projection     () const -> const Projection*            override;
    auto clip_from_node () const -> glm::mat4                    override;
    auto clip_from_world() const -> glm::mat4                    override;
    auto node_from_clip () const -> glm::mat4                    override;
    auto world_from_clip() const -> glm::mat4                    override;

    std::string           m_name;
    std::shared_ptr<Node> m_node;
    Projection            m_projection;

    struct Transforms
    {
        Transform clip_from_node;
        Transform clip_from_world;
    };

    Transforms m_transforms;
};

} // namespace erhe::scene
