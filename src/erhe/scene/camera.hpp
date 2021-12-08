#pragma once

#include "erhe/scene/viewport.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/projection.hpp"
#include "erhe/scene/transform.hpp"

#include <glm/glm.hpp>

#include <array>
#include <string>

namespace erhe::scene
{

class Viewport;

class Projection_transforms
{
public:
    Transform clip_from_node;
    Transform clip_from_world;
};

//Transforms m_camera_transforms;

class ICamera
    : public Node
{
public:
    ICamera(const std::string_view name);
    virtual ~ICamera() {}

    virtual [[nodiscard]] auto projection           () -> Projection* = 0;
    virtual [[nodiscard]] auto projection           () const -> const Projection* = 0;
    virtual [[nodiscard]] auto projection_transforms(Viewport viewport) const -> Projection_transforms = 0;
    //virtual [[nodiscard]] auto clip_from_node () const -> glm::mat4 = 0;
    //virtual [[nodiscard]] auto clip_from_world() const -> glm::mat4 = 0;
    //virtual [[nodiscard]] auto node_from_clip () const -> glm::mat4 = 0;
    //virtual [[nodiscard]] auto world_from_clip() const -> glm::mat4 = 0;
};

class Camera
    : public ICamera
{
public:
    explicit Camera(const std::string_view name);
    ~Camera() override;

    [[nodiscard]] auto node_type() const -> const char* override;

    //void update(const Viewport viewport) override;

    // Implements ICamera
    [[nodiscard]] auto projection           () -> Projection*                                  override;
    [[nodiscard]] auto projection           () const -> const Projection*                      override;
    [[nodiscard]] auto projection_transforms(Viewport viewport) const -> Projection_transforms override;

    //[[nodiscard]] auto clip_from_node () const -> glm::mat4         override;
    //[[nodiscard]] auto clip_from_world() const -> glm::mat4         override;
    //[[nodiscard]] auto node_from_clip () const -> glm::mat4         override;
    //[[nodiscard]] auto world_from_clip() const -> glm::mat4         override;

private:
    Projection m_projection;

    //class Transforms
    //{
    //public:
    //    Transform clip_from_node;
    //    Transform clip_from_world;
    //};
    //
    //Transforms m_camera_transforms;
};

[[nodiscard]] auto is_icamera(const Node* const node) -> bool;
[[nodiscard]] auto is_icamera(const std::shared_ptr<Node>& node) -> bool;
[[nodiscard]] auto as_icamera(Node* const node) -> ICamera*;
[[nodiscard]] auto as_icamera(const std::shared_ptr<Node>& node) -> std::shared_ptr<ICamera>;

[[nodiscard]] auto is_camera(const Node* const node) -> bool;
[[nodiscard]] auto is_camera(const std::shared_ptr<Node>& node) -> bool;
[[nodiscard]] auto as_camera(Node* const node) -> Camera*;
[[nodiscard]] auto as_camera(const std::shared_ptr<Node>& node) -> std::shared_ptr<Camera>;

} // namespace erhe::scene
