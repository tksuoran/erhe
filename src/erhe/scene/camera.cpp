#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/scene_host.hpp"
#include "erhe/scene/scene_log.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/verify.hpp"

#include <gsl/gsl>

namespace erhe::scene
{

Camera::Camera(const std::string_view name)
    : Node_attachment{name}
{
}

Camera::~Camera() noexcept
{
}

auto Camera::static_type() -> uint64_t
{
    return Item_type::node_attachment | Item_type::camera;
}

auto Camera::static_type_name() -> const char*
{
    return "Camera";
}

auto Camera::get_type() const -> uint64_t
{
    return static_type();
}

auto Camera::type_name() const -> const char*
{
    return static_type_name();
}

void Camera::handle_node_scene_host_update(
    Scene_host* old_scene_host,
    Scene_host* new_scene_host
)
{
    if (old_scene_host) {
        old_scene_host->unregister_camera(
            std::static_pointer_cast<Camera>(shared_from_this())
        );
    }
    if (new_scene_host) {
        new_scene_host->register_camera(
            std::static_pointer_cast<Camera>(shared_from_this())
        );
    }
}

auto Camera::projection_transforms(const Viewport& viewport) const -> Camera_projection_transforms
{
    const auto clip_from_node = m_projection.clip_from_node_transform(viewport);
    const Node* node = get_node();
    ERHE_VERIFY(node != nullptr);
    return Camera_projection_transforms{
        .clip_from_camera = clip_from_node,
        .clip_from_world = Transform{
            clip_from_node.get_matrix() * node->node_from_world(),
            node->world_from_node()     * clip_from_node.get_inverse_matrix()
        }
    };
}

auto Camera::get_exposure() const -> float
{
    return m_exposure;
}

void Camera::set_exposure(const float value)
{
    m_exposure = value;
}

auto Camera::get_shadow_range() const -> float
{
    return m_shadow_range;
}

void Camera::set_shadow_range(const float value)
{
    m_shadow_range = value;
}

auto Camera::projection() -> Projection*
{
    return &m_projection;
}

auto Camera::projection() const -> const Projection*
{
    return &m_projection;
}

using namespace erhe::toolkit;

auto is_camera(const Item* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    return test_all_rhs_bits_set(item->get_type(), Item_type::camera);
}

auto is_camera(const std::shared_ptr<Item>& item) -> bool
{
    return is_camera(item.get());
}

auto as_camera(Item* const item) -> Camera*
{
    if (item == nullptr) {
        return nullptr;
    }
    if (!test_all_rhs_bits_set(item->get_type(), Item_type::camera)) {
        return nullptr;
    }
    return static_cast<Camera*>(item);
}

auto as_camera(const std::shared_ptr<Item>& item) -> std::shared_ptr<Camera>
{
    if (!item) {
        return {};
    }
    if (!test_all_rhs_bits_set(item->get_type(), Item_type::camera)) {
        return {};
    }
    return std::static_pointer_cast<Camera>(item);
}

auto get_camera(const erhe::scene::Node* const node) -> std::shared_ptr<Camera>
{
    if (node == nullptr) {
        return {};
    }
    for (const auto& attachment : node->attachments()) {
        auto camera = as_camera(attachment);
        if (camera) {
            return camera;
        }
    }
    return {};
}

} // namespace erhe::scene
