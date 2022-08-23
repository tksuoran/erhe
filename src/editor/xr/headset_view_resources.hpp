#pragma once

#include <memory>

namespace erhe::xr
{
    class Render_view;
}

namespace erhe::graphics
{
    class Framebuffer;
    class Texture;
}
namespace erhe::scene
{
    class Camera;
}

namespace editor
{

class Editor_rendering;
class Headset_renderer;
class Scene_root;

class Headset_view_resources
{
public:
    Headset_view_resources(
        erhe::xr::Render_view& render_view,
        Headset_renderer&      headset_renderer,
        Scene_root&            scene_root,
        const std::size_t      slot
    );

    void update(erhe::xr::Render_view& render_view);

    std::shared_ptr<erhe::graphics::Texture>     color_texture;
    std::shared_ptr<erhe::graphics::Texture>     depth_texture;
    std::unique_ptr<erhe::graphics::Framebuffer> framebuffer;
    std::shared_ptr<erhe::scene::Camera>         camera;
    bool                                         is_valid{false};
};

} // namespace editor
