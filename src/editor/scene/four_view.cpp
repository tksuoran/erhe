#include "scene/four_view.hpp"

#include "scene/scene_root.hpp"

#include "erhe_math/math_util.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/scene.hpp"

#include <mutex>

namespace editor {

namespace {

constexpr const char* c_camera_names[Four_view::axis_count] = { "Top", "Front", "Right" };

[[nodiscard]] auto get_axis_direction(const Four_view_axis axis) -> glm::vec3
{
    switch (axis) {
        case Four_view_axis::top:   return glm::vec3{0.0f, 1.0f, 0.0f};
        case Four_view_axis::front: return glm::vec3{0.0f, 0.0f, 1.0f};
        case Four_view_axis::right: return glm::vec3{1.0f, 0.0f, 0.0f};
        default:                    return glm::vec3{0.0f, 0.0f, 1.0f};
    }
}

[[nodiscard]] auto get_up_direction(const Four_view_axis axis) -> glm::vec3
{
    return (axis == Four_view_axis::top)
        ? glm::vec3{0.0f, 0.0f, -1.0f}
        : glm::vec3{0.0f, 1.0f,  0.0f};
}

} // anonymous namespace

namespace {

// The point focus_distance ahead of the camera, along its view direction (-Z).
[[nodiscard]] auto get_point_ahead(const erhe::scene::Camera& camera, const float focus_distance) -> glm::vec3
{
    const glm::mat4 world_from_node = camera.world_from_node();
    const glm::vec3 position        = glm::vec3{world_from_node[3]};
    const glm::vec3 forward         = -glm::normalize(glm::vec3{world_from_node[2]});
    return position + (focus_distance * forward);
}

} // anonymous namespace

Four_view::Four_view(
    const std::shared_ptr<Scene_root>&          scene_root,
    const std::shared_ptr<erhe::scene::Camera>& perspective_camera,
    const float                                 focus_distance,
    const float                                 view_height,
    const float                                 distance
)
    : m_scene_root        {scene_root}
    , m_perspective_camera{perspective_camera}
    , m_focus_distance    {focus_distance}
    , m_view_height       {view_height}
    , m_distance          {distance}
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};
    if (perspective_camera) {
        m_focus = get_point_ahead(*perspective_camera, m_focus_distance);
        link_camera(Four_view_axis::perspective);
    }
    for (std::size_t i = 0; i < axis_count; ++i) {
        const Four_view_axis axis = static_cast<Four_view_axis>(i);
        std::shared_ptr<erhe::scene::Camera> camera = std::make_shared<erhe::scene::Camera>(c_camera_names[i]);
        camera->set_projection_type(erhe::scene::Projection::Type::orthogonal_vertical);
        camera->set_ortho_height   (m_view_height);
        camera->set_z_near         (0.0f);
        camera->set_z_far          (2.0f * m_distance);
        // View cameras of the editor session, like the default camera
        // injected on open: shown in the hierarchy, never saved.
        camera->enable_flag_bits(
            erhe::Item_flags::content             |
            erhe::Item_flags::show_in_ui          |
            erhe::Item_flags::exclude_from_prefab |
            erhe::Item_flags::session_only
        );
        camera->set_parent(scene_root->get_scene().get_root_node());
        m_cameras[i] = camera;
        m_placing = true;
        place_camera(axis);
        m_placing = false;
        link_camera(axis);
    }
}

// The tokens are members, so their destruction takes every observer off its
// camera - including the user's own perspective camera, which outlives the
// four view.
Four_view::~Four_view() noexcept = default;

void Four_view::link_camera(const Four_view_axis axis)
{
    const std::shared_ptr<erhe::scene::Camera> camera = get_camera(axis);
    if (!camera) {
        return;
    }
    erhe::scene::Transform_observer_token token = camera->add_transform_observer(
        [this, axis](erhe::scene::Node&) {
            on_camera_moved(axis);
        }
    );
    if (axis == Four_view_axis::perspective) {
        m_perspective_link = std::move(token);
    } else {
        m_links[static_cast<std::size_t>(axis)] = std::move(token);
    }
}

auto Four_view::get_perspective_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
    return m_perspective_camera.lock();
}

auto Four_view::get_scene_root() const -> std::shared_ptr<Scene_root>
{
    return m_scene_root.lock();
}

auto Four_view::get_camera(const Four_view_axis axis) const -> std::shared_ptr<erhe::scene::Camera>
{
    if (axis == Four_view_axis::perspective) {
        return m_perspective_camera.lock();
    }
    return m_cameras[static_cast<std::size_t>(axis)].lock();
}

auto Four_view::contains(const erhe::scene::Camera* const camera) const -> bool
{
    if (camera == nullptr) {
        return false;
    }
    for (const std::weak_ptr<erhe::scene::Camera>& entry : m_cameras) {
        if (entry.lock().get() == camera) {
            return true;
        }
    }
    return false;
}

auto Four_view::get_focus() const -> glm::vec3
{
    return m_focus;
}

auto Four_view::get_view_height() const -> float
{
    return m_view_height;
}

void Four_view::place_camera(const Four_view_axis axis)
{
    const std::shared_ptr<erhe::scene::Camera> camera = get_camera(axis);
    // A camera the user removed from the scene stays alive in the undo
    // history; it has left the four view until an undo brings it back.
    if (!camera || (camera->get_scene() == nullptr)) {
        return;
    }
    const glm::vec3 eye            = m_focus + (m_distance * get_axis_direction(axis));
    const glm::mat4 world_from_node = erhe::math::create_look_at(eye, m_focus, get_up_direction(axis));
    camera->set_world_from_node(world_from_node);
}

void Four_view::place_orthogonal_cameras(const Four_view_axis except)
{
    for (std::size_t i = 0; i < axis_count; ++i) {
        const Four_view_axis axis = static_cast<Four_view_axis>(i);
        if (axis != except) {
            place_camera(axis);
        }
    }
}

void Four_view::on_perspective_camera_moved()
{
    const std::shared_ptr<erhe::scene::Camera> camera = m_perspective_camera.lock();
    if (!camera || (camera->get_scene() == nullptr)) {
        return;
    }
    const glm::vec3 focus     = get_point_ahead(*camera, m_focus_distance);
    const glm::vec3 offset    = focus - m_focus;
    const float     tolerance = 1.0e-6f * glm::max(1.0f, m_view_height);
    if (glm::dot(offset, offset) <= (tolerance * tolerance)) {
        return;
    }
    m_focus = focus;
    m_placing = true;
    place_orthogonal_cameras(Four_view_axis::perspective);
    m_placing = false;
}

void Four_view::on_camera_moved(const Four_view_axis axis)
{
    if (m_placing) {
        return;
    }
    if (axis == Four_view_axis::perspective) {
        on_perspective_camera_moved();
        return;
    }
    const std::shared_ptr<erhe::scene::Camera> camera = get_camera(axis);
    if (!camera || (camera->get_scene() == nullptr)) {
        return;
    }
    // Only movement in the view plane moves the focus: movement along the
    // view axis changes nothing an orthogonal view shows.
    const glm::vec3 axis_direction = get_axis_direction(axis);
    const glm::vec3 expected       = m_focus + (m_distance * axis_direction);
    glm::vec3       offset         = glm::vec3{camera->position_in_world()} - expected;
    offset -= axis_direction * glm::dot(axis_direction, offset);
    const float tolerance = 1.0e-6f * glm::max(1.0f, m_view_height);
    if (glm::dot(offset, offset) <= (tolerance * tolerance)) {
        return;
    }
    m_focus += offset;
    m_placing = true;
    place_orthogonal_cameras(axis);
    // The perspective camera keeps looking at the focus from where it did:
    // it moves by the same offset, orientation unchanged.
    const std::shared_ptr<erhe::scene::Camera> perspective_camera = m_perspective_camera.lock();
    if (perspective_camera && (perspective_camera->get_scene() != nullptr)) {
        glm::mat4 world_from_node = perspective_camera->world_from_node();
        world_from_node[3] += glm::vec4{offset, 0.0f};
        perspective_camera->set_world_from_node(world_from_node);
    }
    m_placing = false;
}

void Four_view::set_view_height(const float view_height)
{
    m_view_height = glm::max(view_height, 1.0e-4f);
    for (const std::weak_ptr<erhe::scene::Camera>& entry : m_cameras) {
        const std::shared_ptr<erhe::scene::Camera> camera = entry.lock();
        if (camera) {
            camera->set_ortho_height(m_view_height);
        }
    }
}

void Four_view::set_focus(const glm::vec3 focus)
{
    const glm::vec3 offset = focus - m_focus;
    m_focus = focus;
    m_placing = true;
    place_orthogonal_cameras(Four_view_axis::perspective);
    const std::shared_ptr<erhe::scene::Camera> perspective_camera = m_perspective_camera.lock();
    if (perspective_camera && (perspective_camera->get_scene() != nullptr)) {
        glm::mat4 world_from_node = perspective_camera->world_from_node();
        world_from_node[3] += glm::vec4{offset, 0.0f};
        perspective_camera->set_world_from_node(world_from_node);
    }
    m_placing = false;
}

} // namespace editor
