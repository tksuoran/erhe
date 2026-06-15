#include "operations/operations_window.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "app_message_bus.hpp"
#include "brushes/brush.hpp"
#include "app_settings.hpp"
#include "content_library/content_library.hpp"
#include "content_library/content_library_window.hpp"
#include "items.hpp"
#include "operations/geometry_operations.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/item_parent_change_operation.hpp"
#include "operations/merge_operation.hpp"
#include "operations/mesh_operation.hpp"
#include "operations/node_transform_operation.hpp"
#include "operations/operation_stack.hpp"
#include "parsers/gltf_physics_export.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"

#include <algorithm>
#include "scene/scene_serialization.hpp"
#include "scene/viewport_scene_views.hpp"
#include "brushes/reference_frame.hpp"
#include "tools/mesh_component_selection.hpp"
#include "tools/selection_tool.hpp"
#include "windows/item_tree_window.hpp"

#include "erhe_rendergraph/rendergraph_node.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_window/window_event_handler.hpp"
#include "config/generated/scene_config.hpp"
#include "erhe_defer/defer.hpp"
#include "erhe_file/file.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_physics/physics_material.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <geogram/mesh/mesh.h>

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <imgui/imgui.h>

#if defined(ERHE_WINDOW_LIBRARY_SDL)
# include <SDL3/SDL_dialog.h>
#endif

#include <taskflow/taskflow.hpp>  // Taskflow is header-only

namespace editor {

namespace {

using erhe::geometry::get_pointf;
using erhe::geometry::mesh_facet_centerf;
using erhe::geometry::to_geo_mat4f;
using erhe::geometry::to_glm_mat4;

// One selected mesh component (vertex / edge / face) resolved to world space,
// plus the owning node and enough source identity to build a face frame.
class Selected_component
{
public:
    std::shared_ptr<erhe::scene::Node>        node;
    std::shared_ptr<erhe::geometry::Geometry> geometry;                  // face only (to build the TNB frame)
    glm::vec3                                 world_position {0.0f};      // vertex pos / edge midpoint / facet centroid
    glm::vec3                                 world_direction{0.0f};      // edge only: unit tangent (zero if degenerate)
    float                                     world_size{0.0f};          // edge only: edge length in world (for Align with Scale)
    GEO::index_t                              facet{GEO::NO_INDEX};       // face only
};

// A facet's world-space frame for Align: an orthonormal (unit-axis) TNB basis
// plus the centroid translation, and a characteristic facet size used to derive
// a uniform scale factor in Align with Scale.
class Face_frame
{
public:
    glm::mat4 basis{1.0f};
    float     scale{0.0f};
};

// Shortest-arc rotation taking unit vector `from` onto unit vector `to`. Callers
// pass `to` chosen as the nearer of +-target, so the two are never antiparallel
// and the cross product is well defined; returns identity when already aligned.
[[nodiscard]] auto shortest_arc(const glm::vec3& from, const glm::vec3& to) -> glm::quat
{
    const float d = glm::dot(from, to);
    if (d >= 1.0f - 1e-6f) {
        return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    }
    const glm::vec3 axis = glm::cross(from, to);
    const float     s    = std::sqrt((1.0f + d) * 2.0f);
    const float     inv  = 1.0f / s;
    return glm::normalize(glm::quat{s * 0.5f, axis.x * inv, axis.y * inv, axis.z * inv});
}

// Append every selected component of `mode` (across all live entries, in entry
// then sorted-component order) resolved to world space. Allocating: call only on
// invocation, never per frame.
void gather_components(Mesh_component_selection& selection, const Mesh_component_mode mode, std::vector<Selected_component>& out)
{
    out.clear();
    for (const Mesh_component_entry& entry : selection.get_entries()) {
        if (!selection.is_live(entry)) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Mesh>        mesh     = entry.mesh.lock();
        const std::shared_ptr<erhe::geometry::Geometry> geometry = entry.geometry.lock();
        if (!mesh || !geometry) {
            continue;
        }
        erhe::scene::Node* const node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Node> node_shared = std::dynamic_pointer_cast<erhe::scene::Node>(node->shared_from_this());
        if (!node_shared) {
            continue;
        }
        const glm::mat4  world_from_node = node->world_from_node();
        const GEO::Mesh& geo_mesh        = geometry->get_mesh();
        const auto to_world = [&](const GEO::vec3f& p) -> glm::vec3 {
            return glm::vec3{world_from_node * glm::vec4{p.x, p.y, p.z, 1.0f}};
        };

        switch (mode) {
            case Mesh_component_mode::vertex: {
                for (const GEO::index_t vertex : entry.vertices) {
                    Selected_component& component = out.emplace_back();
                    component.node           = node_shared;
                    component.world_position = to_world(get_pointf(geo_mesh.vertices, vertex));
                }
                break;
            }
            case Mesh_component_mode::edge: {
                for (const Mesh_edge_key& edge : entry.edges) {
                    const glm::vec3 world_p0 = to_world(get_pointf(geo_mesh.vertices, edge.first));
                    const glm::vec3 world_p1 = to_world(get_pointf(geo_mesh.vertices, edge.second));
                    const glm::vec3 delta    = world_p1 - world_p0;
                    const float     len      = glm::length(delta);

                    Selected_component& component = out.emplace_back();
                    component.node            = node_shared;
                    component.world_position  = 0.5f * (world_p0 + world_p1);
                    component.world_direction = (len > 1e-8f) ? (delta / len) : glm::vec3{0.0f};
                    component.world_size      = len;
                }
                break;
            }
            case Mesh_component_mode::face: {
                for (const GEO::index_t facet : entry.facets) {
                    Selected_component& component = out.emplace_back();
                    component.node           = node_shared;
                    component.geometry       = geometry;
                    component.facet          = facet;
                    component.world_position = to_world(mesh_facet_centerf(geo_mesh, facet));
                }
                break;
            }
            case Mesh_component_mode::object:
            default: {
                break;
            }
        }
    }
}

// World-space TNB basis (axes in columns, centroid in translation) for a facet,
// with the given normal orientation. Reuses the brush-placement Reference_frame:
// build it in geometry-local space, then push it through world_from_node.
[[nodiscard]] auto facet_world_frame(const Selected_component& component, const Frame_orientation orientation) -> Face_frame
{
    Reference_frame frame{component.geometry->get_mesh(), component.facet, 0, 0, orientation};
    frame.transform_by(to_geo_mat4f(component.node->world_from_node()));
    return Face_frame{
        .basis = to_glm_mat4(frame.transform(0.0f)),
        .scale = frame.scale()
    };
}

// Queue an undoable node transform that left-multiplies `world_delta` onto the
// moved node's current world matrix (rigid delta -> the node's own scale/shear is
// preserved), converting back to a parent-relative transform.
[[nodiscard]] auto queue_align(App_context& context, const std::shared_ptr<erhe::scene::Node>& moved_node, const glm::mat4& world_delta) -> bool
{
    erhe::scene::Node* const node                   = moved_node.get();
    const glm::mat4          new_world_from_node    = world_delta * node->world_from_node();
    const glm::mat4          parent_from_node_after = node->parent_from_world() * new_world_from_node;

    context.operation_stack->queue(
        std::make_shared<Node_transform_operation>(
            Node_transform_operation::Parameters{
                .node                    = moved_node,
                .parent_from_node_before = node->parent_from_node_transform(),
                .parent_from_node_after  = erhe::scene::Transform{parent_from_node_after},
                .time_duration           = 0.0f
            }
        )
    );
    return true;
}

} // anonymous namespace

template<typename T>
void Operations::async_mesh_operation()
{
    async_for_nodes_with_mesh(
        m_context,
        m_context.selection->get_selected_items(),
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<T>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::async_for_selected_nodes_with_mesh(std::function<void(Mesh_operation_parameters&&)> op)
{
    async_for_nodes_with_mesh(m_context, m_context.selection->get_selected_items(), op);
}

Operations::Operations(
    const Scene_config&          scene_config,
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context,
    App_message_bus&             app_message_bus
)
    : Imgui_window              {imgui_renderer, imgui_windows, "Operations", "operations"}
    , m_context                 {app_context}
    , m_merge_command           {commands, "Geometry.Merge",                     [this]() -> bool { merge           (); return true; } }
    , m_align_command           {commands, "Geometry.Align",                     [this]() -> bool { return align_selection(false); } }
    , m_align_with_scale_command{commands, "Geometry.AlignWithScale",            [this]() -> bool { return align_selection(true ); } }
    , m_triangulate_command     {commands, "Geometry.Triangulate",               [this]() -> bool { triangulate     (); return true; } }
    , m_normalize_command       {commands, "Geometry.Normalize",                 [this]() -> bool { normalize       (); return true; } }
    , m_bake_transform_command  {commands, "Geometry.BakeTransform",             [this]() -> bool { bake_transform  (); return true; } }
    , m_center_transform_command{commands, "Geometry.CenterTransform",           [this]() -> bool { center_transform(); return true; } }
    , m_reverse_command       {commands, "Geometry.Reverse",                   [this]() -> bool { reverse       (); return true; } }
    , m_repair_command        {commands, "Geometry.Repair",                    [this]() -> bool { repair        (); return true; } }
    , m_weld_command          {commands, "Geometry.Weld",                      [this]() -> bool { weld          (); return true; } }

    , m_remesh_command            {commands, "Geometry.Remesh.Isotropic",      [this]() -> bool { remesh            (); return true; } }
    , m_anisotropic_remesh_command{commands, "Geometry.Remesh.Anisotropic",    [this]() -> bool { anisotropic_remesh(); return true; } }
    , m_decimate_command          {commands, "Geometry.Remesh.Decimate",       [this]() -> bool { decimate          (); return true; } }
    , m_smooth_command            {commands, "Geometry.Remesh.Smooth",         [this]() -> bool { smooth            (); return true; } }
    , m_make_atlas_command        {commands, "Geometry.MakeAtlas",             [this]() -> bool { make_atlas        (); return true; } }

    , m_difference_command    {commands, "Geometry.Difference",                [this]() -> bool { difference    (); return true; } }
    , m_intersection_command  {commands, "Geometry.Intersection",              [this]() -> bool { intersection  (); return true; } }
    , m_union_command         {commands, "Geometry.Union",                     [this]() -> bool { union_        (); return true; } }

    , m_catmull_clark_command {commands, "Geometry.Subdivision.Catmull-Clark", [this]() -> bool { catmull_clark (); return true; } }
    , m_sqrt3_command         {commands, "Geometry.Subdivision.Sqrt3",         [this]() -> bool { sqrt3         (); return true; } }

    , m_dual_command          {commands, "Geometry.Conway.Dual",               [this]() -> bool { dual          (); return true; } }
    , m_join_command          {commands, "Geometry.Conway.Join",               [this]() -> bool { join          (); return true; } }
    , m_kis_command           {commands, "Geometry.Conway.Kis",                [this]() -> bool { kis           (); return true; } }
    , m_meta_command          {commands, "Geometry.Conway.Meta",               [this]() -> bool { meta          (); return true; } }
    , m_ortho_command         {commands, "Geometry.Conway.Ortho",              [this]() -> bool { ortho         (); return true; } }
    , m_ambo_command          {commands, "Geometry.Conway.Ambo",               [this]() -> bool { ambo          (); return true; } }
    , m_truncate_command      {commands, "Geometry.Conway.Truncate",           [this]() -> bool { truncate      (); return true; } }
    , m_gyro_command          {commands, "Geometry.Conway.Gyro",               [this]() -> bool { gyro          (); return true; } }
    , m_chamfer3_command      {commands, "Geometry.Conway.Chamfer3",           [this]() -> bool { chamfer3      (); return true; } }

    , m_generate_tangents_command{commands, "Geometry.GenerateTangents",      [this]() -> bool { generate_tangents(); return true; } }
    , m_make_geometry_command    {commands, "Mesh.MakeGeometry",              [this]() -> bool { make_geometry(); return true; } }
    , m_make_raytrace_command    {commands, "Mesh.MakeRaytrace",              [this]() -> bool { make_raytrace(); return true; } }

    , m_export_gltf_command   {commands, "File.Export.glTF",                   [this]() -> bool { export_gltf   (); return true; } }
    , m_save_scene_command    {commands, "File.SaveScene",                     [this]() -> bool { save_scene     (); return true; } }
    , m_load_scene_command    {commands, "File.LoadScene",                     [this]() -> bool { load_scene     (); return true; } }
    , m_create_material       {commands, "Create.Material",                    [this]() -> bool { create_material(); return true; } }
    , m_create_brush          {commands, "Create.Brush",                       [this]() -> bool { create_brush   (); return true; } }
    , m_create_physics_material{commands, "Create.PhysicsMaterial",            [this]() -> bool { create_physics_material(); return true; } }
    , m_create_collision_filter{commands, "Create.CollisionFilter",            [this]() -> bool { create_collision_filter(); return true; } }
    , m_create_joint_settings {commands, "Create.JointSettings",               [this]() -> bool { create_joint_settings(); return true; } }
{
    commands.register_command(&m_merge_command         );
    commands.register_command(&m_align_command         );
    commands.register_command(&m_align_with_scale_command);
    commands.register_command(&m_triangulate_command   );
    commands.register_command(&m_normalize_command     );
    commands.register_command(&m_bake_transform_command);
    commands.register_command(&m_center_transform_command);
    commands.register_command(&m_reverse_command       );
    commands.register_command(&m_repair_command        );
    commands.register_command(&m_weld_command          );

    commands.register_command(&m_remesh_command            );
    commands.register_command(&m_anisotropic_remesh_command);
    commands.register_command(&m_decimate_command          );
    commands.register_command(&m_smooth_command            );
    commands.register_command(&m_make_atlas_command        );

    commands.register_command(&m_difference_command  );
    commands.register_command(&m_intersection_command);
    commands.register_command(&m_union_command       );

    commands.register_command(&m_catmull_clark_command);
    commands.register_command(&m_sqrt3_command        );

    commands.register_command(&m_ortho_command   );
    commands.register_command(&m_dual_command    );
    commands.register_command(&m_join_command    );
    commands.register_command(&m_kis_command     );
    commands.register_command(&m_meta_command    );
    commands.register_command(&m_ambo_command    );
    commands.register_command(&m_truncate_command);
    commands.register_command(&m_gyro_command    );
    commands.register_command(&m_generate_tangents_command );
    commands.register_command(&m_make_geometry_command );
    commands.register_command(&m_make_raytrace_command );

    commands.register_command(&m_export_gltf_command);
    commands.register_command(&m_save_scene_command);
    commands.register_command(&m_load_scene_command);
    commands.register_command(&m_create_material);
    commands.register_command(&m_create_brush);
    commands.register_command(&m_create_physics_material);
    commands.register_command(&m_create_collision_filter);
    commands.register_command(&m_create_joint_settings);

    commands.bind_command_to_menu(&m_merge_command,            "Geometry.Merge");
    commands.bind_command_to_menu(&m_align_command,            "Geometry.Align");
    commands.bind_command_to_menu(&m_align_with_scale_command, "Geometry.Align with Scale");
    commands.bind_command_to_menu(&m_triangulate_command,      "Geometry.Triangulate");
    commands.bind_command_to_menu(&m_normalize_command,        "Geometry.Normalize");
    commands.bind_command_to_menu(&m_bake_transform_command,   "Geometry.Bake-Transform");
    commands.bind_command_to_menu(&m_center_transform_command, "Geometry.Canter-Transform");
    commands.bind_command_to_menu(&m_reverse_command,          "Geometry.Reverse");
    commands.bind_command_to_menu(&m_repair_command,           "Geometry.Repair");
    commands.bind_command_to_menu(&m_weld_command,             "Geometry.Weld");

    commands.bind_command_to_menu(&m_remesh_command,             "Geometry.Remesh.Isotropic");
    commands.bind_command_to_menu(&m_anisotropic_remesh_command, "Geometry.Remesh.Anisotropic");
    commands.bind_command_to_menu(&m_decimate_command,           "Geometry.Remesh.Decimate");
    commands.bind_command_to_menu(&m_smooth_command,             "Geometry.Remesh.Smooth");
    commands.bind_command_to_menu(&m_make_atlas_command,         "Geometry.Generate Texture Coordinates");

    commands.bind_command_to_menu(&m_difference_command,     "Geometry.CSG.Difference");
    commands.bind_command_to_menu(&m_intersection_command,   "Geometry.CSG.Intersection");
    commands.bind_command_to_menu(&m_union_command,          "Geometry.CSG.Union");

    commands.bind_command_to_menu(&m_catmull_clark_command, "Geometry.Subdivision.Catmull-Clark");
    commands.bind_command_to_menu(&m_sqrt3_command,         "Geometry.Subdivision.Sqrt(3)");

    commands.bind_command_to_menu(&m_dual_command    , "Geometry.Conway Operations.Dual");
    commands.bind_command_to_menu(&m_join_command    , "Geometry.Conway Operations.Join");
    commands.bind_command_to_menu(&m_kis_command     , "Geometry.Conway Operations.Kis");
    commands.bind_command_to_menu(&m_meta_command    , "Geometry.Conway Operations.Meta");
    commands.bind_command_to_menu(&m_ortho_command   , "Geometry.Conway Operations.Ortho");
    commands.bind_command_to_menu(&m_ambo_command    , "Geometry.Conway Operations.Ambo");
    commands.bind_command_to_menu(&m_truncate_command, "Geometry.Conway Operations.Truncate");
    commands.bind_command_to_menu(&m_gyro_command    , "Geometry.Conway Operations.Gyro");
    commands.bind_command_to_menu(&m_chamfer3_command, "Geometry.Conway Operations.Chamfer3");

    commands.bind_command_to_menu(&m_generate_tangents_command, "Geometry.Generate Tangents");
    commands.bind_command_to_menu(&m_make_geometry_command,     "Mesh.Make Geometry");
    commands.bind_command_to_menu(&m_make_raytrace_command,     "Mesh.Make Raytrace");

    commands.bind_command_to_menu(&m_export_gltf_command, "File.Export glTF");
    commands.bind_command_to_menu(&m_save_scene_command,  "File.Save Scene");
    commands.bind_command_to_menu(&m_load_scene_command,  "File.Load Scene");
    commands.bind_command_to_menu(&m_create_material,     "Create.Material");
    commands.bind_command_to_menu(&m_create_brush,        "Create.Brush from Selection");
    commands.bind_command_to_menu(&m_create_physics_material, "Create.Physics Material");
    commands.bind_command_to_menu(&m_create_collision_filter, "Create.Collision Filter");
    commands.bind_command_to_menu(&m_create_joint_settings,   "Create.Joint Settings");

    // Default keys for Align / Align with Scale (F7/F8 are otherwise unbound).
    commands.bind_command_to_key(&m_align_command,            erhe::window::Key_f8);
    commands.bind_command_to_key(&m_align_with_scale_command, erhe::window::Key_f7);

    m_make_mesh_config.instance_count = scene_config.instance_count;
    m_make_mesh_config.instance_gap   = scene_config.instance_gap;
    m_make_mesh_config.object_scale   = scene_config.object_scale;
    // detail and mass_scale moved to per-command args in commands.json;
    // the Operations UI uses Make_mesh_config defaults for them.

    m_hover_scene_view_subscription = app_message_bus.hover_scene_view.subscribe(
        [&](Hover_scene_view_message& message) {
            m_hover_scene_view = message.scene_view;
            if (message.scene_view != nullptr) {
                m_last_hover_scene_view = message.scene_view;
            }
        }
    );
    m_load_scene_file_subscription = app_message_bus.load_scene_file.subscribe(
        [&](Load_scene_file_message& message) {
            try {
                auto content_library = std::make_shared<Content_library>();
                auto scene_root = editor::load_scene(
                    &m_context,
                    m_context.app_message_bus,
                    m_context.app_scenes,
                    content_library,
                    message.path
                );
                if (scene_root) {
                    log_operations->info("Scene loaded: {}", scene_root->get_name());
                    prune_loaded_scene_windows();
                    m_loaded_content_library_windows.push_back(
                        Loaded_scene_window{
                            .scene_root = scene_root,
                            .content_library_window = std::make_shared<Content_library_window>(
                                *m_context.imgui_renderer,
                                *m_context.imgui_windows,
                                m_context,
                                content_library,
                                scene_root->get_name()
                            )
                        }
                    );
                    auto browser_window = scene_root->make_browser_window(
                        *m_context.imgui_renderer,
                        *m_context.imgui_windows,
                        m_context,
                        *m_context.app_settings
                    );
                    browser_window->show_window();

                    // Create viewport window for the loaded scene
                    std::shared_ptr<erhe::rendergraph::Rendergraph_node> rendergraph_output_node;
                    auto viewport_scene_view = m_context.scene_views->open_new_viewport_scene_view(
                        rendergraph_output_node,
                        scene_root
                    );
                    m_context.scene_views->create_viewport_window(
                        *m_context.imgui_renderer,
                        *m_context.imgui_windows,
                        *m_context.app_message_bus,
                        viewport_scene_view,
                        rendergraph_output_node,
                        scene_root->get_name(),
                        ""
                    );
                }
            } catch (...) {
                log_operations->error("exception: load scene");
            }
        }
    );
}


auto Operations::mesh_context() -> Mesh_operation_parameters
{
    return Mesh_operation_parameters{
        .context = m_context,
        .build_info{
            .primitive_types = {
                .fill_triangles  = true,
                .edge_lines      = true,
                .corner_points   = true,
                .centroid_points = true
            },
            .buffer_info     = m_context.mesh_memory->make_primitive_buffer_info()
        }
    };
}

// Special rule to count meshes as selected even when the node that
// contains the mesh is seletected and mesh itself is not selected.
auto Operations::count_selected_meshes() const -> size_t
{
    const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = m_context.selection->get_selected_items();
    std::size_t count = 0;
    for (const auto& item : selected_items) {
        auto node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (node) {
            for (const auto& attachment : node->get_attachments()) {
                if (erhe::is<erhe::scene::Mesh>(attachment)) {
                    ++count;
                }
            }
        } else if (erhe::is<erhe::scene::Mesh>(item)) {
            ++count;
        }
    }
    return count;
}

void Operations::imgui()
{
    ERHE_PROFILE_FUNCTION();

    Property_editor& p = m_property_editor;
    p.reset();

    bool scenes_open{false};
    p.push_group("Scenes", ImGuiTreeNodeFlags_DefaultOpen, 0.0f, &scenes_open);

    std::shared_ptr<erhe::primitive::Material> material = m_context.selection->get_last_selected<erhe::primitive::Material>();
    if (material) {
        p.add_entry("Material", [material](){ ImGui::TextUnformatted(material->get_name().c_str()); });
    }
    m_make_mesh_config.material = material;
   
    p.add_entry("Count", [this](){ ImGui::SliderInt  ("##", &m_make_mesh_config.instance_count, 1, 32); });
    p.add_entry("Scale", [this](){ ImGui::SliderFloat("##", &m_make_mesh_config.object_scale,   0.2f, 2.0f, "%.2f"); });
    p.add_entry("Gap",   [this](){ ImGui::SliderFloat("##", &m_make_mesh_config.instance_gap,   0.0f, 1.0f, "%.2f"); });
    p.pop_group();
    p.show_entries("operations");

    if (scenes_open) {
        ImGui::Indent(20.0f);
        ERHE_DEFER( ImGui::Unindent(20.0f); );
        const auto button_size = ImVec2{ImGui::GetContentRegionAvail().x, 0.0f};
#if 0
        if (erhe::imgui::make_button("Cubes", erhe::imgui::Item_mode::normal, button_size)) {
            m_context.scene_builder->add_cubes(
                glm::ivec3{m_make_mesh_config.instance_count, m_make_mesh_config.instance_count, m_make_mesh_config.instance_count},
                m_make_mesh_config.object_scale,
                m_make_mesh_config.instance_gap
            );
        }
#endif
        if (erhe::imgui::make_button("Platonic Solids", erhe::imgui::Item_mode::normal, button_size)) {
            Add_platonic_solids_command& cmd = m_context.scene_commands->get_add_platonic_solids_command();
            cmd.set_make_mesh_config(m_make_mesh_config);
            cmd.try_call();
        }
        if (erhe::imgui::make_button("Johnson Solids", erhe::imgui::Item_mode::normal, button_size)) {
            Add_johnson_solids_command& cmd = m_context.scene_commands->get_add_johnson_solids_command();
            cmd.set_make_mesh_config(m_make_mesh_config);
            cmd.try_call();
        }
        if (erhe::imgui::make_button("Curved Shapes", erhe::imgui::Item_mode::normal, button_size)) {
            Add_curved_shapes_command& cmd = m_context.scene_commands->get_add_curved_shapes_command();
            cmd.set_make_mesh_config(m_make_mesh_config);
            cmd.try_call();
        }
        if (erhe::imgui::make_button("Chain", erhe::imgui::Item_mode::normal, button_size)) {
            Add_chain_command& cmd = m_context.scene_commands->get_add_chain_command();
            cmd.set_make_mesh_config(m_make_mesh_config);
            cmd.try_call();
        }
        if (erhe::imgui::make_button("Toruses", erhe::imgui::Item_mode::normal, button_size)) {
            Add_toruses_command& cmd = m_context.scene_commands->get_add_toruses_command();
            cmd.set_make_mesh_config(m_make_mesh_config);
            cmd.try_call();
        }
    }

    ImGui::Separator();

    Operation_stack& operation_stack = *m_context.operation_stack;
    Selection&       selection       = *m_context.selection;

    const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = selection.get_selected_items();

    //// for (unsigned int i = 0; i < static_cast<unsigned int>(m_active_tools.size()); ++i) {
    ////     auto* tool = m_active_tools.at(i);
    ////     const bool button_pressed = erhe::imgui::make_button(
    ////         tool->description(),
    ////         tool->is_active()
    ////             ? erhe::imgui::Item_mode::active
    ////             : erhe::imgui::Item_mode::normal,
    ////         button_size
    ////     );
    ////     if (button_pressed) {
    ////         if (get_active_tool() != tool) {
    ////             set_active_tool(tool);
    ////         } else {
    ////             set_active_tool(nullptr);
    ////         }
    ////     }
    //// }

    const auto button_size = ImVec2{ImGui::GetContentRegionAvail().x, 0.0f};
    const auto undo_mode = operation_stack.can_undo() ? erhe::imgui::Item_mode::normal : erhe::imgui::Item_mode::disabled;
    if (erhe::imgui::make_button("Undo", undo_mode, button_size)) {
        operation_stack.undo();
    }

    const auto redo_mode = operation_stack.can_redo() ? erhe::imgui::Item_mode::normal : erhe::imgui::Item_mode::disabled;
    if (erhe::imgui::make_button("Redo", redo_mode, button_size)) {
        operation_stack.redo();
    }

    const auto selected_mesh_count = count_selected_meshes();
    const auto selected_node_count = count<erhe::scene::Node>(selected_items);
    const auto multi_select_meshes = (selected_mesh_count >= 2) ? erhe::imgui::Item_mode::normal : erhe::imgui::Item_mode::disabled;
    const auto multi_select_nodes  = (selected_node_count >= 2) ? erhe::imgui::Item_mode::normal : erhe::imgui::Item_mode::disabled;
    const auto delete_mode         = (selected_mesh_count + selected_node_count) > 0 ? erhe::imgui::Item_mode::normal : erhe::imgui::Item_mode::disabled;
    if (erhe::imgui::make_button("Delete", delete_mode, button_size)) {
        m_context.selection->delete_selection();
    }

    if (erhe::imgui::make_button("Attach", multi_select_nodes, button_size)) {
        const auto& node0 = get<erhe::scene::Node>(selected_items, 0);
        const auto& node1 = get<erhe::scene::Node>(selected_items, 1);
        if (node0 && node1) {
            operation_stack.queue(
                std::make_shared<Item_parent_change_operation>(
                    node1,
                    node0,
                    std::shared_ptr<erhe::scene::Node>{},
                    std::shared_ptr<erhe::scene::Node>{}
                )
            );
        }
    }

    const auto has_selection_mode = (selected_mesh_count >= 1) ? erhe::imgui::Item_mode::normal : erhe::imgui::Item_mode::disabled;

    //if (make_button("Unparent", has_selection_mode, button_size)) {
    //    const auto& node0 = selection.get<erhe::scene::Node>();
    //    if (node0) {
    //        m_context.operation_stack->queue(
    //            std::make_shared<Item_parent_change_operation>(
    //                std::shared_ptr<erhe::scene::Node>{},
    //                node0,
    //                std::shared_ptr<erhe::scene::Node>{},
    //                std::shared_ptr<erhe::scene::Node>{}
    //            )
    //        );
    //    }
    //}

    if (make_button("Export glTF", erhe::imgui::Item_mode::normal, button_size)) {
        export_gltf();
    }

    if (make_button("Merge", multi_select_meshes, button_size)) {
        merge();
    }

    const auto align_mode = can_align() ? erhe::imgui::Item_mode::normal : erhe::imgui::Item_mode::disabled;
    if (make_button("Align", align_mode, button_size)) {
        align_selection(false);
    }
    if (make_button("Align with Scale", align_mode, button_size)) {
        align_selection(true);
    }

    if (make_button("Catmull-Clark", has_selection_mode, button_size)) {
        catmull_clark();
    }
    if (make_button("Sqrt3", has_selection_mode, button_size)) {
        sqrt3();
    }
    if (make_button("Triangulate", has_selection_mode, button_size)) {
        triangulate();
    }
    if (make_button("Join", has_selection_mode, button_size)) {
        join();
    }
    ImGui::PushID("Kis");
    ImGui::PushID("height");
    ImGui::SliderFloat("##", &m_kis_height, -1.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Kis: Apex height along face normal (0 = flat)"); }
    ImGui::PopID();
    if (make_button("Kis", has_selection_mode, button_size)) {
        kis();
    }
    ImGui::PopID();
    if (make_button("Meta", has_selection_mode, button_size)) {
        meta();
    }
    if (make_button("Ortho", has_selection_mode, button_size)) {
        ortho();
    }
    ImGui::PushID("Gyro");
    ImGui::PushID("ratio");
    ImGui::SliderFloat("##", &m_gyro_ratio, 0.01f, 0.49f, "%.2f");
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Gyro: Edge split position (1/3 = standard gyro)"); }
    ImGui::PopID();
    if (make_button("Gyro", has_selection_mode, button_size)) {
        gyro();
    }
    ImGui::PopID();
    ImGui::PushID("Chamfer");
    ImGui::PushID("bevel");
    ImGui::SliderFloat("##", &m_bevel_ratio, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Chamfer: Bevel ratio (how much each face shrinks)"); }
    ImGui::PopID();
    if (make_button("Chamfer", has_selection_mode, button_size)) {
        chamfer3();
    }
    ImGui::PopID();
    if (make_button("Dual", has_selection_mode, button_size)) {
        dual();
    }
    if (make_button("Ambo", has_selection_mode, button_size)) {
        ambo();
    }
    ImGui::PushID("Truncate");
    ImGui::PushID("ratio");
    ImGui::SliderFloat("##", &m_truncate_ratio, 0.01f, 0.5f, "%.2f");
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Truncate: Cut depth (1/3 = standard, 0.5 = ambo)"); }
    ImGui::PopID();
    if (make_button("Truncate", has_selection_mode, button_size)) {
        truncate();
    }
    ImGui::PopID();
    if (make_button("Normalize", has_selection_mode, button_size)) {
        normalize();
    }
    if (make_button("Reverse", has_selection_mode, button_size)) {
        reverse();
    }
    if (make_button("Repair", has_selection_mode, button_size)) {
        repair();
    }
    if (make_button("Weld", has_selection_mode, button_size)) {
        weld();
    }
    ImGui::PushID("Remesh");
    ImGui::Checkbox("Regenerate Normals/UVs", &m_remesh_regenerate_attributes);
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("When on, process() recomputes smooth normals and texture coordinates.\nWhen off, Smooth keeps the original UVs/attributes; Isotropic/Anisotropic Remesh and Decimate produce none (new topology)."); }
    ImGui::PushID("target");
    ImGui::SliderInt("##", &m_remesh_target_vertex_count, 50, 100000, "%d", ImGuiSliderFlags_Logarithmic);
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Remesh: target vertex count"); }
    ImGui::PopID();
    if (make_button("Isotropic Remesh", has_selection_mode, button_size)) {
        remesh();
    }
    ImGui::PushID("anisotropy");
    ImGui::SliderFloat("##", &m_anisotropy, 0.0f, 0.5f, "%.3f");
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Anisotropic Remesh: anisotropy strength (uses the target vertex count above)"); }
    ImGui::PopID();
    if (make_button("Anisotropic Remesh", has_selection_mode, button_size)) {
        anisotropic_remesh();
    }
    ImGui::PopID();
    ImGui::PushID("Decimate");
    ImGui::PushID("bins");
    ImGui::SliderInt("##", &m_decimate_bins, 4, 512, "%d");
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Decimate: clustering grid resolution (higher = more detail kept)"); }
    ImGui::PopID();
    if (make_button("Decimate", has_selection_mode, button_size)) {
        decimate();
    }
    ImGui::PopID();
    ImGui::PushID("Smooth");
    ImGui::PushID("iterations");
    ImGui::SliderInt("##", &m_smooth_iterations, 1, 50, "%d");
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Smooth: number of Taubin lambda/mu cycles"); }
    ImGui::PopID();
    ImGui::PushID("strength");
    ImGui::SliderFloat("##", &m_smooth_strength, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Smooth: per-step Laplacian strength (lambda)"); }
    ImGui::PopID();
    if (make_button("Smooth", has_selection_mode, button_size)) {
        smooth();
    }
    ImGui::PopID();
    ImGui::PushID("Atlas");
    {
        const char* const texcoord_slot_items[] = { "Texcoord 0", "Texcoord 1" };
        const char* const parameterizer_items[] = { "Projection", "LSCM", "Spectral LSCM", "ABF" };
        const char* const packer_items[]        = { "None", "Tetris", "XAtlas" };
        ImGui::PushID("slot");
        ImGui::Combo("##", &m_atlas_texcoord_slot, texcoord_slot_items, IM_ARRAYSIZE(texcoord_slot_items));
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Make Atlas: target texture coordinate channel (overwritten)"); }
        ImGui::PopID();
        ImGui::PushID("hard_angles");
        ImGui::SliderFloat("##", &m_atlas_hard_angles_deg, 0.0f, 180.0f, "%.0f deg");
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Make Atlas: dihedral angle above which an edge becomes a chart boundary"); }
        ImGui::PopID();
        ImGui::PushID("param");
        ImGui::Combo("##", &m_atlas_parameterizer, parameterizer_items, IM_ARRAYSIZE(parameterizer_items));
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Make Atlas: per-chart parameterizer (ABF = best quality)"); }
        ImGui::PopID();
        ImGui::PushID("pack");
        ImGui::Combo("##", &m_atlas_packer, packer_items, IM_ARRAYSIZE(packer_items));
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Make Atlas: chart packing in texture space (XAtlas = best)"); }
        ImGui::PopID();
    }
    if (make_button("Generate Texture Coordinates", has_selection_mode, button_size)) {
        make_atlas();
    }
    ImGui::PopID();
    if (make_button("Gen Tangents", has_selection_mode, button_size)) {
        generate_tangents();
    }
    if (make_button("Make Geometry", has_selection_mode, button_size)) {
        make_geometry();
    }
    if (make_button("Make Raytrace", has_selection_mode, button_size)) {
        make_raytrace();
    }
    if (make_button("Bake Transform", has_selection_mode, button_size)) {
        bake_transform();
    }
    if (make_button("Center Transform", has_selection_mode, button_size)) {
        center_transform();
    }
    if (make_button("Create Brush", has_selection_mode, button_size)) {
        create_brush();
    }
    //// if (make_button("GUI Quad", erhe::imgui::Item_mode::normal, button_size)) {
    ////     Scene_builder* scene_builder = get<Scene_builder>().get();
    ////
    ////     m_imgui_renderer->at_end_of_frame(
    ////         [scene_builder](){
    ////             scene_builder->add_rendertarget_viewports(2);
    ////         }
    ////     );
    //// }
}

void Operations::merge()
{
    /// tf::Executor& executor = m_context.operation_stack->get_executor();
    /// executor.silent_async(
    ///     [this]()
    ///     {
            m_context.operation_stack->queue(
                std::make_shared<Merge_operation>(
                    Merge_operation::Parameters{
                        .context = m_context,
                        .build_info{
                            .primitive_types{
                                .fill_triangles  = true,
                                .edge_lines      = true,
                                .corner_points   = true,
                                .centroid_points = true
                            },
                            .buffer_info = m_context.mesh_memory->make_primitive_buffer_info()
                        }
                    }
                )
            );
    ///     }
    /// );
}

auto Operations::can_align() const -> bool
{
    const Mesh_component_selection* selection = m_context.mesh_component_selection;
    if (selection == nullptr) {
        return false;
    }
    const Mesh_component_mode mode = selection->get_mode();
    if (mode == Mesh_component_mode::object) {
        return false;
    }

    // Allocation-free structural check: count selected components of the active
    // mode across live entries and track up to two distinct owning nodes. Each
    // entry's components belong to the same node, so two components on one node
    // (one entry with two, or two primitives of one node) leaves node_b null and
    // is rejected.
    std::size_t              count  = 0;
    const erhe::scene::Node* node_a = nullptr;
    const erhe::scene::Node* node_b = nullptr;
    for (const Mesh_component_entry& entry : selection->get_entries()) {
        if (!selection->is_live(entry)) {
            continue;
        }
        std::size_t n = 0;
        switch (mode) {
            case Mesh_component_mode::vertex: n = entry.vertices.size(); break;
            case Mesh_component_mode::edge:   n = entry.edges.size();    break;
            case Mesh_component_mode::face:   n = entry.facets.size();   break;
            default:                          n = 0;                     break;
        }
        if (n == 0) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Mesh> mesh = entry.mesh.lock();
        if (!mesh) {
            continue;
        }
        const erhe::scene::Node* const node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        count += n;
        if (count > 2) {
            return false;
        }
        if (node_a == nullptr) {
            node_a = node;
        } else if ((node != node_a) && (node_b == nullptr)) {
            node_b = node;
        }
    }
    return (count == 2) && (node_a != nullptr) && (node_b != nullptr);
}

auto Operations::align_selection(const bool apply_scale) -> bool
{
    Mesh_component_selection* selection = m_context.mesh_component_selection;
    if (selection == nullptr) {
        return false;
    }
    const Mesh_component_mode mode = selection->get_mode();
    if (mode == Mesh_component_mode::object) {
        return false;
    }

    std::vector<Selected_component> components;
    gather_components(*selection, mode, components);
    if (components.size() != 2) {
        log_operations->info("Align: requires exactly two selected {} components (have {})", c_str(mode), components.size());
        return false;
    }

    const Selected_component& anchor = components[0]; // first-selected component stays put
    const Selected_component& moved  = components[1]; // its node is transformed to align onto the anchor
    if (!anchor.node || !moved.node) {
        return false;
    }
    if (anchor.node == moved.node) {
        log_operations->info("Align: both components are on the same node; nothing to align");
        return false;
    }

    switch (mode) {
        case Mesh_component_mode::vertex: {
            // Translate-only: colocate the two vertex positions (a vertex has no
            // inherent size or direction, so apply_scale has no effect here).
            const glm::mat4 world_delta = glm::translate(glm::mat4{1.0f}, anchor.world_position - moved.world_position);
            return queue_align(m_context, moved.node, world_delta);
        }

        case Mesh_component_mode::edge: {
            if ((glm::length(anchor.world_direction) < 0.5f) || (glm::length(moved.world_direction) < 0.5f)) {
                return false; // degenerate (zero-length) edge
            }
            // Choose same vs opposite edge direction by least rotation (nearer of
            // +-dir_a). Roll about the edge axis is left free (shortest-arc).
            const glm::vec3 dir_a    = anchor.world_direction;
            const glm::vec3 dir_b    = moved.world_direction;
            const glm::vec3 target   = (glm::dot(dir_b, dir_a) >= 0.0f) ? dir_a : -dir_a;
            const glm::quat rotation = shortest_arc(dir_b, target);

            // Optional uniform scale so the moved edge matches the anchor edge length.
            const float     scale     = (apply_scale && (moved.world_size > 1e-6f)) ? (anchor.world_size / moved.world_size) : 1.0f;
            const glm::mat4 scale_mat = glm::scale(glm::mat4{1.0f}, glm::vec3{scale});

            // Scale and rotate about the moved edge's midpoint, then translate that
            // midpoint onto the anchor midpoint.
            const glm::mat4 to_pivot    = glm::translate(glm::mat4{1.0f}, -moved.world_position);
            const glm::mat4 rotate      = glm::mat4_cast(rotation);
            const glm::mat4 from_anchor = glm::translate(glm::mat4{1.0f}, anchor.world_position);
            const glm::mat4 world_delta = from_anchor * rotate * scale_mat * to_pivot;
            return queue_align(m_context, moved.node, world_delta);
        }

        case Mesh_component_mode::face: {
            // Full-frame, faces meeting each other: align the moved face's frame
            // (normal flipped via Frame_orientation::in) onto the anchor face's
            // frame (Frame_orientation::out), so centroids coincide, tangents align
            // and the two outward normals end up antiparallel (glued face-to-face).
            const Face_frame anchor_frame = facet_world_frame(anchor, Frame_orientation::out);
            const Face_frame moved_frame  = facet_world_frame(moved,  Frame_orientation::in);

            // Optional uniform scale so the moved face matches the anchor face size.
            // The frame bases are orthonormal, so scaling about the (frame-local)
            // centroid resizes the moved object without otherwise disturbing the fit.
            const float     scale     = (apply_scale && (moved_frame.scale > 1e-6f)) ? (anchor_frame.scale / moved_frame.scale) : 1.0f;
            const glm::mat4 scale_mat = glm::scale(glm::mat4{1.0f}, glm::vec3{scale});
            const glm::mat4 world_delta = anchor_frame.basis * scale_mat * glm::inverse(moved_frame.basis);
            return queue_align(m_context, moved.node, world_delta);
        }

        case Mesh_component_mode::object:
        default: {
            return false;
        }
    }
}

void Operations::triangulate()
{
    async_mesh_operation<Triangulate_operation>();
}

void Operations::normalize()
{
    async_mesh_operation<Normalize_operation>();
}

void Operations::bake_transform()
{
    /// tf::Executor& executor = m_context.operation_stack->get_executor();
    /// executor.silent_async(
    ///     [this]()
    ///     {
            Compound_operation::Parameters compound_operation_parameters;
            // First: Transform geometries using node transforms
            compound_operation_parameters.operations.push_back(
                std::make_shared<Bake_transform_operation>(mesh_context())
            );
            // Second: Reset transform in all nodes
            const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = m_context.selection->get_selected_items();
            const std::vector<std::shared_ptr<erhe::scene::Node>>& nodes = get_all<erhe::scene::Node>(selected_items);
            for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
                compound_operation_parameters.operations.push_back(
                    std::make_shared<Node_transform_operation>(
                        Node_transform_operation::Parameters{
                            .node = node,
                            .parent_from_node_before = node->parent_from_node_transform(),
                            .parent_from_node_after = {}
                        }
                    )
                );
            }
            m_context.operation_stack->queue(
                std::make_shared<Compound_operation>(std::move(compound_operation_parameters))
            );
    ///    }
    /// );
}

void Operations::center_transform()
{
    /// tf::Executor& executor = m_context.operation_stack->get_executor();
    /// executor.silent_async(
    ///     [this]()
    ///     {
            Compound_operation::Parameters compound_operation_parameters;

            // First: Transform geometries using node transforms
            Mesh_operation_parameters parameters = mesh_context();
            //std::unordered_map<uint64_t, glm::mat4> mesh_transform;
            parameters.make_entry_node_callback = [](erhe::scene::Node* node, Mesh_operation_parameters& parameters) {
                const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node);
                if (!mesh) {
                    return;
                }
                const erhe::math::Aabb aabb_world  = mesh->get_aabb_world();
                const glm::vec3        aabb_center = aabb_world.center();
                parameters.transform = erhe::math::create_translation<float>(-aabb_center);
            };
            compound_operation_parameters.operations.push_back(
                std::make_shared<Bake_transform_operation>(std::move(parameters))
            );
            // Second: Reset transform in all nodes
            const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = m_context.selection->get_selected_items();
            const std::vector<std::shared_ptr<erhe::scene::Node>>& nodes = get_all<erhe::scene::Node>(selected_items);
            for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
                const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
                if (!mesh) {
                    return;
                }
                const erhe::math::Aabb aabb_world       = mesh->get_aabb_world();
                const glm::vec3        aabb_center      = aabb_world.center();
                const glm::mat4        offset_transform = erhe::math::create_translation<float>(aabb_center);

                const glm::mat4 world_from_node_before = node->world_from_node();
                const glm::mat4 world_from_node_after  = offset_transform * node->world_from_node();
                compound_operation_parameters.operations.push_back(
                    std::make_shared<Node_transform_operation>(
                        Node_transform_operation::Parameters{
                            .node                    = node,
                            .parent_from_node_before = node->parent_from_node_transform(),
                            .parent_from_node_after  = erhe::scene::Transform{node->parent_from_world() * world_from_node_after}
                        }
                    )
                );
            }
            m_context.operation_stack->queue(
                std::make_shared<Compound_operation>(std::move(compound_operation_parameters))
            );
    ///    }
    /// );
}

void Operations::reverse()
{
    async_mesh_operation<Reverse_operation>();
}

void Operations::repair()
{
    async_mesh_operation<Repair_operation>();
}

void Operations::weld()
{
    async_mesh_operation<Weld_operation>();
}

void Operations::remesh()
{
    const unsigned int target_point_count    = static_cast<unsigned int>(m_remesh_target_vertex_count);
    const bool         regenerate_attributes = m_remesh_regenerate_attributes;
    async_for_selected_nodes_with_mesh(
        [this, target_point_count, regenerate_attributes](Mesh_operation_parameters&& params) {
            m_context.operation_stack->queue(
                std::make_shared<Remesh_operation>(std::move(params), target_point_count, regenerate_attributes)
            );
        }
    );
}

void Operations::anisotropic_remesh()
{
    const unsigned int target_point_count    = static_cast<unsigned int>(m_remesh_target_vertex_count);
    const float        anisotropy            = m_anisotropy;
    const bool         regenerate_attributes = m_remesh_regenerate_attributes;
    async_for_selected_nodes_with_mesh(
        [this, target_point_count, anisotropy, regenerate_attributes](Mesh_operation_parameters&& params) {
            m_context.operation_stack->queue(
                std::make_shared<Anisotropic_remesh_operation>(std::move(params), target_point_count, anisotropy, regenerate_attributes)
            );
        }
    );
}

void Operations::decimate()
{
    const unsigned int nb_bins               = static_cast<unsigned int>(m_decimate_bins);
    const bool         regenerate_attributes = m_remesh_regenerate_attributes;
    async_for_selected_nodes_with_mesh(
        [this, nb_bins, regenerate_attributes](Mesh_operation_parameters&& params) {
            m_context.operation_stack->queue(
                std::make_shared<Decimate_operation>(std::move(params), nb_bins, regenerate_attributes)
            );
        }
    );
}

void Operations::smooth()
{
    const unsigned int iterations            = static_cast<unsigned int>(m_smooth_iterations);
    const float        strength              = m_smooth_strength;
    const bool         regenerate_attributes = m_remesh_regenerate_attributes;
    async_for_selected_nodes_with_mesh(
        [this, iterations, strength, regenerate_attributes](Mesh_operation_parameters&& params) {
            m_context.operation_stack->queue(
                std::make_shared<Smooth_operation>(std::move(params), iterations, strength, regenerate_attributes)
            );
        }
    );
}

void Operations::make_atlas()
{
    const std::size_t usage_index           = static_cast<std::size_t>(m_atlas_texcoord_slot);
    const float       hard_angles_threshold = m_atlas_hard_angles_deg;
    const auto        parameterizer         = static_cast<erhe::geometry::operation::Atlas_parameterizer>(m_atlas_parameterizer);
    const auto        packer                = static_cast<erhe::geometry::operation::Atlas_packer>(m_atlas_packer);
    async_for_selected_nodes_with_mesh(
        [this, usage_index, hard_angles_threshold, parameterizer, packer](Mesh_operation_parameters&& params) {
            m_context.operation_stack->queue(
                std::make_shared<Make_atlas_operation>(std::move(params), usage_index, hard_angles_threshold, parameterizer, packer)
            );
        }
    );
}

void Operations::generate_tangents()
{
    async_mesh_operation<Generate_tangents_operation>();
}

void Operations::make_geometry()
{
    async_for_selected_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            Compound_operation::Parameters compound_parameters;
            for (const std::shared_ptr<erhe::Item_base>& item : mesh_operation_parameters.items) {
                std::shared_ptr<erhe::scene::Mesh> scene_mesh = erhe::scene::get_mesh(item);
                ERHE_VERIFY(scene_mesh);

                erhe::scene::Node*                              node              = scene_mesh->get_node();
                std::shared_ptr<Node_physics>                   node_physics      = erhe::scene::get_attachment<Node_physics>(node);
                const std::vector<erhe::scene::Mesh_primitive>& primitives_before = scene_mesh->get_primitives();
                std::vector<erhe::scene::Mesh_primitive>        primitives_after  = primitives_before;

                for (erhe::scene::Mesh_primitive& mesh_primitive : primitives_after) {
                    erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
                    const bool geometry_ok = primitive.make_geometry();
                    if (!geometry_ok) {
                        continue;
                    }
                }

                Mesh_operation_parameters parameters = mesh_context();
                parameters.items.push_back(item);
                std::shared_ptr<Mesh_operation> mesh_operation = std::make_shared<Mesh_operation>(std::move(parameters));
                mesh_operation->add_entry(
                    Mesh_operation::Entry{
                        .scene_mesh = scene_mesh,
                        .before = {
                            .node_physics = node_physics,
                            .primitives   = primitives_before
                        },
                        .after = {
                            .node_physics = node_physics,
                            .primitives   = primitives_after
                        }
                    }
                );
                compound_parameters.operations.push_back(std::move(mesh_operation));
            }

            if (!compound_parameters.operations.empty()) {
                m_context.operation_stack->queue(
                    std::make_shared<Compound_operation>(std::move(compound_parameters))
                );
            }
        }
    );
}

void Operations::make_raytrace()
{
    async_for_selected_nodes_with_mesh(
        [](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            for (const std::shared_ptr<erhe::Item_base>& item : mesh_operation_parameters.items) {
                std::shared_ptr<erhe::scene::Mesh> scene_mesh = erhe::scene::get_mesh(item);
                ERHE_VERIFY(scene_mesh);
                erhe::Item_host* item_host = item->get_item_host();
                ERHE_VERIFY(item_host != nullptr);
                Scene_root* scene_root = static_cast<Scene_root*>(item_host);
                std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};

                scene_root->begin_mesh_rt_update(scene_mesh);
                std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = scene_mesh->get_mutable_primitives();
                for (erhe::scene::Mesh_primitive& mesh_primitive : mesh_primitives) {
                    erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
                    // Ensure raytrace exists
                    ERHE_VERIFY(primitive.make_raytrace());
                }
                scene_mesh->update_rt_primitives();
                scene_root->end_mesh_rt_update(scene_mesh);
            }
        }
    );
}

void Operations::difference()
{
    async_mesh_operation<Difference_operation>();
}

void Operations::intersection()
{
    async_mesh_operation<Intersection_operation>();
}

void Operations::union_()
{
    async_mesh_operation<Union_operation>();
}

void Operations::catmull_clark()
{
    async_mesh_operation<Catmull_clark_subdivision_operation>();
}

void Operations::sqrt3()
{
    async_mesh_operation<Sqrt3_subdivision_operation>();
}

void Operations::dual()
{
    async_mesh_operation<Dual_operation>();
}

void Operations::join()
{
    async_mesh_operation<Join_operation>();
}

void Operations::kis()
{
    const float height = m_kis_height;
    async_for_selected_nodes_with_mesh(
        [this, height](Mesh_operation_parameters&& params) {
            m_context.operation_stack->queue(
                std::make_shared<Kis_operation>(std::move(params), height)
            );
        }
    );
}

void Operations::meta()
{
    async_mesh_operation<Meta_operation>();
}

void Operations::ortho()
{
    async_mesh_operation<Subdivide_operation>();
}

void Operations::ambo()
{
    async_mesh_operation<Ambo_operation>();
}

void Operations::truncate()
{
    const float ratio = m_truncate_ratio;
    async_for_selected_nodes_with_mesh(
        [this, ratio](Mesh_operation_parameters&& params) {
            m_context.operation_stack->queue(
                std::make_shared<Truncate_operation>(std::move(params), ratio)
            );
        }
    );
}

void Operations::gyro()
{
    const float ratio = m_gyro_ratio;
    async_for_selected_nodes_with_mesh(
        [this, ratio](Mesh_operation_parameters&& params) {
            m_context.operation_stack->queue(
                std::make_shared<Gyro_operation>(std::move(params), ratio)
            );
        }
    );
}

void Operations::chamfer3()
{
    const float ratio = m_bevel_ratio;
    async_for_selected_nodes_with_mesh(
        [this, ratio](Mesh_operation_parameters&& params) {
            m_context.operation_stack->queue(
                std::make_shared<Chamfer3_operation>(std::move(params), ratio)
            );
        }
    );
}

auto Operations::get_target_scene_root() -> std::shared_ptr<Scene_root>
{
    Scene_view* scene_view = (m_last_hover_scene_view != nullptr) ? m_last_hover_scene_view : m_hover_scene_view;
    if (scene_view != nullptr) {
        std::shared_ptr<Scene_root> scene_root = scene_view->get_scene_root();
        if (scene_root) {
            return scene_root;
        }
    }
    // No viewport hovered yet: when exactly one scene exists, use it.
    return m_context.app_scenes->get_single_scene_root();
}

#if defined(ERHE_WINDOW_LIBRARY_SDL)
static void s_export_callback(void* userdata, const char* const* filelist, int filter)
{
    Operations* operations = static_cast<Operations*>(userdata);
    operations->export_callback(filelist, filter);
}
#endif

void Operations::export_callback(const char* const* filelist, int filter)
{
    static_cast<void>(filter);
    if (filelist == nullptr) {
        // error
        return;
    }
    const char* const file = *filelist;
    if (file == nullptr) {
        // nothing chosen / canceled
        return;
    }
    //std::optional<std::filesystem::path> path = erhe::file::select_file_for_write();
    std::optional<std::filesystem::path> path = std::filesystem::path{file};

    std::shared_ptr<Scene_root> scene_root = get_target_scene_root();
    if (!scene_root) {
        return;
    }

    const erhe::scene::Scene& scene = scene_root->get_scene();
    std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    if (!root_node) {
        return;
    }

    if (path.has_value()) {
        const bool binary = true;
        const erhe::gltf::Gltf_physics_data physics_data = build_gltf_physics_data(scene);
        std::string gltf = erhe::gltf::export_gltf(*root_node.get(), binary, &physics_data);
        erhe::file::write_file(path.value(), gltf);
    }
}

void Operations::export_gltf()
{
#if defined(ERHE_WINDOW_LIBRARY_SDL)
    SDL_DialogFileFilter filters[3];
    filters[0].name    = "glTF files";
    filters[0].pattern = "glb;gltf";
    filters[1].name    = "All files";
    filters[1].pattern = "*";
    SDL_Window* window = static_cast<SDL_Window*>(m_context.context_window->get_sdl_window());
    SDL_ShowSaveFileDialog(s_export_callback, this, window, filters, 2, nullptr);
#elif defined(ERHE_OS_WINDOWS)
    try {
        std::optional<std::filesystem::path> path_opt = erhe::file::select_file_for_write();
        if (path_opt.has_value()) {
            std::string path = path_opt.value().string();
            const char* const filelist[2] = {
                path.data(),
                nullptr
            };
            int filter = 0;
            export_callback(filelist, filter);
        }
    } catch (...) {
        log_operations->error("exception: file dialog / glTF export");
    }
#else
    const char* const filelist[2] =
    {
        "erhe.glb",
        nullptr
    };
    int filter = 0;
    export_callback(filelist, filter);
#endif
}

void Operations::save_scene()
{
    std::shared_ptr<Scene_root> scene_root = get_target_scene_root();
    if (!scene_root) {
        return;
    }

    try {
        std::optional<std::filesystem::path> path_opt = erhe::file::select_file_for_write();
        if (path_opt.has_value()) {
            editor::save_scene(*scene_root, path_opt.value());
        }
    } catch (...) {
        log_operations->error("exception: file dialog / save scene");
    }
}

void Operations::load_scene()
{
    try {
        std::optional<std::filesystem::path> path_opt = erhe::file::select_file_for_read();
        if (!path_opt.has_value()) {
            return;
        }

        // Queue the load to happen outside ImGui iteration
        m_context.app_message_bus->load_scene_file.queue_message(
            Load_scene_file_message{
                .path = path_opt.value(),
            }
        );
    } catch (...) {
        log_operations->error("exception: file dialog / load scene");
    }
}

void Operations::create_material()
{
    std::shared_ptr<Scene_root> scene_root = get_target_scene_root();
    if (!scene_root) {
        return;
    }

    std::shared_ptr<Content_library>      content_library = scene_root->get_content_library();
    std::shared_ptr<Content_library_node> materials       = content_library->materials;

    std::shared_ptr<erhe::primitive::Material> new_material = std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{
            .name = "New Material",
            .data = {
                .base_color = glm::vec3{0.5f, 0.5f, 0.5f},
                .roughness  = glm::vec2{0.5f, 0.5f},
                .metallic   = 1.0f
            }
        }
    );
    std::shared_ptr<Content_library_node> new_content_library_node = std::make_shared<Content_library_node>(new_material);

    std::shared_ptr<Item_insert_remove_operation> make_material_operation = std::make_shared<Item_insert_remove_operation>(
        Item_insert_remove_operation::Parameters{
            .context = m_context,
            .item    = new_content_library_node,
            .parent  = materials,
            .mode    = Item_insert_remove_operation::Mode::insert
        }
    );

    m_context.operation_stack->queue(make_material_operation);
}

void Operations::create_physics_material()
{
    std::shared_ptr<Scene_root> scene_root = get_target_scene_root();
    if (!scene_root) {
        return;
    }

    std::shared_ptr<Content_library> content_library = scene_root->get_content_library();

    auto new_physics_material = std::make_shared<erhe::physics::Physics_material>("New Physics Material");
    std::shared_ptr<Content_library_node> new_content_library_node = std::make_shared<Content_library_node>(new_physics_material);

    m_context.operation_stack->queue(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = new_content_library_node,
                .parent  = content_library->physics_materials,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );
}

void Operations::create_collision_filter()
{
    std::shared_ptr<Scene_root> scene_root = get_target_scene_root();
    if (!scene_root) {
        return;
    }

    std::shared_ptr<Content_library> content_library = scene_root->get_content_library();

    auto new_collision_filter = std::make_shared<erhe::physics::Collision_filter>("New Collision Filter");
    std::shared_ptr<Content_library_node> new_content_library_node = std::make_shared<Content_library_node>(new_collision_filter);

    m_context.operation_stack->queue(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = new_content_library_node,
                .parent  = content_library->collision_filters,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );
}

void Operations::create_joint_settings()
{
    std::shared_ptr<Scene_root> scene_root = get_target_scene_root();
    if (!scene_root) {
        return;
    }

    std::shared_ptr<Content_library> content_library = scene_root->get_content_library();

    auto new_joint_settings = std::make_shared<erhe::physics::Physics_joint_settings>("New Joint Settings");
    std::shared_ptr<Content_library_node> new_content_library_node = std::make_shared<Content_library_node>(new_joint_settings);

    m_context.operation_stack->queue(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = new_content_library_node,
                .parent  = content_library->physics_joints,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );
}

void Operations::create_brush()
{
    // Find mesh: check directly selected meshes, then check selected nodes for mesh attachments
    std::shared_ptr<erhe::scene::Mesh> mesh = m_context.selection->get_last_selected<erhe::scene::Mesh>();
    if (!mesh) {
        const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = m_context.selection->get_selected_items();
        for (const std::shared_ptr<erhe::Item_base>& item : selected_items) {
            std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
            if (node) {
                mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
                if (mesh) {
                    break;
                }
            }
        }
    }
    if (!mesh) {
        return;
    }

    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    if (primitives.empty()) {
        return;
    }

    const erhe::scene::Mesh_primitive& first_primitive = primitives.front();
    if (!first_primitive.primitive || !first_primitive.primitive->render_shape) {
        return;
    }

    const std::shared_ptr<erhe::geometry::Geometry>& geometry = first_primitive.primitive->render_shape->get_geometry_const();
    if (!geometry) {
        return;
    }

    std::shared_ptr<Scene_root> scene_root = get_target_scene_root();
    if (!scene_root) {
        return;
    }

    std::shared_ptr<Content_library>      content_library = scene_root->get_content_library();
    std::shared_ptr<Content_library_node> brushes         = content_library->brushes;

    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{content_library->mutex};
        const erhe::primitive::Build_info brush_build_info{
            .primitive_types = {
                .fill_triangles  = true,
                .edge_lines      = true,
                .corner_points   = true,
                .centroid_points = true
            },
            .buffer_info = m_context.mesh_memory->make_primitive_buffer_info()
        };
        // Derive brush name from mesh, owner node, or geometry
        std::string brush_name;
        if (!mesh->get_name().empty()) {
            brush_name = mesh->get_name();
        } else {
            erhe::scene::Node* owner_node = mesh->get_node();
            if ((owner_node != nullptr) && !owner_node->get_name().empty()) {
                brush_name = owner_node->get_name();
            } else if (!geometry->get_name().empty()) {
                brush_name = geometry->get_name();
            } else {
                brush_name = "Brush";
            }
        }

        std::shared_ptr<Brush> new_brush = brushes->make<Brush>(
            Brush_data{
                .context      = m_context,
                .app_settings = *m_context.app_settings,
                .name         = brush_name,
                .build_info   = brush_build_info,
                .normal_style = erhe::primitive::Normal_style::polygon_normals,
                .geometry     = geometry
            }
        );
        if (first_primitive.material) {
            new_brush->set_material(first_primitive.material);
        }
    }
}

void Operations::prune_loaded_scene_windows()
{
    // Drop entries whose Scene_root has been released (closed scene).
    // Stable in-place erase to keep insertion order of the remaining
    // entries identical to load order.
    m_loaded_content_library_windows.erase(
        std::remove_if(
            m_loaded_content_library_windows.begin(),
            m_loaded_content_library_windows.end(),
            [](const Loaded_scene_window& entry) {
                return entry.scene_root.expired();
            }
        ),
        m_loaded_content_library_windows.end()
    );
}

}
