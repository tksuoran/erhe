#pragma once

#include "scene/collision_generator.hpp"
#include "tools/tool.hpp"
#include "scene/scene_view.hpp"

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
    class Text_renderer;
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
class Brush_tool;
class Content_library_window;
class Editor;
class Editor_scenes;
class Grid;
class Grid_tool;
class Headset_view;
class Mesh_memory;
class Operation_stack;
class Render_context;
class Selection_tool;
class Viewport_windows;

class Brush_tool_preview_command
    : public erhe::application::Command
{
public:
    explicit Brush_tool_preview_command(Brush_tool& brush_tool);

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Brush_tool& m_brush_tool;
};

class Brush_tool_insert_command
    : public erhe::application::Command
{
public:
    explicit Brush_tool_insert_command(Brush_tool& brush_tool);

    void try_ready(erhe::application::Command_context& context) override;
    auto try_call (erhe::application::Command_context& context) -> bool override;

private:
    Brush_tool& m_brush_tool;
};

class Brush_data;
class Editor_message;

class Brush_tool
    : public erhe::components::Component
    , public Tool
{
public:
    static constexpr int              c_priority {4};
    static constexpr std::string_view c_type_name{"Brush_tool"};
    static constexpr std::string_view c_title    {"Brush Tool"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Brush_tool ();
    ~Brush_tool() noexcept override;

    // Implements erhe::components::Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    void tool_render           (const Render_context& context) override;
    void tool_properties       () override;
    void handle_priority_update(int old_priority, int new_priority) override;

    // Commands
    auto try_insert_ready() -> bool;
    auto try_insert      () -> bool;
    void on_motion       ();

private:
    void on_message                (Editor_message& editor_message);
    void update_mesh               ();
    void do_insert_operation       ();
    void add_brush_mesh            ();
    void remove_brush_mesh         ();
    void update_mesh_node_transform();

    [[nodiscard]] auto get_hover_mesh_transform() -> glm::mat4; // Places brush in parent (hover) mesh
    [[nodiscard]] auto get_hover_grid_transform() -> glm::mat4;

    Brush_tool_preview_command m_preview_command;
    Brush_tool_insert_command  m_insert_command;

    // Component dependencies
    std::shared_ptr<erhe::application::Configuration>     m_configuration;
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<erhe::application::Text_renderer>     m_text_renderer;
    std::shared_ptr<Content_library_window> m_content_library_window;
    std::shared_ptr<Editor_scenes   >       m_editor_scenes;
    std::shared_ptr<Grid_tool       >       m_grid_tool;
    std::shared_ptr<Operation_stack >       m_operation_stack;
    std::shared_ptr<Selection_tool  >       m_selection_tool;
    std::shared_ptr<Viewport_windows>       m_viewport_windows;
    std::shared_ptr<Headset_view    >       m_headset_view;

    std::mutex                          m_brush_mutex;
    bool                                m_snap_to_hover_polygon{true};
    bool                                m_snap_to_grid         {true};
    bool                                m_debug_visualization  {false};
    Hover_entry                         m_hover;
    std::shared_ptr<erhe::scene::Mesh>  m_brush_mesh;
    std::shared_ptr<erhe::scene::Node>  m_brush_node;
    bool                                m_with_physics     {true};
    float                               m_scale            {1.0f};
    float                               m_transform_scale  {1.0f};
    int                                 m_polygon_offset   {0};
    int                                 m_corner_offset    {0};
    Scene_view*                         m_scene_view       {nullptr};
};

} // namespace editor
