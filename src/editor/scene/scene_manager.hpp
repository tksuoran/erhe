#pragma once

#include "scene/collision_generator.hpp"
#include "scene/frame_controller.hpp"
#include "windows/brushes.hpp"

#include "erhe/components/component.hpp"

#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

class btCollisionShape;

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::graphics
{
    class Buffer;
    class Buffer_transfer_queue;
    class Vertex_format;
}

namespace erhe::primitive
{
    struct Primitive_build_context;
    struct Primitive_geometry;
    struct Primitive;
}

namespace erhe::scene
{
    class Camera;
    class ICamera;
    class Layer;
    class Light;
    class Mesh;
    class Scene;
}

namespace erhe::physics
{
    class World;
}

namespace editor
{

class Brush;
class Debug_draw;
class Mesh_memory;
class Node_physics;
class Scene_root;

class Scene_manager
    : public erhe::components::Component
    , public erhe::components::IUpdate_fixed_step
    , public erhe::components::IUpdate_once_per_frame
{
public:
    static constexpr const char* c_name = "Scene_manager";
    Scene_manager();
    ~Scene_manager() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    void set_view_camera(std::shared_ptr<erhe::scene::ICamera> camera);
    auto get_view_camera() const -> std::shared_ptr<erhe::scene::ICamera>;
    void add_scene      ();
    void sort_lights    ();
    auto make_directional_light(std::string_view name,
                                glm::vec3        position,
                                glm::vec3        color,
                                float            intensity)
    -> std::shared_ptr<erhe::scene::Light>;

    auto make_spot_light(std::string_view name,
                         glm::vec3        position,
                         glm::vec3        direction,
                         glm::vec3        color,
                         float            intensity,
                         glm::vec2        spot_cone_angle)
    -> std::shared_ptr<erhe::scene::Light>;

    template <typename ...Args>
    auto make_brush(bool instantiate_to_scene, Args&& ...args)
    -> std::shared_ptr<Brush>
    {
        auto brush = m_brushes->make_brush(std::forward<Args>(args)...);
        if (instantiate_to_scene)
        {
            std::lock_guard<std::mutex> lock(m_scene_brushes_mutex);
            m_scene_brushes.push_back(brush);
        }
        return brush;
    }

    // Implements IUpdate_once_per_frame
    void update_once_per_frame(const erhe::components::Time_context& time_context);

    // Implements IUpdate_fixed_step
    void update_fixed_step(const erhe::components::Time_context& time_context);

private:
    void initialize_camera        ();
    void animate_lights           (double time_d);
    auto primitive_build_context  () -> erhe::primitive::Primitive_build_context&;
    auto buffer_transfer_queue    () -> erhe::graphics::Buffer_transfer_queue&;
    void add_floor                ();
    void make_brushes             ();
    void make_mesh_nodes          ();
    void make_punctual_light_nodes();

    // Components
    std::shared_ptr<Brushes>     m_brushes;
    std::shared_ptr<Mesh_memory> m_mesh_memory;
    std::shared_ptr<Scene_root>  m_scene_root;

    // Self owned parts
    std::shared_ptr<erhe::scene::ICamera> m_view_camera;
    //std::shared_ptr<erhe::scene::ICamera> m_view_camera_position_only;
    //std::shared_ptr<erhe::scene::ICamera> m_view_camera_position_heading;
    //std::shared_ptr<erhe::scene::ICamera> m_view_camera_position_heading_elevation;
    std::mutex                            m_brush_mutex;
    std::unique_ptr<Brush>                m_floor_brush;
    std::mutex                            m_scene_brushes_mutex;
    std::vector<std::shared_ptr<Brush>>   m_scene_brushes;
    std::shared_ptr<erhe::scene::Camera>  m_camera;
    std::shared_ptr<Frame_controller>     m_camera_controls;

};

} // namespace editor
