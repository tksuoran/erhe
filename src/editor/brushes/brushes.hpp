#pragma once

#include "scene/collision_generator.hpp"
#include "tools/tool.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/primitive/enums.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace erhe::geometry
{
    class Geometry;
}

namespace Physics
{
    class ICollision_shape;
}

namespace erhe::application
{
    class Configuration;
    class Line_renderer_set;
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
    class Transform;
}

namespace editor
{

class Brush;
class Brushes;
class Editor;
class Editor_scenes;
class Grid_tool;
class Materials_window;
class Mesh_memory;
class Operation_stack;
class Render_context;
class Selection_tool;
class Viewport_windows;

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

class Brush_data;

class Brushes
    : public erhe::components::Component
    , public Tool
{
public:
    static constexpr int              c_priority {4};
    static constexpr std::string_view c_type_name{"Brushes"};
    static constexpr std::string_view c_title    {"Brushes"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Brushes ();
    ~Brushes() noexcept override;

    // Implements erhe::components::Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto tool_priority() const -> int   override { return c_priority; }
    [[nodiscard]] auto description  () -> const char* override { return c_title.data(); }
    void tool_properties        () override;
    void on_enable_state_changed() override;

    // Public API
    void brush_palette(int& selected_brush_index);

    // Commands
    auto try_insert_ready() -> bool;
    auto try_insert      () -> bool;
    void on_motion       ();

    void register_brush(const std::shared_ptr<Brush>& brush);

    [[nodiscard]] auto make_brush(const Brush_data& create_info) -> std::shared_ptr<Brush>;

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
    std::shared_ptr<erhe::application::Configuration> m_configuration;
    std::shared_ptr<Editor_scenes   > m_editor_scenes;
    std::shared_ptr<Grid_tool       > m_grid_tool;
    std::shared_ptr<Materials_window> m_materials_window;
    std::shared_ptr<Operation_stack > m_operation_stack;
    std::shared_ptr<Selection_tool  > m_selection_tool;
    std::shared_ptr<Viewport_windows> m_viewport_windows;

    std::mutex                          m_brush_mutex;
    std::vector<std::shared_ptr<Brush>> m_brushes;

    std::weak_ptr<Brush>                m_brush                {};
    bool                                m_snap_to_hover_polygon{true};
    bool                                m_snap_to_grid         {true};
    bool                                m_hover_content        {false};
    bool                                m_hover_tool           {false};
    std::optional<glm::vec3>            m_hover_position;
    std::optional<glm::vec3>            m_hover_normal;
    std::shared_ptr<erhe::scene::Mesh>  m_hover_mesh;
    erhe::geometry::Geometry*           m_hover_geometry   {nullptr};
    std::size_t                         m_hover_primitive  {0};
    std::size_t                         m_hover_local_index{0};
    std::shared_ptr<erhe::scene::Mesh>  m_brush_mesh;
    bool                                m_with_physics{true};
    float                               m_scale            {1.0f};
    float                               m_transform_scale  {1.0f};
    int                                 m_polygon_offset   {0};
    int                                 m_corner_offset    {0};

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
