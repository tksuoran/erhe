#pragma once

#include "erhe_scene/transform_observer.hpp"

#include <glm/glm.hpp>

#include <array>
#include <memory>

namespace erhe::scene { class Camera; }

namespace editor {

class Scene_root;

// The views of a four view: three axis-aligned orthogonal views and the
// perspective view the four view was opened from.
enum class Four_view_axis : unsigned int {
    top         = 0, // from +Y looking down, -Z up on screen
    front       = 1, // from +Z looking towards -Z
    right       = 2, // from +X looking towards -X
    perspective = 3  // the source viewport's camera
};

// Four linked views of one scene that share one focus point: a perspective
// view and three axis-aligned orthogonal views, which also share one zoom.
// The focus is the point a fixed distance ahead of the perspective camera;
// each orthogonal camera sits at focus + axis * distance, looking at the
// focus. Moving or turning the perspective camera moves the focus and with it
// the orthogonal cameras; moving an orthogonal camera in its view plane moves
// the focus, the other two orthogonal cameras, and the perspective camera by
// the same offset. See doc/editor/four_view.md.
class Four_view
{
public:
    static constexpr std::size_t axis_count = 3;

    // The focus starts focus_distance ahead of the perspective camera.
    Four_view(
        const std::shared_ptr<Scene_root>&          scene_root,
        const std::shared_ptr<erhe::scene::Camera>& perspective_camera,
        float                                       focus_distance,
        float                                       view_height,
        float                                       distance
    );
    ~Four_view() noexcept;
    Four_view(const Four_view&)            = delete;
    Four_view& operator=(const Four_view&) = delete;

    [[nodiscard]] auto get_scene_root () const -> std::shared_ptr<Scene_root>;
    [[nodiscard]] auto get_camera     (Four_view_axis axis) const -> std::shared_ptr<erhe::scene::Camera>;
    [[nodiscard]] auto contains       (const erhe::scene::Camera* camera) const -> bool;
    [[nodiscard]] auto get_perspective_camera() const -> std::shared_ptr<erhe::scene::Camera>;
    [[nodiscard]] auto get_focus      () const -> glm::vec3;
    [[nodiscard]] auto get_view_height() const -> float;

    // Change sites
    void on_camera_moved(Four_view_axis axis);              // from a camera's transform observer
    void set_view_height(float view_height);                // zoom, applies to all three cameras
    void set_focus      (glm::vec3 focus);

private:
    void place_camera            (Four_view_axis axis);
    void place_orthogonal_cameras(Four_view_axis except);
    void on_perspective_camera_moved();
    // The link of one camera (doc/erhe/scene.md "Transform observers"):
    // the four view holds the
    // camera weakly and follows its transform through a token it owns, so
    // ~Four_view takes the observer off the user's own perspective camera.
    void link_camera(Four_view_axis axis);

    std::weak_ptr<Scene_root>                                            m_scene_root;
    std::array<std::weak_ptr<erhe::scene::Camera>,            axis_count> m_cameras;
    std::array<erhe::scene::Transform_observer_token,         axis_count> m_links;
    std::weak_ptr<erhe::scene::Camera>                                   m_perspective_camera;
    erhe::scene::Transform_observer_token                                m_perspective_link;
    float                                                                m_focus_distance{10.0f};
    glm::vec3                                                            m_focus{0.0f};
    float                                                                m_view_height{10.0f};
    float                                                                m_distance{100.0f};
    bool                                                                 m_placing{false};
};

} // namespace editor
