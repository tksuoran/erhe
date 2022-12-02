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
    enable_flag_bits(Scene_item_flags::camera);
}

Camera::~Camera() noexcept
{
}

auto Camera::type_name() const -> const char*
{
    return "Camera";
}

void Camera::handle_node_scene_host_update(
    Scene_host* old_scene_host,
    Scene_host* new_scene_host
)
{
    if (old_scene_host)
    {
        Scene* scene = old_scene_host->get_scene();
        if (scene != nullptr)
        {
            scene->unregister_camera(
                std::static_pointer_cast<Camera>(shared_from_this())
            );
        }
    }
    if (new_scene_host)
    {
        Scene* scene = new_scene_host->get_scene();
        if (scene != nullptr)
        {
            scene->register_camera(
                std::static_pointer_cast<Camera>(shared_from_this())
            );
        }
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
            clip_from_node.matrix() * node->node_from_world(),
            node->world_from_node() * clip_from_node.inverse_matrix()
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

auto is_camera(const Scene_item* const scene_item) -> bool
{
    if (scene_item == nullptr)
    {
        return false;
    }
    using namespace erhe::toolkit;
    return test_all_rhs_bits_set(scene_item->get_flag_bits(), Scene_item_flags::camera);
}

auto is_camera(const std::shared_ptr<Scene_item>& scene_item) -> bool
{
    return is_camera(scene_item.get());
}

auto as_camera(Scene_item* const scene_item) -> Camera*
{
    if (scene_item == nullptr)
    {
        return nullptr;
    }
    using namespace erhe::toolkit;
    if (!test_all_rhs_bits_set(scene_item->get_flag_bits(), Scene_item_flags::camera))
    {
        return nullptr;
    }
    return reinterpret_cast<Camera*>(scene_item);
}

auto as_camera(const std::shared_ptr<Scene_item>& scene_item) -> std::shared_ptr<Camera>
{
    if (!scene_item)
    {
        return {};
    }
    using namespace erhe::toolkit;
    if (!test_all_rhs_bits_set(scene_item->get_flag_bits(), Scene_item_flags::camera))
    {
        return {};
    }
    return std::dynamic_pointer_cast<Camera>(scene_item);
}

auto get_camera(
    const erhe::scene::Node* const node
) -> std::shared_ptr<Camera>
{
    for (const auto& attachment : node->attachments())
    {
        auto camera = as_camera(attachment);
        if (camera)
        {
            return camera;
        }
    }
    return {};
}

} // namespace erhe::scene
