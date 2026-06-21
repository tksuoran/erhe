#pragma once

#include "tools/tool.hpp"

#include "app_message.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_renderer/primitive_renderer.hpp"

#include <geogram/basic/numeric.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace erhe::commands { class Commands; }
namespace erhe::geometry { class Geometry; }
namespace erhe::scene    { class Mesh; }

namespace editor {

class App_context;
class App_message_bus;
class Mesh_component_selection;
class Mesh_component_selection_tool;
class Scene_view;

// Left-mouse command that selects a mesh sub-component (vertex / edge / face)
// under the pointer. It only becomes ready while a component mode is active,
// so in Object mode it stays inactive and the object Selection handles the
// click unchanged.
class Component_select_command : public erhe::commands::Command
{
public:
    Component_select_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready() override;
    auto try_call () -> bool override;

private:
    App_context& m_context;
};

// Blender-style mesh component selection tool. A background tool whose mode
// (Object / Vertex / Edge / Face, held by Mesh_component_selection) controls
// whether it intercepts viewport clicks. Renders the current selection and the
// hovered component into the desktop viewport via Primitive_renderer. Initial
// scope: non-skinned meshes, single active mesh, no editing.
class Mesh_component_selection_tool : public Tool
{
public:
    static constexpr int c_priority{4};

    Mesh_component_selection_tool(
        erhe::commands::Commands&    commands,
        App_context&                 context,
        App_message_bus&             app_message_bus,
        Mesh_component_selection&    mesh_component_selection,
        Tools&                       tools
    );

    // Implements Tool
    void tool_render(const Render_context& context) override;

    // Contributes the mode combo + clear button to the viewport toolbar
    // (called from Viewport_scene_view::viewport_toolbar).
    void viewport_toolbar();

    // Called by Component_select_command.
    [[nodiscard]] auto try_ready() const -> bool;
    auto               on_select()       -> bool;

private:
    // Component under the pointer for the active mode, resolved from the
    // content Hover_entry. Edge fields hold the picked edge's vertex pair.
    class Pick_result
    {
    public:
        bool                                      valid          {false};
        std::shared_ptr<erhe::scene::Mesh>        mesh           {};
        std::size_t                               primitive_index{0};
        std::shared_ptr<erhe::geometry::Geometry> geometry       {};
        GEO::index_t                              facet          {0};
        GEO::index_t                              vertex         {0};
        GEO::index_t                              edge_v0        {0};
        GEO::index_t                              edge_v1        {0};
    };

    [[nodiscard]] auto pick(Scene_view& scene_view) const -> Pick_result;

    // Smooth local-space normal at a vertex (area-weighted average of incident
    // facet normals) and the world-space normal of an edge (mean of its two
    // endpoint normals, transformed by normal_matrix). Used to bias selected /
    // hovered edge lines off the surface.
    [[nodiscard]] auto vertex_normal_local(const erhe::geometry::Geometry& geometry, GEO::index_t vertex) const -> glm::vec3;
    [[nodiscard]] auto edge_world_normal   (const erhe::geometry::Geometry& geometry, const glm::mat3& normal_matrix, GEO::index_t v0, GEO::index_t v1) const -> glm::vec3;

    // Append a facet's fan triangulation (mesh-local positions + indices) to
    // the scratch buffers. Caller renders them with transform = world_from_node.
    void append_facet_triangles(const erhe::geometry::Geometry& geometry, GEO::index_t facet);
    // Append a camera-facing quad (2 triangles, world space) for a vertex.
    void append_vertex_quad(const glm::vec3& position_in_world, const glm::vec3& camera_right, const glm::vec3& camera_up, float half_size);

    Mesh_component_selection&                                 m_mesh_component_selection;
    erhe::message_bus::Subscription<Hover_scene_view_message> m_hover_scene_view_subscription;
    Component_select_command                                  m_select_command;

    // Visual style (colors, edge thickness, vertex size, edge depth bias) lives
    // editor-global in Editor_settings_config::mesh_component_style so it is
    // covered by codegen serialization / autosave; tool_render reads it from
    // app_context.editor_settings and edits happen in the Settings window.

    // Per-frame scratch (cleared each frame, capacity retained). tool_render
    // is hot-path, so it must not allocate transient containers (see CLAUDE.md
    // run-time allocation discipline).
    std::vector<glm::vec3>            m_scratch_positions;
    std::vector<uint32_t>             m_scratch_indices;
    std::vector<erhe::renderer::Line> m_scratch_lines;
    std::vector<glm::vec3>            m_scratch_normals;        // hover edge (single averaged normal)
    std::vector<glm::vec3>            m_scratch_face_normals_a; // selected edges: face A normal (endpoint 0)
    std::vector<glm::vec3>            m_scratch_face_normals_b; // selected edges: face B normal (endpoint 1)
    std::vector<float>                m_scratch_signs_a;        // selected edges: face A interior-tangent sign
};

} // namespace editor
