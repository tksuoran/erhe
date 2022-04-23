#pragma once

#include "scene/collision_generator.hpp"
#include "tools/tool.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/toolkit/optional.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <mutex>
#include <vector>

namespace erhe::geometry
{
    class Geometry;
}

namespace Physics
{
    class ICollision_shape;
}

namespace erhe::primitive
{
    class Build_info;
    class Material;
    class Primitive_geometry;
}

namespace erhe::scene
{
    class Mesh;
    class Node;
}

namespace editor
{

class Brush;
class Brushes;
class Editor;
class Grid_tool;
class Materials;
class Operation_stack;
class Pointer_context;
class Scene_root;
class Selection_tool;

class Brush_tool_preview_command
    : public erhe::application::Command
{
public:
    explicit Brush_tool_preview_command(Brushes& brushes)
        : Command  {"Brush_tool.motion_preview"}
        , m_brushes{brushes}
    {
    }

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Brushes& m_brushes;
};

class Brush_tool_insert_command
    : public erhe::application::Command
{
public:
    explicit Brush_tool_insert_command(Brushes& brushes)
        : Command  {"Brush_tool.insert"}
        , m_brushes{brushes}
    {
    }

    void try_ready(erhe::application::Command_context& context) override;
    auto try_call (erhe::application::Command_context& context) -> bool override;

private:
    Brushes& m_brushes;
};

class Brush_create_context
{
public:
    erhe::primitive::Build_info&  build_info;
    erhe::primitive::Normal_style normal_style{erhe::primitive::Normal_style::corner_normals};
};

class Brushes
    : public erhe::components::Component
    , public Tool
    , public erhe::application::Imgui_window
{
public:
    static constexpr int              c_priority{4};
    static constexpr std::string_view c_name   {"Brushes"};
    static constexpr std::string_view c_title  {"Brushes"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Brushes ();
    ~Brushes() noexcept override;

    // Implements erhe::components::Component
    [[nodiscard]]
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements erhe::application::Tool
    [[nodiscard]] auto tool_priority() const -> int   override { return c_priority; }
    [[nodiscard]] auto description  () -> const char* override { return c_name.data(); }
    void tool_properties        () override;
    void on_enable_state_changed() override;

    // Implements erhe::application::Imgui_window
    void imgui() override;

    // Public API

    // Commands
    auto try_insert_ready() -> bool;
    auto try_insert      () -> bool;
    void on_motion       ();

    [[nodiscard]] auto allocate_brush(
        erhe::primitive::Build_info& build_info
    ) -> std::shared_ptr<Brush>;

    [[nodiscard]] auto make_brush(
        erhe::geometry::Geometry&&                              geometry,
        const Brush_create_context&                             context,
        const std::shared_ptr<erhe::physics::ICollision_shape>& collision_shape = {}
    ) -> std::shared_ptr<Brush>;

    [[nodiscard]] auto make_brush(
        const std::shared_ptr<erhe::geometry::Geometry>&        geometry,
        const Brush_create_context&                             context,
        const std::shared_ptr<erhe::physics::ICollision_shape>& collision_shape = {}
    ) -> std::shared_ptr<Brush>;

    [[nodiscard]] auto make_brush(
        const std::shared_ptr<erhe::geometry::Geometry>& geometry,
        const Brush_create_context&                      context,
        const Collision_volume_calculator                collision_volume_calculator,
        const Collision_shape_generator                  collision_shape_generator
    ) -> std::shared_ptr<Brush>;

private:
    void update_mesh               ();

    [[nodiscard]]
    auto get_brush_transform       () -> glm::mat4; // Places brush in parent (hover) mesh

    void do_insert_operation       ();
    void add_brush_mesh            ();
    void remove_brush_mesh         ();
    void update_mesh_node_transform();

    Brush_tool_preview_command m_preview_command;
    Brush_tool_insert_command  m_insert_command;

    // Component dependencies
    std::shared_ptr<Materials>       m_materials;
    std::shared_ptr<Operation_stack> m_operation_stack;
    std::shared_ptr<Pointer_context> m_pointer_context;
    std::shared_ptr<Scene_root>      m_scene_root;
    std::shared_ptr<Selection_tool>  m_selection_tool;
    std::shared_ptr<Grid_tool>       m_grid_tool;

    std::mutex                          m_brush_mutex;
    std::vector<std::shared_ptr<Brush>> m_brushes;

    Brush*                              m_brush{nullptr};
    int                                 m_selected_brush_index{0};
    bool                                m_snap_to_hover_polygon{true};
    bool                                m_snap_to_grid         {false};
    bool                                m_hover_content        {false};
    bool                                m_hover_tool           {false};
    nonstd::optional<glm::vec3>         m_hover_position;
    nonstd::optional<glm::vec3>         m_hover_normal;
    std::shared_ptr<erhe::scene::Mesh>  m_hover_mesh;
    erhe::geometry::Geometry*           m_hover_geometry   {nullptr};
    size_t                              m_hover_primitive  {0};
    size_t                              m_hover_local_index{0};
    std::shared_ptr<erhe::scene::Mesh>  m_brush_mesh;
    float                               m_scale            {1.0f};
    float                               m_transform_scale  {1.0f};

    class Debug_info
    {
    public:
        float hover_frame_scale{0.0f};
        float brush_frame_scale{0.0f};
        float transform_scale{0.0f};
    };
    Debug_info debug_info;
};

} // namespace editor
