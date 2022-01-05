#pragma once

#include "command.hpp"
#include "tools/tool.hpp"
#include "tools/selection_tool.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/components/component.hpp"
#include "erhe/scene/node.hpp"

#include <glm/glm.hpp>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string_view>

namespace erhe::physics
{
    enum class Motion_mode : unsigned int;
    class Rigid_body;
}

namespace erhe::primitive
{
    class Geometry;
    class Material;
    class Primitive_geometry;
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

class Log_window;
class Line_renderer;
class Node_physics;
class Mesh_memory;
class Operation_stack;
class Pointer_context;
class Scene_root;
class Text_renderer;
class Trs_tool;

class Trs_tool_drag_command
    : public Command
{
public:
    explicit Trs_tool_drag_command(Trs_tool& trs_tool)
        : Command   {"Trs_tool.drag"}
        , m_trs_tool{trs_tool}
    {
    }

    auto try_call   (Command_context& context) -> bool override;
    void try_ready  (Command_context& context) override;
    void on_inactive(Command_context& context) override;

private:
    Trs_tool& m_trs_tool;
};

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

    static constexpr int              c_priority   {1};
    static constexpr std::string_view c_name       {"Trs_tool"};
    static constexpr std::string_view c_description{"Transform"};
    static constexpr uint32_t         hash         {
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Trs_tool ();
    ~Trs_tool() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Tool
    [[nodiscard]] auto tool_priority() const -> int   override { return c_priority; }
    [[nodiscard]] auto description  () -> const char* override;
    void begin_frame            ()                              override;
    void tool_render            (const Render_context& context) override;
    void on_enable_state_changed()                              override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    void set_translate(const bool enabled);
    void set_rotate   (const bool enabled);

    // Commands
    auto on_drag_ready() -> bool;
    auto on_drag      () -> bool;
    void end_drag     ();

private:
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

    class Drag
    {
    public:
        glm::dmat4             initial_world_from_local {1.0};
        glm::dmat4             initial_local_from_world {1.0};
        glm::dvec3             initial_position_in_world{0.0, 0.0, 0.0};
        double                 initial_window_depth     {0.0};
        erhe::scene::Transform initial_parent_from_node_transform;
    };

    class Rotation_context
    {
    public:
        glm::dvec3                normal              {0.0}; // also rotation axis
        glm::dvec3                reference_direction {0.0};
        glm::dvec3                center_of_rotation  {0.0};
        std::optional<glm::dvec3> intersection;
        double                    start_rotation_angle{0.0};
        double                    current_angle       {0.0};
        erhe::scene::Transform    world_from_node;
    };

    class Visualization
    {
    public:
        Visualization();

        void initialize       (Mesh_memory& mesh_memory, Scene_root& scene_root);
        void update_visibility(const bool show, const Handle active_handle);
        void update_scale     (const glm::vec3 view_position_in_world);
        void update_transforms(const uint64_t serial);

        auto make_mesh(
            Scene_root&                                       scene_root,
            const std::string_view                            name,
            const std::shared_ptr<erhe::primitive::Material>& material,
            erhe::primitive::Primitive_geometry&              primitive_geometry
        ) -> std::shared_ptr<erhe::scene::Mesh>;

        bool  show_translate{true};
        bool  show_rotate   {false};
        bool  hide_inactive {true};
        float scale         {3.5f}; // 3.5 for normal, 6.6 for debug

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

    [[nodiscard]] auto project_pointer_to_plane (const glm::dvec3 n, const glm::dvec3 p) -> std::optional<glm::dvec3>;
    [[nodiscard]] auto root                     () -> erhe::scene::Node*;
    [[nodiscard]] auto snap_translate           (const glm::dvec3 translation) const -> glm::dvec3;
    [[nodiscard]] auto snap_rotate              (const double angle_radians) const -> double;
    [[nodiscard]] auto is_x_translate_active    () const -> bool;
    [[nodiscard]] auto is_y_translate_active    () const -> bool;
    [[nodiscard]] auto is_z_translate_active    () const -> bool;
    [[nodiscard]] auto is_rotate_active         () const -> bool;
    [[nodiscard]] auto get_handle               (erhe::scene::Mesh* mesh) const -> Trs_tool::Handle;
    [[nodiscard]] auto get_handle_type          (const Handle handle) const -> Handle_type;
    [[nodiscard]] auto offset_plane_origo       (const Handle handle, const glm::dvec3 p) const -> glm::dvec3;
    [[nodiscard]] auto project_to_offset_plane  (const Handle handle, const glm::dvec3 p, const glm::dvec3 q) const -> glm::dvec3;
    [[nodiscard]] auto get_axis_direction       () const -> glm::dvec3;
    [[nodiscard]] auto get_plane_normal         (const bool world) const -> glm::dvec3;
    [[nodiscard]] auto get_plane_side           (const bool world) const -> glm::dvec3;
    [[nodiscard]] auto get_axis_color           (Handle handle) const -> uint32_t;

    void set_node                   (const std::shared_ptr<erhe::scene::Node>& node);
    void hide                       ();
    void update_axis_translate      ();
    void update_axis_translate_final(const glm::dvec3 drag_position);
    void update_plane_translate     ();
    void update_rotate              ();
    auto update_rotate_circle_around() -> bool;
    auto update_rotate_parallel     () -> bool;
    void update_rotate_final        ();
    void set_node_world_transform   (const glm::dmat4 world_from_node);
    void update_transforms          ();
    void update_visibility          ();

    Trs_tool_drag_command                      m_drag_command;

    // Component dependencies
    std::shared_ptr<Log_window>                m_log_window;
    std::shared_ptr<Line_renderer>             m_line_renderer;
    std::shared_ptr<Mesh_memory>               m_mesh_memory;
    std::shared_ptr<Operation_stack>           m_operation_stack;
    std::shared_ptr<Pointer_context>           m_pointer_context;
    std::shared_ptr<Scene_root>                m_scene_root;
    std::shared_ptr<Selection_tool>            m_selection_tool;
    std::shared_ptr<Text_renderer>             m_text_renderer;

    bool                                       m_local          {true};
    bool                                       m_touched        {false};
    Handle                                     m_active_handle  {Handle::e_handle_none};
    std::optional<Selection_tool::Subcription> m_selection_subscription;
    std::map<erhe::scene::Mesh*, Handle>       m_handles;
    std::shared_ptr<erhe::scene::Node>         m_target_node;
    std::shared_ptr<Node_physics>              m_node_physics;
    std::shared_ptr<erhe::scene::Node>         m_tool_node;
    std::optional<erhe::physics::Motion_mode>  m_original_motion_mode;
    bool                                       m_translate_snap_enable{false};
    bool                                       m_rotate_snap_enable   {false};
    int                                        m_translate_snap_index {2};
    float                                      m_translate_snap       {0.1f};
    int                                        m_rotate_snap_index    {2};
    float                                      m_rotate_snap          {15.0f};
    erhe::scene::Transform                     m_parent_from_node_before;

    Debug_rendering  m_debug_rendering;
    Drag             m_drag;
    Rotation_context m_rotation;
    Visualization    m_visualization;
};

} // namespace editor
