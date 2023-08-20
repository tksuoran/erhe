#pragma once

#include "tools/brushes/brush_tool.hpp"
#include "scene/collision_generator.hpp"
#include "scene/frame_controller.hpp"

#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

class btCollisionShape;

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::graphics {
    class Buffer_transfer_queue;
    class Instance;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::primitive {
    class Build_info;
}
namespace erhe::scene {
    class Camera;
    class Light;
    class Scene_message_bus;
}
namespace erhe::scene_renderer {
    class Shadow_renderer;
}

namespace editor
{

class Brush;
class Brush_data;
class Debug_view_window;
class Editor_context;
class Editor_message_bus;
class Editor_rendering;
class Editor_scenes;
class Editor_settings;
class Fly_camera_tool;
class Mesh_memory;
class Post_processing;
class Scene_root;
class Settings_window;
class Tools;
class Viewport_config_window;
class Viewport_window;
class Viewport_windows;

class Scene_builder
{
public:
    class Config
    {
    public:
        Config();
        float camera_exposure            {1.0f};
        float directional_light_intensity{20.0f};
        float directional_light_radius   {6.0f};
        float directional_light_height   {10.0f};
        int   directional_light_count    {4};
        float spot_light_intensity       {150.0f};
        float spot_light_radius          {20.0f};
        float spot_light_height          {10.0f};
        int   spot_light_count           {3};
        float floor_size                 {40.0f};
        int   instance_count             {1};
        float instance_gap               {0.4f};
        float object_scale               {1.0f};
        float mass_scale                 {1.0f};
        int   detail                     {2};
        bool  floor                      {true};
        bool  gltf_files                 {false};
        bool  obj_files                  {false};
        bool  sphere                     {false};
        bool  torus                      {false};
        bool  cylinder                   {false};
        bool  cone                       {false};
        bool  platonic_solids            {true};
        bool  johnson_solids             {false};
    };
    Config config;

    Scene_builder(
        erhe::graphics::Instance&       graphics_instance,
        erhe::imgui::Imgui_renderer&    imgui_renderer,
        erhe::imgui::Imgui_windows&     imgui_windows,
        erhe::rendergraph::Rendergraph& rendergraph,
        erhe::scene::Scene_message_bus& scene_message_bus,
        Editor_context&                 editor_context,
        Editor_message_bus&             editor_message_bus,
        Editor_rendering&               editor_rendering,
        Editor_scenes&                  editor_scenes,
        Editor_settings&                editor_settings,
        Mesh_memory&                    mesh_memory,
        Tools&                          tools,
        Viewport_config_window&         viewport_config_window,
        Viewport_windows&               viewport_windows
    );

    // Public API
    void add_rendertarget_viewports(int count);
    [[nodiscard]] auto get_scene_root             () const -> std::shared_ptr<Scene_root>;
    [[nodiscard]] auto get_primary_viewport_window() const -> std::shared_ptr<Viewport_window>;

    // Can discard return value
    auto make_camera(
        const std::string_view name,
        const glm::vec3        position,
        const glm::vec3        look_at = glm::vec3{0.0f, 0.0f, 0.0f}
    ) -> std::shared_ptr<erhe::scene::Camera>;

    // TODO Something nicer, do not expose here
    //[[nodiscard]] auto buffer_transfer_queue() -> erhe::graphics::Buffer_transfer_queue&;

private:

    auto make_directional_light(
        const std::string_view name,
        const glm::vec3        position,
        const glm::vec3        color,
        const float            intensity
    ) -> std::shared_ptr<erhe::scene::Light>;

    auto make_spot_light(
        const std::string_view name,
        const glm::vec3        position,
        const glm::vec3        direction,
        const glm::vec3        color,
        const float            intensity,
        const glm::vec2        spot_cone_angle
    ) -> std::shared_ptr<erhe::scene::Light>;

    auto make_brush(
        Brush_data&& brush_create_info,
        const bool   instantiate_to_scene
    ) -> std::shared_ptr<Brush>;

    auto make_brush(
        Editor_settings&           editor_settings,
        Mesh_memory&               mesh_memory,
        erhe::geometry::Geometry&& geometry,
        const bool                 instantiate_to_scene
    ) -> std::shared_ptr<Brush>;

    auto make_brush(
        Editor_settings&                                 editor_settings,
        Mesh_memory&                                     mesh_memory,
        const std::shared_ptr<erhe::geometry::Geometry>& geometry,
        const bool                                       instantiate_to_scene
    ) -> std::shared_ptr<Brush>;

    [[nodiscard]] auto build_info(Mesh_memory& mesh_memory) -> erhe::primitive::Build_info;

    void setup_cameras(
        erhe::graphics::Instance&       graphics_instance,
        erhe::imgui::Imgui_renderer&    imgui_renderer,
        erhe::imgui::Imgui_windows&     imgui_windows,
        erhe::rendergraph::Rendergraph& rendergraph,
        Editor_rendering&               editor_rendering,
        Editor_settings&                editor_settings,
        Tools&                          tools,
        Viewport_config_window&         viewport_config_window,
        Viewport_windows&               viewport_windows
    );
    void animate_lights     (const double time_d);
    void add_room           ();
    void make_brushes       (
        erhe::graphics::Instance& graphics_instance,
        Editor_settings&          editor_settings,
        Mesh_memory&              mesh_memory
    );
    void make_mesh_nodes    ();
    void make_cube_benchmark(Mesh_memory& mesh_memory);
    void setup_lights       ();

    Editor_context& m_context;

    // Self owned parts
    std::mutex                          m_brush_mutex;
    std::unique_ptr<Brush>              m_floor_brush;
    std::unique_ptr<Brush>              m_table_brush;
    std::mutex                          m_scene_brushes_mutex;
    std::vector<std::shared_ptr<Brush>> m_scene_brushes;

    std::vector<std::shared_ptr<erhe::physics::ICollision_shape>> m_collision_shapes;

    // Output
    std::shared_ptr<Viewport_window> m_primary_viewport_window;
    std::shared_ptr<Scene_root>      m_scene_root;
};

} // namespace editor
