#include "operations/geometry_operations.hpp"
#include "operations/item_insert_remove_operation.hpp"

#include "app_context.hpp"
#include "app_settings.hpp"
#include "renderers/lightmap_report.hpp"
#include "tools/selection_tool.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/bake_transform.hpp"
#include "erhe_geometry/operation/conway/ambo.hpp"
#include "erhe_geometry/operation/conway/chamfer3.hpp"
#include "erhe_geometry/operation/conway/dual.hpp"
#include "erhe_geometry/operation/conway/gyro.hpp"
#include "erhe_geometry/operation/conway/join.hpp"
#include "erhe_geometry/operation/conway/kis.hpp"
#include "erhe_geometry/operation/conway/meta.hpp"
#include "erhe_geometry/operation/conway/subdivide.hpp"
#include "erhe_geometry/operation/conway/truncate.hpp"
#include "erhe_geometry/operation/csg/difference.hpp"
#include "erhe_geometry/operation/csg/intersection.hpp"
#include "erhe_geometry/operation/csg/union.hpp"
#include "erhe_geometry/operation/generate_frame_field_tangents.hpp"
#include "erhe_geometry/operation/generate_tangents.hpp"
#include "erhe_geometry/operation/make_atlas.hpp"
#include "erhe_geometry/operation/merge_faces.hpp"
#include "erhe_geometry/operation/normalize.hpp"
#include "erhe_geometry/operation/remesh.hpp"
#include "erhe_geometry/operation/repair.hpp"
#include "erhe_geometry/operation/reverse.hpp"
#include "erhe_geometry/operation/subdivision/catmull_clark_subdivision.hpp"
#include "erhe_geometry/operation/subdivision/sqrt3_subdivision.hpp"
#include "erhe_geometry/operation/triangulate.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>

#include <cmath>
#include <limits>

using erhe::geometry::to_geo_mat4f;

namespace editor {

Catmull_clark_subdivision_operation::Catmull_clark_subdivision_operation(Mesh_operation_parameters&& context, const uint64_t post_process_flags)
    : Mesh_operation{std::move(context)}
{
    set_description("Catmull_clark");
    make_entries(
        [post_process_flags](
            const erhe::geometry::Geometry& before_geometry,
            erhe::geometry::Geometry&       after_geometry,
            erhe::scene::Node*              /*node*/,
            const std::set<GEO::index_t>*   selected_facets,
            const erhe::geometry::operation::Geometry_component_selection* remap_source,
            erhe::geometry::operation::Geometry_component_selection*       remap_destination
        ) -> void {
            erhe::geometry::operation::Component_remap remap{remap_source, remap_destination};
            erhe::geometry::operation::catmull_clark_subdivision(before_geometry, after_geometry, selected_facets, &remap, post_process_flags, post_process_flags);
        }
    );
    set_description(fmt::format("Catmull_clark {}", describe_entries()));
}

Sqrt3_subdivision_operation::Sqrt3_subdivision_operation(Mesh_operation_parameters&& context, const uint64_t post_process_flags)
    : Mesh_operation{std::move(context)}
{
    set_description("Sqrt3");
    make_entries(
        [post_process_flags](
            const erhe::geometry::Geometry& before_geometry,
            erhe::geometry::Geometry&       after_geometry,
            erhe::scene::Node*              /*node*/,
            const std::set<GEO::index_t>*   selected_facets,
            const erhe::geometry::operation::Geometry_component_selection* remap_source,
            erhe::geometry::operation::Geometry_component_selection*       remap_destination
        ) -> void {
            erhe::geometry::operation::Component_remap remap{remap_source, remap_destination};
            erhe::geometry::operation::sqrt3_subdivision(before_geometry, after_geometry, selected_facets, &remap, post_process_flags, post_process_flags);
        }
    );
    set_description(fmt::format("Sqrt3 {}", describe_entries()));
}

Triangulate_operation::Triangulate_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    set_description("Triangulate");
    make_entries(erhe::geometry::operation::triangulate);
    set_description(fmt::format("Triangulate {}", describe_entries()));
}

Join_operation::Join_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    set_description("Join");
    make_entries(
        [](
            const erhe::geometry::Geometry& before_geometry,
            erhe::geometry::Geometry&       after_geometry,
            erhe::scene::Node*              /*node*/,
            const std::set<GEO::index_t>*   selected_facets,
            const erhe::geometry::operation::Geometry_component_selection* remap_source,
            erhe::geometry::operation::Geometry_component_selection*       remap_destination
        ) -> void {
            erhe::geometry::operation::Component_remap remap{remap_source, remap_destination};
            erhe::geometry::operation::join(before_geometry, after_geometry, selected_facets, &remap);
        }
    );
    set_description(fmt::format("Join {}", describe_entries()));
}

Kis_operation::Kis_operation(Mesh_operation_parameters&& context, float height)
    : Mesh_operation{std::move(context)}
{
    set_description("Kis");
    make_entries(
        [height](
            const erhe::geometry::Geometry& before_geometry,
            erhe::geometry::Geometry&       after_geometry,
            erhe::scene::Node*              /*node*/,
            const std::set<GEO::index_t>*   selected_facets,
            const erhe::geometry::operation::Geometry_component_selection* remap_source,
            erhe::geometry::operation::Geometry_component_selection*       remap_destination
        ) -> void {
            erhe::geometry::operation::Component_remap remap{remap_source, remap_destination};
            erhe::geometry::operation::kis(before_geometry, after_geometry, height, selected_facets, &remap);
        }
    );
    set_description(fmt::format("Kis {}", describe_entries()));
}

Subdivide_operation::Subdivide_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    set_description("Subdivide");
    make_entries(
        [](
            const erhe::geometry::Geometry& before_geometry,
            erhe::geometry::Geometry&       after_geometry,
            erhe::scene::Node*              /*node*/,
            const std::set<GEO::index_t>*   selected_facets,
            const erhe::geometry::operation::Geometry_component_selection* remap_source,
            erhe::geometry::operation::Geometry_component_selection*       remap_destination
        ) -> void {
            erhe::geometry::operation::Component_remap remap{remap_source, remap_destination};
            erhe::geometry::operation::subdivide(before_geometry, after_geometry, selected_facets, &remap);
        }
    );
    set_description(fmt::format("Subdivide {}", describe_entries()));
}

Meta_operation::Meta_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    set_description("Meta");
    make_entries(
        [](
            const erhe::geometry::Geometry& before_geometry,
            erhe::geometry::Geometry&       after_geometry,
            erhe::scene::Node*              /*node*/,
            const std::set<GEO::index_t>*   selected_facets,
            const erhe::geometry::operation::Geometry_component_selection* remap_source,
            erhe::geometry::operation::Geometry_component_selection*       remap_destination
        ) -> void {
            erhe::geometry::operation::Component_remap remap{remap_source, remap_destination};
            erhe::geometry::operation::meta(before_geometry, after_geometry, selected_facets, &remap);
        }
    );
    set_description(fmt::format("Meta {}", describe_entries()));
}

Gyro_operation::Gyro_operation(Mesh_operation_parameters&& context, float ratio)
    : Mesh_operation{std::move(context)}
{
    set_description("Gyro");
    make_entries(
        [ratio](
            const erhe::geometry::Geometry& before_geometry,
            erhe::geometry::Geometry&       after_geometry,
            erhe::scene::Node*              /*node*/,
            const std::set<GEO::index_t>*   selected_facets,
            const erhe::geometry::operation::Geometry_component_selection* remap_source,
            erhe::geometry::operation::Geometry_component_selection*       remap_destination
        ) -> void {
            erhe::geometry::operation::Component_remap remap{remap_source, remap_destination};
            erhe::geometry::operation::gyro(before_geometry, after_geometry, ratio, selected_facets, &remap);
        }
    );
    set_description(fmt::format("Gyro {}", describe_entries()));
}

Chamfer3_operation::Chamfer3_operation(Mesh_operation_parameters&& context, float bevel_ratio)
    : Mesh_operation{std::move(context)}
{
    set_description("Chamfer3");
    make_entries(
        [bevel_ratio](
            const erhe::geometry::Geometry& before_geometry,
            erhe::geometry::Geometry&       after_geometry,
            erhe::scene::Node*              /*node*/,
            const std::set<GEO::index_t>*   selected_facets,
            const erhe::geometry::operation::Geometry_component_selection* remap_source,
            erhe::geometry::operation::Geometry_component_selection*       remap_destination
        ) -> void {
            erhe::geometry::operation::Component_remap remap{remap_source, remap_destination};
            erhe::geometry::operation::chamfer3(before_geometry, after_geometry, bevel_ratio, selected_facets, &remap);
        }
    );
    set_description(fmt::format("Chamfer3 {}", describe_entries()));
}

Dual_operation::Dual_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    set_description("Dual");
    make_entries(erhe::geometry::operation::dual);
    set_description(fmt::format("Dual {}", describe_entries()));
}

Ambo_operation::Ambo_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    set_description("Ambo");
    make_entries(erhe::geometry::operation::ambo);
    set_description(fmt::format("Ambo {}", describe_entries()));
}

Truncate_operation::Truncate_operation(Mesh_operation_parameters&& context, float ratio)
    : Mesh_operation{std::move(context)}
{
    set_description("Truncate");
    make_entries(
        [ratio](
            const erhe::geometry::Geometry& before_geometry,
            erhe::geometry::Geometry&       after_geometry,
            erhe::scene::Node*              /*node*/,
            const std::set<GEO::index_t>*   selected_facets,
            const erhe::geometry::operation::Geometry_component_selection* remap_source,
            erhe::geometry::operation::Geometry_component_selection*       remap_destination
        ) -> void {
            erhe::geometry::operation::Component_remap remap{remap_source, remap_destination};
            erhe::geometry::operation::truncate(before_geometry, after_geometry, ratio, selected_facets, &remap);
        }
    );
    set_description(fmt::format("Truncate {}", describe_entries()));
}

Merge_faces_operation::Merge_faces_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    set_description("Merge Faces");
    make_entries(
        [](
            const erhe::geometry::Geometry& before_geometry,
            erhe::geometry::Geometry&       after_geometry,
            erhe::scene::Node*              /*node*/,
            const std::set<GEO::index_t>*   selected_facets,
            const erhe::geometry::operation::Geometry_component_selection* remap_source,
            erhe::geometry::operation::Geometry_component_selection*       remap_destination
        ) -> void {
            erhe::geometry::operation::Component_remap remap{remap_source, remap_destination};
            erhe::geometry::operation::merge_faces(before_geometry, after_geometry, selected_facets, &remap);
        }
    );
    set_description(fmt::format("Merge Faces {}", describe_entries()));
}

Reverse_operation::Reverse_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    set_description("Reverse");
    make_entries(erhe::geometry::operation::reverse);
    set_description(fmt::format("Reverse {}", describe_entries()));
}

Normalize_operation::Normalize_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    set_description("Normalize");
    make_entries(erhe::geometry::operation::normalize);
    set_description(fmt::format("Normalize {}", describe_entries()));
}

Generate_tangents_operation::Generate_tangents_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    set_description("Generate tangents");
    make_entries(erhe::geometry::operation::generate_tangents);
    set_description(fmt::format("Generate tangents {}", describe_entries()));
}

Generate_frame_field_tangents_operation::Generate_frame_field_tangents_operation(Mesh_operation_parameters&& context, float sharp_angle_threshold)
    : Mesh_operation{std::move(context)}
{
    set_description("Generate frame field tangents");
    make_entries(
        [sharp_angle_threshold](const erhe::geometry::Geometry& source, erhe::geometry::Geometry& destination) {
            erhe::geometry::operation::generate_frame_field_tangents(source, destination, static_cast<double>(sharp_angle_threshold));
        }
    );
    set_description(fmt::format("Generate frame field tangents {}", describe_entries()));
}

Bake_transform_operation::Bake_transform_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    set_description("Bake transform");
    make_entries(
        [&](
            const erhe::geometry::Geometry& before_geometry,
            erhe::geometry::Geometry&       after_geometry,
            erhe::scene::Node*              node
        ) -> void
        {
            const glm::mat4 transform = m_parameters.transform.has_value()
                ? m_parameters.transform.value()
                : node->world_from_node();
            erhe::geometry::operation::bake_transform(before_geometry, after_geometry, to_geo_mat4f(transform));
        }
    );
    set_description(fmt::format("Bake transform {}", describe_entries()));
}

Repair_operation::Repair_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    set_description("Repair");
    make_entries(erhe::geometry::operation::repair);
    set_description(fmt::format("Repair {}", describe_entries()));
}

Weld_operation::Weld_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    set_description("Weld");
    make_entries(erhe::geometry::operation::weld);
    set_description(fmt::format("Weld {}", describe_entries()));
}

Remesh_operation::Remesh_operation(Mesh_operation_parameters&& context, unsigned int target_point_count, bool regenerate_attributes)
    : Mesh_operation{std::move(context)}
{
    set_description("Remesh");
    make_entries(
        [target_point_count, regenerate_attributes](const erhe::geometry::Geometry& source, erhe::geometry::Geometry& destination) {
            erhe::geometry::operation::remesh(source, destination, target_point_count, 0.0, regenerate_attributes);
        }
    );
    set_description(fmt::format("Remesh {}", describe_entries()));
}

Anisotropic_remesh_operation::Anisotropic_remesh_operation(Mesh_operation_parameters&& context, unsigned int target_point_count, float anisotropy, bool regenerate_attributes)
    : Mesh_operation{std::move(context)}
{
    set_description("Anisotropic remesh");
    make_entries(
        [target_point_count, anisotropy, regenerate_attributes](const erhe::geometry::Geometry& source, erhe::geometry::Geometry& destination) {
            erhe::geometry::operation::remesh(source, destination, target_point_count, static_cast<double>(anisotropy), regenerate_attributes);
        }
    );
    set_description(fmt::format("Anisotropic remesh {}", describe_entries()));
}

Decimate_operation::Decimate_operation(Mesh_operation_parameters&& context, unsigned int nb_bins, bool regenerate_attributes)
    : Mesh_operation{std::move(context)}
{
    set_description("Decimate");
    make_entries(
        [nb_bins, regenerate_attributes](const erhe::geometry::Geometry& source, erhe::geometry::Geometry& destination) {
            erhe::geometry::operation::decimate(source, destination, nb_bins, regenerate_attributes);
        }
    );
    set_description(fmt::format("Decimate {}", describe_entries()));
}

Smooth_operation::Smooth_operation(Mesh_operation_parameters&& context, unsigned int iterations, float strength, bool regenerate_attributes)
    : Mesh_operation{std::move(context)}
{
    set_description("Smooth");
    make_entries(
        [iterations, strength, regenerate_attributes](const erhe::geometry::Geometry& source, erhe::geometry::Geometry& destination) {
            erhe::geometry::operation::smooth(source, destination, iterations, strength, regenerate_attributes);
        }
    );
    set_description(fmt::format("Smooth {}", describe_entries()));
}

Make_atlas_operation::Make_atlas_operation(
    Mesh_operation_parameters&&                          context,
    std::size_t                                          usage_index,
    float                                                hard_angles_threshold,
    erhe::geometry::operation::Atlas_parameterizer       parameterizer,
    erhe::geometry::operation::Atlas_packer              packer,
    float                                                lightmap_texels_per_meter,
    float                                                chart_gutter_texels,
    float                                                chart_min_side_texels,
    std::unordered_map<const erhe::geometry::Geometry*, std::vector<float>> per_facet_chart_order
)
    : Mesh_operation{std::move(context)}
{
    set_description("Make atlas");
    // make_atlas() takes geogram_lock() internally, only around its
    // Geogram-parameterizer branch - the per-facet branch reaches no Geogram
    // algorithm. Skipping the blanket make_entries() wrap lets per-facet
    // unwraps of different meshes run concurrently on worker threads
    // (Generate Lightmap UVs / Reorder Charts queue one operation per mesh).
    m_callback_requires_geogram_lock = false;
    Lightmap_report* const report = m_parameters.context.lightmap_report;
    make_entries(
        [usage_index, hard_angles_threshold, parameterizer, packer, lightmap_texels_per_meter, chart_gutter_texels, chart_min_side_texels, report, per_facet_chart_order = std::move(per_facet_chart_order)](
            const erhe::geometry::Geometry& source,
            erhe::geometry::Geometry&       destination,
            erhe::scene::Node*              node
        ) {
            // Mesh-local density = world density times the node's linear
            // scale (uniform-scale approximation from the 3x3 determinant).
            double density = 0.0;
            if (lightmap_texels_per_meter > 0.0f) {
                const float det = (node != nullptr) ? glm::determinant(glm::mat3{node->world_from_node()}) : 1.0f;
                density = static_cast<double>(lightmap_texels_per_meter) * std::pow(static_cast<double>(std::abs(det)), 1.0 / 3.0);
            }
            const auto order_it = per_facet_chart_order.find(&source);
            const std::vector<float>* const order_keys = (order_it != per_facet_chart_order.end()) ? &order_it->second : nullptr;
            const std::string subject = (node != nullptr) ? node->get_name() : source.get_name();
            // Geogram parameterizers (ABF++ & co) throw on assertion
            // failures for degenerate input. Retry once with the per-facet
            // parameterizer, which skips Geogram entirely and cannot throw,
            // so one bad mesh no longer aborts the whole batch unseen -
            // requirement: unwrap failures must surface in the Lightmap
            // window (via Lightmap_report) and UV generation must succeed.
            try {
                erhe::geometry::operation::make_atlas(source, destination, usage_index, static_cast<double>(hard_angles_threshold), parameterizer, packer, density, static_cast<double>(chart_gutter_texels), static_cast<double>(chart_min_side_texels), order_keys);
            } catch (const std::exception& e) {
                if (parameterizer == erhe::geometry::operation::Atlas_parameterizer::per_facet) {
                    if (report != nullptr) {
                        report->add_error(Lightmap_report::Stage::uv_unwrap, subject, e.what());
                    }
                    throw;
                }
                if (report != nullptr) {
                    report->add_warning(
                        Lightmap_report::Stage::uv_unwrap,
                        subject,
                        fmt::format("parameterizer failed ({}); fell back to per-facet unwrap", e.what())
                    );
                }
                try {
                    erhe::geometry::operation::make_atlas(source, destination, usage_index, static_cast<double>(hard_angles_threshold), erhe::geometry::operation::Atlas_parameterizer::per_facet, packer, density, static_cast<double>(chart_gutter_texels), static_cast<double>(chart_min_side_texels), order_keys);
                } catch (const std::exception& retry_error) {
                    if (report != nullptr) {
                        report->add_error(Lightmap_report::Stage::uv_unwrap, subject, retry_error.what());
                    }
                    throw;
                }
            }
        }
    );
    set_description(fmt::format("Make atlas {}", describe_entries()));
}

///

Lattice_deform_operation::Lattice_deform_operation(
    Mesh_operation_parameters&&                            context,
    erhe::geometry::operation::Lattice_deform_parameters&& lattice_parameters,
    const bool                                             auto_fit_cage
)
    : Mesh_operation{std::move(context)}
{
    set_description("Lattice_deform");
    make_entries(
        [parameters = std::move(lattice_parameters), auto_fit_cage](
            const erhe::geometry::Geometry& before_geometry,
            erhe::geometry::Geometry&       after_geometry
        ) mutable -> void {
            if (auto_fit_cage) {
                // Fit the cage to the source geometry's local bounds; pad
                // degenerate axes so the lattice basis stays well defined
                // (same policy as the geometry-graph Lattice node).
                const GEO::MeshVertices& vertices = before_geometry.get_mesh().vertices;
                glm::vec3 cage_min{ std::numeric_limits<float>::max()};
                glm::vec3 cage_max{-std::numeric_limits<float>::max()};
                for (GEO::index_t vertex = 0; vertex < vertices.nb(); ++vertex) {
                    const GEO::vec3f p = erhe::geometry::get_pointf(vertices, vertex);
                    cage_min = glm::min(cage_min, glm::vec3{p.x, p.y, p.z});
                    cage_max = glm::max(cage_max, glm::vec3{p.x, p.y, p.z});
                }
                if (vertices.nb() > 0) {
                    for (int axis = 0; axis < 3; ++axis) {
                        if ((cage_max[axis] - cage_min[axis]) < 1e-4f) {
                            cage_min[axis] -= 0.05f;
                            cage_max[axis] += 0.05f;
                        }
                    }
                    parameters.cage_min = cage_min;
                    parameters.cage_max = cage_max;
                }
            }
            erhe::geometry::operation::lattice_deform(before_geometry, after_geometry, parameters);
        }
    );
    set_description(fmt::format("Lattice_deform {}", describe_entries()));
}

///

Intersection_operation::Intersection_operation(Mesh_operation_parameters&& parameters)
    : Binary_mesh_operation{std::move(parameters), "intersection", erhe::geometry::operation::intersection}
{
}
Difference_operation::Difference_operation(Mesh_operation_parameters&& parameters)
    : Binary_mesh_operation{std::move(parameters), "difference", erhe::geometry::operation::difference}
{
}
Union_operation::Union_operation(Mesh_operation_parameters&& parameters)
    : Binary_mesh_operation{std::move(parameters), "union", erhe::geometry::operation::union_}
{
}

// //

Binary_mesh_operation::Binary_mesh_operation(
    Mesh_operation_parameters&& parameters,
    const char*                 operation_name,
    std::function<void(
        const erhe::geometry::Geometry& lhs,
        const erhe::geometry::Geometry& rhs,
        erhe::geometry::Geometry&       result
    )> operation
)
    : Compound_operation{make_operations(std::move(parameters), operation_name, operation)}
{
}

auto Binary_mesh_operation::make_operations(
    Mesh_operation_parameters&& parameters,
    const char*                 operation_name,
    std::function<void(
        const erhe::geometry::Geometry& lhs,
        const erhe::geometry::Geometry& rhs,
        erhe::geometry::Geometry&       result
    )> operation
) -> Compound_operation::Parameters
{
    // Inputs come from parameters.items in order (snapshotted synchronously
    // on the main thread by Operations::resolve_operation_items, so the MCP
    // retarget-selection-and-restore pattern is race-free): the first
    // mesh-carrying content node is the target, the rest are tools.
    struct Entry
    {
        std::shared_ptr<erhe::geometry::Geometry> geometry;
        glm::mat4                                 target_from_entry_node;
    };
    std::vector<Entry> lhs_entries;
    std::vector<Entry> rhs_entries;
    std::shared_ptr<erhe::scene::Node>              target_node{};
    std::shared_ptr<erhe::scene::Mesh>              target_mesh{};
    std::vector<std::shared_ptr<erhe::scene::Node>> tool_nodes;
    std::shared_ptr<erhe::primitive::Material>      material{};
    erhe::primitive::Normal_style normal_style = erhe::primitive::Normal_style::none;
    glm::mat4 target_node_from_world = glm::mat4{1};
    erhe::Item_host* item_host = nullptr;

    for (const auto& item : parameters.items) {
        if (!erhe::utility::test_bit_set(item->get_flag_bits(), erhe::Item_flags::content)) {
            continue;
        }
        std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (!node) {
            continue;
        }
        erhe::scene::Node* raw_node = node.get();
        std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(raw_node);
        if (!mesh) {
            continue;
        }

        // All participating nodes share one host (async_for_nodes_with_mesh
        // verified this), so world transforms compose within one world space
        // and the single item_host_mutex lock below is correct.
        glm::mat4 target_from_entry_node;
        const bool is_target = !target_node;
        if (is_target) {
            target_from_entry_node = glm::mat4{1};
            target_node_from_world = raw_node->node_from_world();
            target_node            = node;
            target_mesh            = mesh;
            item_host              = raw_node->get_item_host();
        } else {
            target_from_entry_node = target_node_from_world * raw_node->world_from_node();
            tool_nodes.push_back(node);
        }

        std::vector<Entry>& entries = is_target ? lhs_entries : rhs_entries;
        for (erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_mutable_primitives()) {
            const erhe::primitive::Primitive&                               primitive = *mesh_primitive.primitive.get();
            const std::shared_ptr<erhe::primitive::Primitive_render_shape>& shape     = primitive.render_shape;
            if (!shape) {
                continue;
            }
            const std::shared_ptr<erhe::geometry::Geometry>& geometry = shape->get_geometry();
            if (!geometry) {
                continue;
            }
            entries.emplace_back(geometry, target_from_entry_node);
            if (normal_style == erhe::primitive::Normal_style::none) {
                normal_style = shape->get_normal_style();
            }
            if (!material) {
                material = mesh_primitive.material;
            }
        }
    }

    if (item_host == nullptr) {
        return {};
    }
    if (lhs_entries.empty() || rhs_entries.empty()) {
        log_operations->info("CSG {}: need a target mesh node plus at least one tool mesh node", operation_name);
        return {};
    }
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

    // Merge inputs into the target's local space and run the boolean. The CSG
    // implementations (and merge) reach Geogram, which must not run
    // concurrently with other Geogram invocations - see geogram_lock().
    std::shared_ptr<erhe::geometry::Geometry> out_geometry = std::make_shared<erhe::geometry::Geometry>(operation_name);
    {
        const std::lock_guard<std::recursive_mutex> geogram_guard{erhe::geometry::geogram_lock()};
        erhe::geometry::Geometry transformed_lhs{};
        erhe::geometry::Geometry transformed_rhs{};
        for (const Entry& entry : lhs_entries) {
            transformed_lhs.merge_with_transform(*entry.geometry.get(), to_geo_mat4f(entry.target_from_entry_node));
        }
        for (const Entry& entry : rhs_entries) {
            transformed_rhs.merge_with_transform(*entry.geometry.get(), to_geo_mat4f(entry.target_from_entry_node));
        }

        transformed_lhs.get_mesh().vertices.set_double_precision();
        transformed_rhs.get_mesh().vertices.set_double_precision();
        out_geometry->get_mesh().vertices.set_double_precision();
        operation(
            transformed_lhs,
            transformed_rhs,
            *out_geometry.get()
        );

        out_geometry->get_mesh().vertices.set_single_precision();
    }

    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;

    out_geometry->process({.flags = flags});

    // If the CSG result is empty (no facets), produce an empty compound operation
    if (out_geometry->get_mesh().facets.nb() == 0) {
        log_operations->info("CSG operation produced empty result geometry");
        return Compound_operation::Parameters{};
    }

    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(out_geometry);
    const bool renderable_ok = primitive->make_renderable_mesh(parameters.build_info, normal_style);
    const bool raytrace_ok   = primitive->make_raytrace();
    ERHE_VERIFY(renderable_ok && raytrace_ok);

    // The result replaces the target mesh's primitives in place; the target
    // keeps its material unless it had none (then the first tool material).
    const std::shared_ptr<Node_physics> before_node_physics = erhe::scene::get_attachment<Node_physics>(target_node.get());
    Mesh_operation::Entry entry{
        .scene_mesh = target_mesh,
        .before = {
            .node_physics = before_node_physics,
            .primitives   = target_mesh->get_primitives()
        },
        .after = {
            .node_physics = before_node_physics,
            .primitives   = { erhe::scene::Mesh_primitive{primitive, material} }
        }
    };

    // Rebuild the collision shape from the result (same policy as
    // Mesh_operation::make_entries: convex hull of the new geometry).
    if (before_node_physics && parameters.context.editor_settings->physics.static_enable) {
        GEO::Mesh convex_hull{};
        const bool convex_hull_ok = erhe::geometry::make_convex_hull(out_geometry->get_mesh(), convex_hull);
        if (convex_hull_ok) {
            std::vector<float> coordinates;
            coordinates.resize(convex_hull.vertices.nb() * 3);
            for (GEO::index_t vertex : convex_hull.vertices) {
                const GEO::vec3f p = erhe::geometry::get_pointf(convex_hull.vertices, vertex);
                coordinates[3 * vertex + 0] = p.x;
                coordinates[3 * vertex + 1] = p.y;
                coordinates[3 * vertex + 2] = p.z;
            }
            auto collision_shape = erhe::physics::ICollision_shape::create_convex_hull_shape_shared(
                coordinates.data(),
                static_cast<int>(convex_hull.vertices.nb()),
                static_cast<int>(3 * sizeof(float))
            );
            const erhe::physics::IRigid_body_create_info rigid_body_create_info{
                .collision_shape = collision_shape,
                .debug_label     = out_geometry->get_name(),
                .motion_mode     = before_node_physics->get_motion_mode()
            };
            entry.after.node_physics = std::make_shared<Node_physics>(rigid_body_create_info);
        }
    }

    Mesh_operation_parameters entry_parameters{
        .context    = parameters.context,
        .build_info = parameters.build_info
    };
    entry_parameters.items.push_back(target_node);
    std::shared_ptr<Mesh_operation> mesh_operation = std::make_shared<Mesh_operation>(std::move(entry_parameters));
    mesh_operation->add_entry(std::move(entry));

    Compound_operation::Parameters compound_operation_parameters;
    compound_operation_parameters.operations.push_back(std::move(mesh_operation));
    for (const std::shared_ptr<erhe::scene::Node>& tool_node : tool_nodes) {
        compound_operation_parameters.operations.push_back(
            std::make_shared<Item_insert_remove_operation>(
                Item_insert_remove_operation::Parameters{
                    .context = parameters.context,
                    .item    = tool_node,
                    .parent  = tool_node->get_parent().lock(),
                    .mode    = Item_insert_remove_operation::Mode::remove
                }
            )
        );
    }

    return compound_operation_parameters;
}

}
