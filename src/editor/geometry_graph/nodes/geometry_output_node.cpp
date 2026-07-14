#include "geometry_graph/nodes/geometry_output_node.hpp"

#include "graph_editor/graph_editor_widgets.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "geometry_graph/geometry_graph.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <geogram/mesh/mesh.h>

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include <cfloat>

namespace editor {

namespace {

// Motion modes exposed on the output node, in stepper order. Serialized
// by name; maps to erhe::physics::Motion_mode e_static /
// e_kinematic_physical / e_dynamic.
constexpr const char* c_physics_motion_names[] = { "Static", "Kinematic", "Dynamic" };

[[nodiscard]] auto physics_motion_mode_to_string(erhe::physics::Motion_mode motion_mode) -> const char*
{
    switch (motion_mode) {
        case erhe::physics::Motion_mode::e_dynamic:             return "dynamic";
        case erhe::physics::Motion_mode::e_kinematic_physical:  return "kinematic";
        default:                                                return "static";
    }
}

[[nodiscard]] auto physics_motion_mode_from_string(const std::string& name, erhe::physics::Motion_mode fallback) -> erhe::physics::Motion_mode
{
    if (name == "dynamic")   { return erhe::physics::Motion_mode::e_dynamic; }
    if (name == "kinematic") { return erhe::physics::Motion_mode::e_kinematic_physical; }
    if (name == "static")    { return erhe::physics::Motion_mode::e_static; }
    return fallback;
}

}

Geometry_output_node::Geometry_output_node(App_context& context)
    : Geometry_graph_node{"Output"}
    , m_context          {context}
{
    make_input_pin(Geometry_pin_key::geometry, "in");
}

Geometry_output_node::~Geometry_output_node() noexcept
{
}

void Geometry_output_node::on_removed_from_graph()
{
    // An output leaving the graph must not strand its last bake on bound
    // attachments: publish an empty bake (attachments clear their mesh on
    // the next push).
    const std::shared_ptr<Graph_mesh> owning_graph_mesh = get_owning_graph_mesh();
    if (owning_graph_mesh) {
        owning_graph_mesh->set_baked_products(Graph_mesh_baked_products{});
        owning_graph_mesh->request_attachment_push();
    }
}

namespace {

// The first geometry output payload of a designated (display / ghost)
// node; null when the node has no geometry output or its payload is
// empty. Nodes place geometry on the pin keyed Geometry_pin_key::geometry
// (not necessarily slot 0).
[[nodiscard]] auto get_designated_geometry(Geometry_graph_node& node) -> std::shared_ptr<erhe::geometry::Geometry>
{
    const auto& pins = node.get_output_pins();
    for (std::size_t slot = 0, end = pins.size(); slot < end; ++slot) {
        if (pins[slot].get_key() == Geometry_pin_key::geometry) {
            return node.get_output(slot).get_geometry();
        }
    }
    return {};
}

// Copy + render-process a graph payload geometry (the payload is shared
// with upstream nodes and must not be mutated).
[[nodiscard]] auto prepare_render_geometry(const std::shared_ptr<erhe::geometry::Geometry>& source) -> std::shared_ptr<erhe::geometry::Geometry>
{
    std::shared_ptr<erhe::geometry::Geometry> render_geometry = std::make_shared<erhe::geometry::Geometry>(source->get_name());
    GEO::mat4f identity;
    identity.load_identity();
    render_geometry->copy_with_transform(*source.get(), identity);
    render_geometry->process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect                            |
                erhe::geometry::Geometry::process_flag_build_edges                        |
                erhe::geometry::Geometry::process_flag_compute_facet_centroids            |
                erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals      |
                erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates |
                erhe::geometry::Geometry::process_flag_generate_tangents
        }
    );
    return render_geometry;
}

} // anonymous namespace

auto Geometry_output_node::is_scene_output() const -> bool
{
    return true;
}

void Geometry_output_node::evaluate(Geometry_graph& graph)
{
    pull_inputs();

    // Houdini display flag: a designated display node's geometry replaces
    // the wired input - the whole bake (render mesh AND physics collision)
    // follows it, exactly as if that node were wired to this output. A
    // designated-but-empty payload bakes empty (the scene mesh clears); a
    // designation whose node no longer resolves falls back to the wired
    // input (tolerant resolution: undo of a node delete self-heals).
    std::shared_ptr<erhe::geometry::Geometry> source{};
    Geometry_graph_node* display_node = graph.find_node_by_log_id(graph.get_display_node_id());
    if ((display_node != nullptr) && !display_node->is_scene_output()) {
        source = get_designated_geometry(*display_node);
    } else {
        source = get_input(0).get_geometry();
    }

    m_evaluated_geometry.reset();
    m_evaluated_primitive.reset();
    m_evaluated_collision_shape.reset();
    m_evaluated_ghost_geometry.reset();
    m_evaluated_ghost_primitive.reset();
    m_evaluated_valid = true;

    const erhe::primitive::Build_info build_info{
        .primitive_types = {
            .fill_triangles          = true,
            .fill_triangles_expanded = true,
            .edge_lines              = true,
            .corner_points           = true,
            .centroid_points         = true
        },
        .buffer_info = m_context.mesh_memory->make_primitive_buffer_info()
    };

    // A facet-less geometry (e.g. an out-of-range Conway operation index,
    // a boolean of empty inputs) has nothing to render; treat it like a
    // disconnected input rather than asking the primitive builder to
    // build zero-index buffers (which it refuses with a VERIFY abort).
    // A valid-but-empty result makes apply_evaluated_to_scene() clear
    // the scene mesh. The ghost bake below still runs.
    if (source && (source->get_mesh().facets.nb() > 0)) {
        const std::shared_ptr<erhe::geometry::Geometry> render_geometry = prepare_render_geometry(source);

        std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(render_geometry);
        const bool renderable_ok = primitive->make_renderable_mesh(build_info, erhe::primitive::Normal_style::point_normals);
        if (!renderable_ok) {
            log_graph_editor->warn("Geometry_output_node: failed to build renderable mesh for '{}'", render_geometry->get_name());
            // Leave the scene mesh as it is rather than clearing it.
            m_evaluated_valid = false;
            return;
        }
        if (!primitive->make_raytrace()) {
            log_graph_editor->warn("Geometry_output_node: failed to build raytrace shape for '{}'", render_geometry->get_name());
        }

        m_evaluated_geometry  = render_geometry;
        m_evaluated_primitive = primitive;

        if (m_physics_enabled) {
            GEO::Mesh convex_hull{};
            const bool convex_hull_ok = erhe::geometry::make_convex_hull(render_geometry->get_mesh(), convex_hull);
            if (!convex_hull_ok) {
                log_graph_editor->warn("Geometry_output_node: failed to build convex hull collision shape for '{}'", render_geometry->get_name());
            } else {
                std::vector<float> coordinates;
                coordinates.resize(3 * convex_hull.vertices.nb());
                for (GEO::index_t vertex : convex_hull.vertices) {
                    const GEO::vec3f p = erhe::geometry::get_pointf(convex_hull.vertices, vertex);
                    coordinates[(3 * vertex) + 0] = p.x;
                    coordinates[(3 * vertex) + 1] = p.y;
                    coordinates[(3 * vertex) + 2] = p.z;
                }
                m_evaluated_collision_shape = erhe::physics::ICollision_shape::create_convex_hull_shape_shared(
                    coordinates.data(),
                    static_cast<int>(convex_hull.vertices.nb()),
                    static_cast<int>(3 * sizeof(float))
                );
            }
        }
    }

    // Houdini template flag: bake the ghost node's geometry as an
    // edge-lines-only companion. No raytrace (invisible to hover /
    // picking) and no physics; the consuming attachment renders it
    // through the dedicated ghost edge-lines pass (render_wireframe
    // flag). The fill primitive types are built too - the primitive
    // builder's fill path is its primary path - but no composition pass
    // draws them (the ghost mesh carries no `content` flag).
    Geometry_graph_node* ghost_node = graph.find_node_by_log_id(graph.get_ghost_node_id());
    if ((ghost_node != nullptr) && !ghost_node->is_scene_output()) {
        const std::shared_ptr<erhe::geometry::Geometry> ghost_source = get_designated_geometry(*ghost_node);
        if (ghost_source && (ghost_source->get_mesh().facets.nb() > 0)) {
            const std::shared_ptr<erhe::geometry::Geometry> ghost_geometry = prepare_render_geometry(ghost_source);
            std::shared_ptr<erhe::primitive::Primitive> ghost_primitive = std::make_shared<erhe::primitive::Primitive>(ghost_geometry);
            const bool ghost_ok = ghost_primitive->make_renderable_mesh(build_info, erhe::primitive::Normal_style::point_normals);
            if (ghost_ok) {
                m_evaluated_ghost_geometry  = ghost_geometry;
                m_evaluated_ghost_primitive = ghost_primitive;
            } else {
                log_graph_editor->warn("Geometry_output_node: failed to build ghost renderable mesh for '{}'", ghost_geometry->get_name());
            }
        }
    }
}

void Geometry_output_node::take_evaluated(Geometry_output_node& from)
{
    m_evaluated_valid           = from.m_evaluated_valid;
    m_evaluated_geometry        = std::move(from.m_evaluated_geometry);
    m_evaluated_primitive       = std::move(from.m_evaluated_primitive);
    m_evaluated_collision_shape = std::move(from.m_evaluated_collision_shape);
    m_evaluated_ghost_geometry  = std::move(from.m_evaluated_ghost_geometry);
    m_evaluated_ghost_primitive = std::move(from.m_evaluated_ghost_primitive);
    from.m_evaluated_valid = false;
}

void Geometry_output_node::apply_evaluated_to_scene()
{
    if (!m_evaluated_valid) {
        return;
    }
    m_evaluated_valid = false;

    // Publish the products to the owning Graph_mesh asset (bound
    // Geometry_graph_mesh attachments consume them; the evaluation engine
    // pushes right after this). The node never creates scene content
    // itself: an asset with no bound node renders nothing - exactly like
    // a Graph_texture no material samples. A null geometry publish tells
    // attachments to clear their mesh. Publishing needs no scene root
    // (attachments resolve a material fallback from their own scene);
    // m_material stays whatever the node's parameter selected, if any.
    // Graphs only live in the content library, so the owner is always
    // set for a node in a graph.
    const std::shared_ptr<Graph_mesh> owning_graph_mesh = get_owning_graph_mesh();
    if (owning_graph_mesh) {
        Graph_mesh_baked_products products;
        products.geometry            = std::move(m_evaluated_geometry);
        products.primitive           = std::move(m_evaluated_primitive);
        products.material            = m_material;
        products.collision_shape     = std::move(m_evaluated_collision_shape);
        products.physics_enabled     = m_physics_enabled;
        products.physics_motion_mode = m_physics_motion_mode;
        products.ghost_geometry      = std::move(m_evaluated_ghost_geometry);
        products.ghost_primitive     = std::move(m_evaluated_ghost_primitive);
        owning_graph_mesh->set_baked_products(products);
    }
    m_evaluated_geometry.reset();
    m_evaluated_primitive.reset();
    m_evaluated_collision_shape.reset();
    m_evaluated_ghost_geometry.reset();
    m_evaluated_ghost_primitive.reset();
}

void Geometry_output_node::imgui()
{
    // Product name; committed (and made undoable via the parameter edit
    // gesture) when the input defocuses, not on every keystroke.
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##output_name", &m_name);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        mark_dirty();
    }

    // Material selection, from the owning asset's scene content library
    // (the asset's Item_host is its owning scene; falls back to the single
    // scene root when the asset cannot say).
    std::shared_ptr<Scene_root> scene_root = get_hosting_scene_root(get_owning_graph_mesh().get());
    if (!scene_root) {
        scene_root = m_context.app_scenes->get_single_scene_root();
    }
    if (scene_root) {
        const std::shared_ptr<Content_library> library = scene_root->get_content_library();
        if (library && library->materials) {
            const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials = library->materials->get_all<erhe::primitive::Material>();
            if (!materials.empty()) {
                int material_index = 0;
                for (std::size_t i = 0, end = materials.size(); i < end; ++i) {
                    if (materials[i] == m_material) {
                        material_index = static_cast<int>(i);
                        break;
                    }
                }
                if (imgui_index_stepper("material", material_index, static_cast<int>(materials.size()))) {
                    m_material = materials.at(static_cast<std::size_t>(material_index));
                    mark_dirty();
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(m_material ? m_material->get_name().c_str() : "(no material)");
            }
        }
    }

    // Optional physics: convex hull collision shape + rigid body
    if (ImGui::Checkbox("Physics", &m_physics_enabled)) {
        mark_dirty();
    }
    if (m_physics_enabled) {
        ImGui::SameLine();
        int motion_index =
            (m_physics_motion_mode == erhe::physics::Motion_mode::e_dynamic            ) ? 2 :
            (m_physics_motion_mode == erhe::physics::Motion_mode::e_kinematic_physical ) ? 1 : 0;
        if (imgui_enum_stepper("physics_motion", motion_index, c_physics_motion_names, 3)) {
            m_physics_motion_mode =
                (motion_index == 2) ? erhe::physics::Motion_mode::e_dynamic :
                (motion_index == 1) ? erhe::physics::Motion_mode::e_kinematic_physical :
                                      erhe::physics::Motion_mode::e_static;
            mark_dirty();
        }
    }

    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_input(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    } else {
        ImGui::TextUnformatted("(no geometry)");
    }
}

void Geometry_output_node::write_parameters(nlohmann::json& out) const
{
    out["name"] = m_name;
    if (m_material) {
        out["material"] = m_material->get_name();
    }
    out["physics"] = m_physics_enabled;
    out["physics_motion"] = physics_motion_mode_to_string(m_physics_motion_mode);
}

void Geometry_output_node::read_parameters(const nlohmann::json& in)
{
    // Legacy files may carry a "scene" key (the removed direct-to-scene
    // output path targeted a scene); it is ignored - the consuming
    // Geometry_graph_mesh attachment decides where the bake lands.
    m_name = in.value("name", m_name);
    const std::string material_name = in.value("material", "");
    if (!material_name.empty()) {
        // See imgui(): the owning asset's scene first, single scene fallback.
        std::shared_ptr<Scene_root> scene_root = get_hosting_scene_root(get_owning_graph_mesh().get());
        if (!scene_root) {
            scene_root = m_context.app_scenes->get_single_scene_root();
        }
        const std::shared_ptr<Content_library> library = scene_root ? scene_root->get_content_library() : std::shared_ptr<Content_library>{};
        if (library && library->materials) {
            const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials = library->materials->get_all<erhe::primitive::Material>();
            for (const std::shared_ptr<erhe::primitive::Material>& material : materials) {
                if (material->get_name() == material_name) {
                    m_material = material;
                    break;
                }
            }
        }
    }
    m_physics_enabled = in.value("physics", m_physics_enabled);
    m_physics_motion_mode = physics_motion_mode_from_string(in.value("physics_motion", ""), m_physics_motion_mode);
    mark_dirty();
}

}
