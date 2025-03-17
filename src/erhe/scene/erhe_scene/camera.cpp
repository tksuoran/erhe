#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene {

Camera::Camera()                         = default;
Camera::Camera(const Camera&)            = default;
Camera::~Camera() noexcept               = default;

Camera::Camera(const std::string_view name)
    : Item{name}
{
}

Camera::Camera(const Camera& src, erhe::for_clone)
    : Item{src, erhe::for_clone{}}
    , m_projection  {src.m_projection  }
    , m_exposure    {src.m_exposure    }
    , m_shadow_range{src.m_shadow_range}
{
}

auto Camera::clone_attachment() const -> std::shared_ptr<Node_attachment>
{
    return std::make_shared<Camera>(*this, for_clone{});
}

auto Camera::get_static_type() -> uint64_t
{
    return erhe::Item_type::node_attachment | erhe::Item_type::camera;
}

auto Camera::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Camera::get_type_name() const -> std::string_view
{
    return static_type_name;
}

void Camera::handle_item_host_update(Item_host* const old_item_host, Item_host* const new_item_host)
{
    const auto shared_this = std::static_pointer_cast<Camera>(shared_from_this()); // keep alive

    Scene_host* old_scene_host = static_cast<Scene_host*>(old_item_host);
    Scene_host* new_scene_host = static_cast<Scene_host*>(new_item_host);

    if (old_scene_host != nullptr) {
        old_scene_host->unregister_camera(shared_this);
    }
    if (new_scene_host != nullptr) {
        new_scene_host->register_camera(shared_this);
    }
}

auto Camera::projection_transforms(const erhe::math::Viewport& viewport) const -> Camera_projection_transforms
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

auto is_camera(const Item_base* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    return erhe::bit::test_all_rhs_bits_set(item->get_type(), Item_type::camera);
}

auto is_camera(const std::shared_ptr<Item_base>& item) -> bool
{
    return is_camera(item.get());
}

auto get_camera(const erhe::scene::Node* const node) -> std::shared_ptr<Camera>
{
    if (node == nullptr) {
        return {};
    }
    for (const auto& attachment : node->get_attachments()) {
        auto camera = std::dynamic_pointer_cast<Camera>(attachment);
        if (camera) {
            return camera;
        }
    }
    return {};
}

} // namespace erhe::scene
