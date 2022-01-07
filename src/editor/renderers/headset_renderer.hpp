#include "erhe/components/component.hpp"
#include "erhe/xr/xr.hpp"

namespace erhe::graphics
{
    class Framebuffer;
    class Texture;
}
namespace erhe::scene
{
    class Camera;
    class Mesh;
    class Node;
}

namespace erhe::xr
{
    class Headset;
}

namespace editor
{

class Application;
class Editor_rendering;
class Headset_renderer;
class Line_renderer;
class Mesh_memory;
class Scene_builder;
class Scene_root;

class Headset_view_resources
{
public:
    Headset_view_resources(
        erhe::xr::Render_view& render_view,
        Headset_renderer&      headset_renderer,
        Editor_rendering&      rendering,
        const size_t           slot
    );

    void update(erhe::xr::Render_view& render_view);

    std::shared_ptr<erhe::graphics::Texture>     color_texture;
    std::shared_ptr<erhe::graphics::Texture>     depth_texture;
    std::unique_ptr<erhe::graphics::Framebuffer> framebuffer;
    std::shared_ptr<erhe::scene::Camera>         camera;
    bool                                         is_valid{false};
};

class Controller_visualization
{
public:
    Controller_visualization(
        Mesh_memory&       mesh_memory,
        Scene_root&        scene_root,
        erhe::scene::Node* view_root
    );

    [[nodiscard]] auto get_node() const -> erhe::scene::Node*;

    void update(const erhe::xr::Pose& pose);

private:
    std::shared_ptr<erhe::scene::Mesh> m_controller_mesh;
};

class Headset_renderer
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Headset_renderer"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Headset_renderer ();
    ~Headset_renderer() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash                  () const -> uint32_t override { return hash; }
    [[nodiscard]] auto processing_requires_main_thread() const -> bool override { return true; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    void begin_frame();
    void render     ();
    auto root_camera() -> std::shared_ptr<erhe::scene::Camera>;

private:
    [[nodiscard]] auto get_headset_view_resources(
        erhe::xr::Render_view& render_view
    ) -> Headset_view_resources&;

    std::unique_ptr<erhe::xr::Headset>        m_headset;
    std::shared_ptr<erhe::scene::Camera>      m_root_camera;
    std::vector<Headset_view_resources>       m_view_resources;
    std::unique_ptr<Controller_visualization> m_controller_visualization;

    // Component dependencies
    std::shared_ptr<Application>      m_application;
    std::shared_ptr<Editor_rendering> m_editor_rendering;
    std::shared_ptr<Line_renderer>    m_line_renderer;
    std::shared_ptr<Scene_root>       m_scene_root;
};

}