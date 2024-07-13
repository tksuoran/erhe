#pragma once

#include <memory>

namespace erhe::xr {
    class Render_view;
}

namespace erhe::graphics {
    class Framebuffer;
    class Instance;
    class Texture;
}
namespace erhe::scene {
    class Camera;
    class Node;
}

namespace editor {

class Editor_rendering;
class Headset_view;
class Scene_root;
class Viewport_scene_view;

class Headset_view_resources
{
public:
    Headset_view_resources(
        erhe::graphics::Instance& graphics_instance,
        erhe::xr::Render_view&    render_view,
        Headset_view&             headset_view,
        const std::size_t         slot
    );

    void update(erhe::xr::Render_view& render_view);

    [[nodiscard]] auto is_valid                 () const -> bool;
    [[nodiscard]] auto get_framebuffer          () const -> erhe::graphics::Framebuffer*;
    [[nodiscard]] auto get_camera               () const -> erhe::scene::Camera*;
    [[nodiscard]] auto get_width                () const -> int;
    [[nodiscard]] auto get_height               () const -> int;
    [[nodiscard]] auto get_color_texture        () const -> erhe::graphics::Texture*;
    [[nodiscard]] auto get_depth_stencil_texture() const -> erhe::graphics::Texture*;

private:
    Headset_view&                                m_headset_view;
    int                                          m_width;
    int                                          m_height;
    std::shared_ptr<erhe::graphics::Texture>     m_color_texture;
    std::shared_ptr<erhe::graphics::Texture>     m_depth_stencil_texture;
    std::shared_ptr<erhe::graphics::Framebuffer> m_framebuffer;
    std::shared_ptr<erhe::scene::Node>           m_node;
    std::shared_ptr<erhe::scene::Camera>         m_camera;
    bool                                         m_is_valid{false};
};

} // namespace editor
