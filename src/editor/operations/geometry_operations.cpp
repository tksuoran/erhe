#include "operations/geometry_operations.hpp"
#include "operations/item_insert_remove_operation.hpp"

#include "app_context.hpp"
#include "tools/selection_tool.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/bake_transform.hpp"
#include "erhe_geometry/operation/conway/ambo.hpp"
#include "erhe_geometry/operation/conway/chamfer.hpp"
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
#include "erhe_geometry/operation/generate_tangents.hpp"
#include "erhe_geometry/operation/normalize.hpp"
#include "erhe_geometry/operation/repair.hpp"
#include "erhe_geometry/operation/reverse.hpp"
#include "erhe_geometry/operation/subdivision/catmull_clark_subdivision.hpp"
#include "erhe_geometry/operation/subdivision/sqrt3_subdivision.hpp"
#include "erhe_geometry/operation/triangulate.hpp"
#include "erhe_scene/scene.hpp"

#include <fmt/format.h>

namespace editor {

Catmull_clark_subdivision_operation::Catmull_clark_subdivision_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::catmull_clark_subdivision);
    set_description(fmt::format("Catmull_clark {}", describe_entries()));
}

Sqrt3_subdivision_operation::Sqrt3_subdivision_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::sqrt3_subdivision);
    set_description(fmt::format("Sqrt3 {}", describe_entries()));
}

Triangulate_operation::Triangulate_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::triangulate);
    set_description(fmt::format("Triangulate {}", describe_entries()));
}

Join_operation::Join_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::join);
    set_description(fmt::format("Join {}", describe_entries()));
}

Kis_operation::Kis_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::kis);
    set_description(fmt::format("Kis {}", describe_entries()));
}

Subdivide_operation::Subdivide_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::subdivide);
    set_description(fmt::format("Subdivide {}", describe_entries()));
}

Meta_operation::Meta_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::meta);
    set_description(fmt::format("Meta {}", describe_entries()));
}

Gyro_operation::Gyro_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::gyro);
    set_description(fmt::format("Gyro {}", describe_entries()));
}

Chamfer_operation::Chamfer_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::chamfer);
    set_description(fmt::format("Chamfer {}", describe_entries()));
}

Dual_operation::Dual_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::dual);
    set_description(fmt::format("Dual {}", describe_entries()));
}

Ambo_operation::Ambo_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::ambo);
    set_description(fmt::format("Ambo {}", describe_entries()));
}

Truncate_operation::Truncate_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::truncate);
    set_description(fmt::format("Truncate {}", describe_entries()));
}

Reverse_operation::Reverse_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::reverse);
    set_description(fmt::format("Reverse {}", describe_entries()));
}

Normalize_operation::Normalize_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::normalize);
    set_description(fmt::format("Normalize {}", describe_entries()));
}

Generate_tangents_operation::Generate_tangents_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::generate_tangents);
    set_description(fmt::format("Generate tangents {}", describe_entries()));
}

Bake_transform_operation::Bake_transform_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
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
    set_description(fmt::format("Bake tranform {}", describe_entries()));
}

Repair_operation::Repair_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::repair);
    set_description(fmt::format("Repair {}", describe_entries()));
}

Weld_operation::Weld_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::weld);
    set_description(fmt::format("Weld {}", describe_entries()));
}

///

Intersection_operation::Intersection_operation(Mesh_operation_parameters&& parameters)
    : Binary_mesh_operation{std::move(parameters), erhe::geometry::operation::intersection}
{
}
Difference_operation::Difference_operation(Mesh_operation_parameters&& parameters)
    : Binary_mesh_operation{std::move(parameters), erhe::geometry::operation::difference}
{
}
Union_operation::Union_operation(Mesh_operation_parameters&& parameters)
    : Binary_mesh_operation{std::move(parameters), erhe::geometry::operation::union_}
{
}

// //

Binary_mesh_operation::Binary_mesh_operation(
    Mesh_operation_parameters&& parameters,
    std::function<void(
        const erhe::geometry::Geometry& lhs,
        const erhe::geometry::Geometry& rhs,
        erhe::geometry::Geometry&       result
    )> operation
)
    : Compound_operation{make_operations(std::move(parameters), operation)}
{
}

auto Binary_mesh_operation::make_operations(
    Mesh_operation_parameters&& parameters,
    std::function<void(
        const erhe::geometry::Geometry& lhs,
        const erhe::geometry::Geometry& rhs,
        erhe::geometry::Geometry&       result
    )> operation
) -> Compound_operation::Parameters
{
    // Collect input from selection
    struct Entry
    {
        std::shared_ptr<erhe::geometry::Geometry> geometry;
        glm::mat4                                 target_from_entry_node;
    };
    std::vector<Entry> lhs_entries;
    std::vector<Entry> rhs_entries;
    std::shared_ptr<erhe::primitive::Material> material{};
    erhe::primitive::Normal_style normal_style = erhe::primitive::Normal_style::none;

    const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = parameters.context.selection->get_selected_items();
    glm::mat4 target_node_from_world = glm::mat4{1};
    glm::mat4 target_world_from_node = glm::mat4{1};

    bool first_mesh = true;
    erhe::Item_host* item_host = nullptr;
    for (const auto& item : selected_items) {
        std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (!node) {
            continue;
        }
        erhe::scene::Node* raw_node = node.get();
        std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(raw_node);
        if (!mesh) {
            continue;
        }

        glm::mat4 target_from_entry_node;
        if (first_mesh) {
            target_from_entry_node = glm::mat4{1};
            target_node_from_world = raw_node->node_from_world();
            target_world_from_node = raw_node->world_from_node();
            first_mesh = false;
        } else {
            target_from_entry_node = target_node_from_world * raw_node->world_from_node();
        }

        std::vector<Entry>& entries = lhs_entries.empty() ? lhs_entries : rhs_entries;
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
            if (item_host == nullptr) {
                item_host = raw_node->get_item_host();
            }
        }
    }

    if (item_host == nullptr) {
        return {};
    }
    if (lhs_entries.empty() || rhs_entries.empty()) {
        return {};
    }
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};
    Scene_root* scene_root = static_cast<Scene_root*>(item_host);
    erhe::scene::Scene* scene = scene_root->get_hosted_scene();
    const auto scene_root_node = scene->get_root_node();

    // Merge lhs and rhs inputs
    erhe::geometry::Geometry transformed_lhs{};
    erhe::geometry::Geometry transformed_rhs{};
    for (const Entry& entry : lhs_entries) {
        transformed_lhs.merge_with_transform(*entry.geometry.get(), to_geo_mat4f(entry.target_from_entry_node));
    }
    for (const Entry& entry : rhs_entries) {
        transformed_rhs.merge_with_transform(*entry.geometry.get(), to_geo_mat4f(entry.target_from_entry_node));
    }

    // Perform operation
    std::shared_ptr<erhe::geometry::Geometry> out_geometry = std::make_shared<erhe::geometry::Geometry>();
    transformed_lhs.get_mesh().vertices.set_double_precision();
    transformed_rhs.get_mesh().vertices.set_double_precision();
    out_geometry->get_mesh().vertices.set_double_precision();
    operation(
        transformed_lhs,
        transformed_rhs,
        *out_geometry.get()
    );

    out_geometry->get_mesh().vertices.set_single_precision();

    //transform_mesh(out_geometry->get_mesh(), to_geo_mat4f(target_node_from_world));

    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;

    out_geometry->process(flags);

    // Create new Primitive
    constexpr uint64_t mesh_flags =
        erhe::Item_flags::visible     |
        erhe::Item_flags::content     |
        erhe::Item_flags::opaque      |
        erhe::Item_flags::shadow_cast |
        erhe::Item_flags::id          |
        erhe::Item_flags::show_in_ui;
    constexpr uint64_t node_flags =
        erhe::Item_flags::visible     |
        erhe::Item_flags::content     |
        erhe::Item_flags::show_in_ui;

    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(out_geometry);
    const bool renderable_ok = primitive->make_renderable_mesh(parameters.build_info, normal_style);
    const bool raytrace_ok   = primitive->make_raytrace();
    ERHE_VERIFY(renderable_ok);
    ERHE_VERIFY(raytrace_ok);

    // Create new Node
    std::string name{"TODO"};
    std::shared_ptr<erhe::scene::Node> node = std::make_shared<erhe::scene::Node>(name);
    std::shared_ptr<erhe::scene::Mesh> mesh = std::make_shared<erhe::scene::Mesh>(name);
    mesh->add_primitive(primitive, material);
    mesh->layer_id = scene_root->layers().content()->id;
    mesh->enable_flag_bits   (mesh_flags);
    node->set_world_from_node(target_world_from_node);
    node->attach             (mesh);
    node->enable_flag_bits   (node_flags);

    Compound_operation::Parameters compound_operation_parameters;
    compound_operation_parameters.operations.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = parameters.context,
                .item    = node,
                .parent  = scene_root_node,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );

    //if (parameters.make_operations_result_callback) {
    //    parameters.make_operations_result_callback(node, compound_operation_parameters);
    //}

    return compound_operation_parameters;
}

}
