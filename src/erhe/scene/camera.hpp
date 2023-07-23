#pragma once

#include "erhe/toolkit/viewport.hpp"
#include "erhe/scene/node_attachment.hpp"
#include "erhe/scene/projection.hpp"
#include "erhe/scene/transform.hpp"

#include <cstdint>
#include <string>

namespace erhe::scene
{

class Camera;

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
    Camera();
    explicit Camera(const std::string_view name);
    ~Camera() noexcept override;

    // Implements Item
    static constexpr std::string_view static_type_name{"Camera"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    // Implements Node_attachment
    void handle_item_host_update(erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;

    // Public API
    [[nodiscard]] auto projection           () -> Projection*;
    [[nodiscard]] auto projection           () const -> const Projection*;
    [[nodiscard]] auto projection_transforms(const erhe::toolkit::Viewport& viewport) const -> Camera_projection_transforms;
    [[nodiscard]] auto get_exposure         () const -> float;
    [[nodiscard]] auto get_shadow_range     () const -> float;

    void set_exposure    (float value);
    void set_shadow_range(float value);

private:
    Projection m_projection;
    float      m_exposure    {1.0f};
    float      m_shadow_range{22.0f};
};

[[nodiscard]] auto is_camera(const Item* scene_item) -> bool;
[[nodiscard]] auto is_camera(const std::shared_ptr<Item>& scene_item) -> bool;

auto get_camera(const erhe::scene::Node* node) -> std::shared_ptr<Camera>;

} // namespace erhe::scene
