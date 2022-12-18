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
    : public Node_attachment
{
public:
    explicit Camera(const std::string_view name);
    ~Camera() noexcept override;

    // Implements Node_attachment
    [[nodiscard]] static auto static_type     () -> uint64_t;
    [[nodiscard]] static auto static_type_name() -> const char*;
    void handle_node_scene_host_update(Scene_host* old_scene_host, Scene_host* new_scene_host) override;

    // Implements Item
    [[nodiscard]] auto get_type () const -> uint64_t override;
    [[nodiscard]] auto type_name() const -> const char* override;

    // Public API
    [[nodiscard]] auto projection           () -> Projection*;
    [[nodiscard]] auto projection           () const -> const Projection*;
    [[nodiscard]] auto projection_transforms(const Viewport& viewport) const -> Camera_projection_transforms;
    [[nodiscard]] auto get_exposure         () const -> float;
    [[nodiscard]] auto get_shadow_range     () const -> float;

    void set_exposure    (float value);
    void set_shadow_range(float value);

private:
    Projection m_projection;
    float      m_exposure    {1.0f};
    float      m_shadow_range{22.0f};
};

[[nodiscard]] auto is_camera(const Item* const scene_item) -> bool;
[[nodiscard]] auto is_camera(const std::shared_ptr<Item>& scene_item) -> bool;
[[nodiscard]] auto as_camera(Item* const scene_item) -> Camera*;
[[nodiscard]] auto as_camera(const std::shared_ptr<Item>& node) -> std::shared_ptr<Camera>;

auto get_camera(const erhe::scene::Node* node) -> std::shared_ptr<Camera>;

} // namespace erhe::scene
