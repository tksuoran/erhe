#pragma once

#include "erhe_math/viewport.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/transform.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace erhe::scene {

class Camera;

class Camera_projection_transforms
{
public:
    Transform clip_from_camera;
    Transform clip_from_world;
};

class Camera : public erhe::Item<Item_base, Node_attachment, Camera, erhe::Item_kind::clone_using_custom_clone_constructor>
{
public:
    Camera();
    explicit Camera(const Camera&);
    Camera& operator=(const Camera&) = delete;
    ~Camera() noexcept override;

    explicit Camera(std::string_view name);
    Camera(const Camera&, erhe::for_clone);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Camera"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::node_attachment | erhe::Item_type::camera; }

    // Implements Node_attachment
    auto clone_attachment       () const -> std::shared_ptr<Node_attachment> override;
    void handle_item_host_update(erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;

    // Public API
    [[nodiscard]] auto projection           () -> Projection*;
    [[nodiscard]] auto projection           () const -> const Projection*;
    [[nodiscard]] auto projection_transforms(const erhe::math::Viewport& viewport, bool reverse_depth = true) const -> Camera_projection_transforms;
    [[nodiscard]] auto get_exposure         () const -> float;
    [[nodiscard]] auto get_shadow_range     () const -> float;
    [[nodiscard]] auto get_projection_scale () const -> float;

    void set_exposure    (float value);
    void set_shadow_range(float value);

private:
    Projection m_projection;
    float      m_exposure    {1.0f};
    float      m_shadow_range{22.0f};
};

} // namespace erhe::scene
