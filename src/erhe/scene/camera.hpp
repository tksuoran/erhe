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

class Camera_projection_transforms
{
public:
    Transform clip_from_camera;
    Transform clip_from_world;
};

class Camera
    : public Node
{
public:
    explicit Camera(const std::string_view name);
    ~Camera() noexcept override;

    [[nodiscard]] auto node_type() const -> const char* override;

    [[nodiscard]] auto projection           () -> Projection*;
    [[nodiscard]] auto projection           () const -> const Projection*;
    [[nodiscard]] auto projection_transforms(const Viewport& viewport) const -> Camera_projection_transforms;
    [[nodiscard]] auto get_exposure         () const -> float;
    void set_exposure(const float value);

private:
    Projection m_projection;
    float      m_exposure{1.0f};
};

[[nodiscard]] auto is_camera(const Node* const node) -> bool;
[[nodiscard]] auto is_camera(const std::shared_ptr<Node>& node) -> bool;
[[nodiscard]] auto as_camera(Node* const node) -> Camera*;
[[nodiscard]] auto as_camera(const std::shared_ptr<Node>& node) -> std::shared_ptr<Camera>;

} // namespace erhe::scene
