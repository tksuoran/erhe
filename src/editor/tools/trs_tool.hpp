#pragma once

#include "erhe/scene/node.hpp"
#include "tools/tool.hpp"
#include "tools/selection_tool.hpp"
#include "windows/imgui_window.hpp"

#include <glm/glm.hpp>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string_view>

namespace erhe::physics
{
    class Rigid_body;
}

namespace erhe::primitive
{
    class Geometry;
    class Material;
}

namespace erhe::scene
{
    class ICamera;
    class Mesh;
    class Node;
    class Viewport;
}

namespace editor
{

class Operation_stack;
class Line_renderer;
class Node_physics;
class Mesh_memory;
class Scene_root;
class Text_renderer;

class Trs_tool
    : public erhe::components::Component
    , public Tool
    , public Imgui_window
{
public:
    enum class Reference_mode : unsigned int
    {
        local = 0,
        parent,
        world,
    };

    static constexpr std::array<std::string_view, 3> c_reference_mode_strings =
    {
        "Local",
        "Parent",
        "World"
    };

    static constexpr std::string_view c_name{"Trs_tool"};

    Trs_tool ();
    ~Trs_tool() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    // Implements Tool
    auto update       (Pointer_context& pointer_context) -> bool override;
    void render_update(const Render_context& render_context)     override;
    void render       (const Render_context& render_context)     override;
    auto state        () const -> State                          override;
    void cancel_ready ()                                         override;
    auto description  () -> const char*                          override;

    // Implements Imgui_window
    void imgui(Pointer_context& pointer_context) override;

    void set_translate(const bool enabled);
    void set_rotate   (const bool enabled);

private:
    void snap_translate       (glm::vec3& translation) const;
    auto begin                (Pointer_context& pointer_context) -> bool;
    auto end                  (Pointer_context& pointer_context) -> bool;
    void set_node             (const std::shared_ptr<erhe::scene::Node>& node);
    auto is_x_translate_active() const -> bool;
    auto is_y_translate_active() const -> bool;
    auto is_z_translate_active() const -> bool;
    auto is_rotate_active     () const -> bool;

    enum class Handle : unsigned int
    {
        e_handle_none         = 0,
        e_handle_translate_x  = 1,
        e_handle_translate_y  = 2,
        e_handle_translate_z  = 3,
        e_handle_translate_xy = 4,
        e_handle_translate_xz = 5,
        e_handle_translate_yz = 6,
        e_handle_rotate_x     = 7,
        e_handle_rotate_y     = 8,
        e_handle_rotate_z     = 9,
    };

    enum class Handle_type : unsigned int
    {
        e_handle_type_none            = 0,
        e_handle_type_translate_axis  = 1,
        e_handle_type_translate_plane = 2,
        e_handle_type_rotate          = 3
    };

    void update_axis_translate(Pointer_context& pointer_context);

    void update_plane_translate(Pointer_context& pointer_context);

    void update_rotate(Pointer_context& pointer_context);

    void update_rotate_parallel(Pointer_context& pointer_context);

    auto get_handle(erhe::scene::Mesh* mesh) const -> Trs_tool::Handle;

    auto get_handle_type(const Handle handle) const -> Handle_type;

    auto offset_plane_origo(const Handle handle, const glm::vec3 p) const -> glm::vec3;

    auto project_to_offset_plane(const Handle handle, const glm::vec3 p, const glm::vec3 q) const -> glm::vec3;

    auto get_axis_direction() const -> glm::vec3;

    auto get_plane_normal() const -> glm::vec3;

    auto get_plane_normal_in_model() const -> glm::vec3;

    auto get_plane_side_in_model() const -> glm::vec3;

    auto get_plane_side_in_model2() const -> glm::vec3;

    auto get_axis_color(Handle handle) const -> uint32_t;

    // Casts ray from current pointer context position
    // and intersects it to plane of current handle;
    auto project_pointer_to_plane(const Pointer_context& pointer_context, const glm::vec3 p, glm::vec3& q) -> bool;

    void set_node_world_transform(const glm::mat4 world_from_node);

    void update_transforms();

    void update_visibility();

    auto root() -> erhe::scene::Node*;

    bool m_local{true};
    std::shared_ptr<Operation_stack>           m_operation_stack;
    std::shared_ptr<Mesh_memory>               m_mesh_memory;
    std::shared_ptr<Scene_root>                m_scene_root;
    std::shared_ptr<Selection_tool>            m_selection_tool;
    State                                      m_state        {State::Passive};
    Handle                                     m_active_handle{Handle::e_handle_none};
    std::optional<Selection_tool::Subcription> m_selection_subscription;
    std::map<erhe::scene::Mesh*, Handle>       m_handles;
    std::shared_ptr<erhe::scene::Node>         m_target_node;
    std::shared_ptr<Node_physics>              m_node_physics;
    std::shared_ptr<erhe::scene::Node>         m_tool_node;
    bool                                       m_translate_snap_enable{false};
    bool                                       m_rotate_snap_enable   {false};
    float                                      m_translate_snap       {0.1f};
    //float                                    m_rotate_snap          {15.0f};
    erhe::scene::Transform                     m_parent_from_node_before;

    // These are for debug rendering
    class Debug_rendering
    {
    public:
        glm::vec3 m_P0{};
        glm::vec3 m_P1{};
        glm::vec3 m_Q0{};
        glm::vec3 m_Q1{};
        glm::vec3 m_R0{};
        glm::vec3 m_R1{};
        glm::vec2 m_ss_closest{};
        uint32_t  m_debug_color{0x00000000u};
        float     m_v_dot_n{0.0f};
        glm::vec3 m_pw{};
        glm::vec3 m_q0{};
        glm::vec3 m_q{} ;
    };
    Debug_rendering m_debug_rendering;

    class Drag
    {
    public:
        glm::mat4 initial_world_from_local {1.0f};
        glm::mat4 initial_local_from_world {1.0f};
        glm::vec3 initial_position_in_world{0.0f, 0.0f, 0.0f};
        glm::vec3 position_in_root         {0.0f, 0.0f, 0.0f};
        glm::vec3 world_direction          {1.0f, 0.0f, 0.0f};
        float     initial_window_depth     {0.0f};
        erhe::scene::Transform initial_parent_from_node_transform;
    };
    Drag m_drag;

    class Rotation_context
    {
    public:
        auto angle_of_rotation_for_point(glm::vec3 q) -> float;

        glm::vec3 normal; // also rotation axis
        glm::vec3 side;
        glm::vec3 up;
        glm::vec3 drag_start;
        glm::vec3 center_of_rotation;
        glm::vec3 rotation_side_axis;
        float     start_rotation_angle;
        glm::mat4 local_to_rotation_offset;
        glm::mat4 rotation_offset_to_local;
        //glm::vec3 local_drag_start;
    };
    Rotation_context m_rotation;

    class Visualization
    {
    public:
        Visualization();

        void initialize       (Mesh_memory& mesh_memory, Scene_root& scene_root);
        void update_visibility(const bool show, const Handle active_handle);
        void update_scale     (const glm::vec3 view_position_in_world);
        void update_transforms(uint64_t serial);

        bool  show_translate{false};
        bool  show_rotate   {false};
        bool  hide_inactive {true};
        float scale         {3.5f}; // 6.6 for debug

        erhe::scene::Node*                         root{nullptr};
        std::shared_ptr<erhe::scene::Node>         tool_node;
        bool                                       local{true};
        float                                      view_distance{1.0f};
        std::shared_ptr<erhe::primitive::Material> x_material;
        std::shared_ptr<erhe::primitive::Material> y_material;
        std::shared_ptr<erhe::primitive::Material> z_material;
        std::shared_ptr<erhe::primitive::Material> highlight_material;
        std::shared_ptr<erhe::scene::Mesh>         x_arrow_cylinder_mesh;
        std::shared_ptr<erhe::scene::Mesh>         x_arrow_cone_mesh;
        std::shared_ptr<erhe::scene::Mesh>         y_arrow_cylinder_mesh;
        std::shared_ptr<erhe::scene::Mesh>         y_arrow_cone_mesh;
        std::shared_ptr<erhe::scene::Mesh>         z_arrow_cylinder_mesh;
        std::shared_ptr<erhe::scene::Mesh>         z_arrow_cone_mesh;
        std::shared_ptr<erhe::scene::Mesh>         xy_box_mesh;
        std::shared_ptr<erhe::scene::Mesh>         xz_box_mesh;
        std::shared_ptr<erhe::scene::Mesh>         yz_box_mesh;
        std::shared_ptr<erhe::scene::Mesh>         x_rotate_ring_mesh;
        std::shared_ptr<erhe::scene::Mesh>         y_rotate_ring_mesh;
        std::shared_ptr<erhe::scene::Mesh>         z_rotate_ring_mesh;
    };
    Visualization m_visualization;

    class Debug_info
    {
    public:
        float distance;
    };
    //Debug_info m_debug_info;
};

} // namespace editor
