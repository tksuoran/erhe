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
class Line_renderer;
class Mesh_memory;
class Scene_manager;
class Scene_root;

class Headset_view_resources
{
public:
    Headset_view_resources(erhe::xr::Render_view& render_view,
                           Editor_rendering&      rendering);

    void update(erhe::xr::Render_view& render_view,
                Editor_rendering&      rendering);

    std::shared_ptr<erhe::graphics::Texture>      color_texture;
    std::shared_ptr<erhe::graphics::Texture>      depth_texture;
    //std::unique_ptr<erhe::graphics::Renderbuffer> depth_stencil_renderbuffer;
    std::unique_ptr<erhe::graphics::Framebuffer>  framebuffer;
    std::shared_ptr<erhe::scene::Node>            camera_node;
    std::shared_ptr<erhe::scene::Camera>          camera;
    bool                                          is_valid{false};
};

class Controller_visualization
{
public:
    Controller_visualization(Mesh_memory&       mesh_memory,
                             Scene_root&        scene_root,
                             erhe::scene::Node* view_root);

    void update  (const erhe::xr::Pose& pose);
    auto get_node() const -> erhe::scene::Node*;

private:
    std::shared_ptr<erhe::scene::Mesh> m_controller_mesh;
};

class Headset_renderer
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Headset_renderer"};

    Headset_renderer ();
    ~Headset_renderer() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;
    auto initialization_requires_main_thread() const -> bool override { return true; }

    void begin_frame();
    void render     ();

private:
    auto get_headset_view_resources(erhe::xr::Render_view& render_view) -> Headset_view_resources&;

    std::unique_ptr<erhe::xr::Headset>        m_headset;
    std::vector<Headset_view_resources>       m_view_resources;
    std::unique_ptr<Controller_visualization> m_controller_visualization;

    std::shared_ptr<Application>              m_application;
    std::shared_ptr<Editor_rendering>         m_editor_rendering;
    std::shared_ptr<Line_renderer>            m_line_renderer;
    std::shared_ptr<Scene_manager>            m_scene_manager;
    std::shared_ptr<Scene_root>               m_scene_root;
};

}