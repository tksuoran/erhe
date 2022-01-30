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

class ICamera
    : public Node
{
public:
    explicit ICamera(const std::string_view name);
    virtual ~ICamera() {}

    [[nodiscard]] virtual auto projection           () -> Projection* = 0;
    [[nodiscard]] virtual auto projection           () const -> const Projection* = 0;
    [[nodiscard]] virtual auto projection_transforms(const Viewport& viewport) const -> Projection_transforms = 0;
    [[nodiscard]] virtual auto get_exposure         () const -> float = 0;
    virtual void set_exposure(const float value) = 0;
};

class Camera
    : public ICamera
{
public:
    explicit Camera(const std::string_view name);
    ~Camera() override;

    [[nodiscard]] auto node_type() const -> const char* override;

    // Implements ICamera
    [[nodiscard]] auto projection           () -> Projection*                                         override;
    [[nodiscard]] auto projection           () const -> const Projection*                             override;
    [[nodiscard]] auto projection_transforms(const Viewport& viewport) const -> Projection_transforms override;
    [[nodiscard]] auto get_exposure         () const -> float                                         override;
    void set_exposure(const float value)                                                              override;

private:
    Projection m_projection;
    float      m_exposure{1.0f};
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
