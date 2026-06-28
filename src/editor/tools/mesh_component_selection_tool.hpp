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
class Viewport_scene_view;

// Gesture sub-mode of the component selection tool. Click is the legacy
// single-pick behavior; Box drags a rectangle; Paint drags a brush disk
// (Blender Circle Select). Box and Paint scan the GPU id-buffer over a screen
// region to gather visible faces. Faces only for now.
enum class Component_gesture_mode {
    click = 0,
    box   = 1,
    paint = 2
};

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

// Left-mouse drag that box-selects faces. Ready only in Box gesture sub-mode +
// Face component mode; once the drag moves it becomes the active mouse command
// (blocking the single-click command). The selection is committed a few frames
// after release, when the async id-buffer region scan completes (see
// Mesh_component_selection_tool::gesture_update).
class Component_box_select_command : public erhe::commands::Command
{
public:
    Component_box_select_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready          () override;
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;
    void on_inactive        () override;

private:
    App_context& m_context;
};

// Per-frame update command (bound via bind_command_to_update) that drives the
// deferred box commit and paint accumulation, and keeps the brush-radius wheel
// command armed while paint-selecting. Never consumes input.
class Component_gesture_update_command : public erhe::commands::Command
{
public:
    Component_gesture_update_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

// Left-mouse drag that paint-selects faces under a brush disk (Blender Circle
// Select). Ready only in Paint gesture sub-mode + Face component mode. Bound
// with call_on_button_down_without_motion so a single click selects under the
// brush too. Modifiers (captured at stroke start): plain/Shift add (plain also
// clears first), Ctrl subtract. Faces are added continuously along the path.
class Component_paint_select_command : public erhe::commands::Command
{
public:
    Component_paint_select_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready          () override;
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;
    void on_inactive        () override;

private:
    App_context& m_context;
};

// Mouse wheel that resizes the paint brush. Kept Ready (by gesture_update) while
// paint-selecting + hovering a viewport so it out-ranks the fly-camera zoom for
// the wheel (see Commands::sort_mouse_wheel_bindings); inactive otherwise so the
// wheel zooms the camera as usual.
class Component_brush_radius_command : public erhe::commands::Command
{
public:
    Component_brush_radius_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    App_context& m_context;
};

// Key that switches the gesture sub-mode (B -> Box, C -> Paint), a shortcut for
// the toolbar combo. Only consumes the key while in Face component mode, so the
// key falls through to other bindings (e.g. brush preview on C) otherwise.
class Component_gesture_hotkey_command : public erhe::commands::Command
{
public:
    Component_gesture_hotkey_command(erhe::commands::Commands& commands, App_context& context, const char* name, Component_gesture_mode mode);
    auto try_call() -> bool override;

private:
    App_context&           m_context;
    Component_gesture_mode m_mode;
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

    // Gesture sub-mode (click / box / paint), surfaced in viewport_toolbar and
    // settable from the B / C hotkeys.
    [[nodiscard]] auto get_gesture_mode() const -> Component_gesture_mode;
    void               set_gesture_mode(Component_gesture_mode mode);
    // B / C hotkey: switch to `mode` and consume, but only while in Face
    // component mode (returns false otherwise so the key falls through).
    [[nodiscard]] auto try_set_gesture_hotkey(Component_gesture_mode mode) -> bool;

    // Called by Component_box_select_command.
    [[nodiscard]] auto box_select_try_ready() const -> bool;
    // True while Box gesture sub-mode + Face component mode are both active
    // (the in-drag guard; does not require a hovered viewport, unlike
    // box_select_try_ready). Used to drop a drag command left armed across a
    // sub-mode change.
    [[nodiscard]] auto is_gesture_box_mode() const -> bool;
    void               box_select_update(glm::vec2 window_position, bool real_motion);
    void               box_select_release();

    // Called by Component_paint_select_command.
    [[nodiscard]] auto paint_select_try_ready() const -> bool;
    [[nodiscard]] auto is_gesture_paint_mode() const -> bool;
    void               paint_select_update(glm::vec2 window_position, bool real_motion);
    void               paint_select_release();

    // Called by Component_brush_radius_command (mouse wheel).
    void               adjust_brush_radius(float wheel_delta);

    // Called by Component_gesture_update_command once per frame.
    void gesture_update();

    // Debug/test entry (MCP debug_region_select): drive a region face-select over
    // an explicit viewport-pixel rectangle (or brush disk), bypassing the mouse.
    // Commits over the next frames via gesture_update, like a real gesture.
    void debug_region_select(int x, int y, int width, int height, bool is_brush, float brush_radius, bool replace, bool subtract);

    // Draws the rubber-band box (and, later, brush circle) into the viewport
    // window's ImGui draw list. Called by Viewport_window::imgui().
    void draw_gesture_overlay(const Viewport_scene_view* viewport_scene_view);

private:
    // Build and submit an id-buffer scan request for the current box rectangle
    // (window coords -> viewport coords via the gesture's scene view).
    void request_box_scan();
    // Build and submit an id-buffer disk scan for the current brush.
    void request_paint_scan();
    // Abandon any in-flight async region-scan drains (box deferred commit, paint
    // post-release drain, MCP debug scan) so their results are not applied. Used
    // by the Clear button: an explicit clear must be authoritative even while a
    // just-released gesture's final scan is still completing a few frames later.
    void cancel_pending_scans();
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
    Component_box_select_command                              m_box_select_command;
    Component_gesture_update_command                          m_gesture_update_command;
    Component_paint_select_command                            m_paint_select_command;
    Component_brush_radius_command                            m_brush_radius_command;
    Component_gesture_hotkey_command                          m_box_hotkey_command;
    Component_gesture_hotkey_command                          m_paint_hotkey_command;

    // Gesture sub-mode + box-select state (faces only). The box is stored in
    // window coordinates (for the ImGui overlay) and converted to viewport
    // coordinates when building the id-buffer scan request. The commit is
    // deferred: on release we wait for a scan whose pixels are from at-or-after
    // the first post-release request (m_box_commit_request_frame), so the result
    // reflects the final rectangle. Modifiers are captured at release
    // (Blender: plain = replace, Shift = add, Ctrl = subtract).
    Component_gesture_mode m_gesture_mode            {Component_gesture_mode::click};
    bool                   m_box_active              {false};
    Scene_view*            m_box_scene_view          {nullptr};
    glm::vec2              m_box_anchor_window       {0.0f, 0.0f};
    glm::vec2              m_box_current_window      {0.0f, 0.0f};
    bool                   m_box_commit_pending      {false};
    bool                   m_box_modifier_shift      {false};
    bool                   m_box_modifier_ctrl       {false};
    uint64_t               m_box_commit_request_frame{0};

    // Paint (Blender Circle Select) state. The brush is a disk of m_brush_radius
    // pixels (window == viewport pixels: get_viewport_from_window only
    // translates / Y-flips, no scaling). Results are applied continuously as
    // they arrive (add or subtract); the plain stroke clears once at its start.
    bool                   m_paint_active            {false};
    bool                   m_paint_pending           {false};
    bool                   m_paint_subtract          {false};
    Scene_view*            m_paint_scene_view        {nullptr};
    glm::vec2              m_brush_center_window     {0.0f, 0.0f};
    float                  m_brush_radius            {32.0f};
    uint64_t               m_paint_last_applied_frame{0};
    // Post-release drain target, captured ONCE on the first post-release frame
    // (mirrors m_box_commit_request_frame): the drain ends when a scan from
    // at-or-after this frame has been applied. It must be frozen, not advanced
    // per frame -- the post-release drain re-requests the brush scan every frame
    // (to survive a dropped release-frame request), so a target that tracked the
    // latest request would forever outrun the readback-lagged applied frame and
    // the last dab would be re-applied every frame (which, among other things,
    // undid the Clear button).
    uint64_t               m_paint_commit_request_frame{0};

    // Debug region-select state (MCP-driven, viewport coordinates directly).
    bool                   m_debug_pending      {false};
    bool                   m_debug_replace      {false};
    bool                   m_debug_subtract     {false};
    bool                   m_debug_is_brush     {false};
    int                    m_debug_x            {0};
    int                    m_debug_y            {0};
    int                    m_debug_w            {0};
    int                    m_debug_h            {0};
    float                  m_debug_brush_radius {0.0f};
    uint64_t               m_debug_request_frame{0};

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
