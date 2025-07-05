#include "operations/geometry_operations.hpp"

#include "app_context.hpp"
#include "tools/selection_tool.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/ambo.hpp"
#include "erhe_geometry/operation/bake_transform.hpp"
#include "erhe_geometry/operation/catmull_clark_subdivision.hpp"
#include "erhe_geometry/operation/chamfer.hpp"
#include "erhe_geometry/operation/dual.hpp"
#include "erhe_geometry/operation/gyro.hpp"
#include "erhe_geometry/operation/join.hpp"
#include "erhe_geometry/operation/kis.hpp"
#include "erhe_geometry/operation/meta.hpp"
#include "erhe_geometry/operation/normalize.hpp"
#include "erhe_geometry/operation/reverse.hpp"
#include "erhe_geometry/operation/sqrt3_subdivision.hpp"
#include "erhe_geometry/operation/subdivide.hpp"
#include "erhe_geometry/operation/triangulate.hpp"
#include "erhe_geometry/operation/truncate.hpp"
#include "erhe_geometry/operation/repair.hpp"
#include "erhe_geometry/operation/difference.hpp"
#include "erhe_geometry/operation/intersection.hpp"
#include "erhe_geometry/operation/union.hpp"

#include <fmt/format.h>

namespace editor {

auto Catmull_clark_subdivision_operation::describe() const -> std::string
{
    return fmt::format("Catmull_clark {}", Mesh_operation::describe());
}

Catmull_clark_subdivision_operation::Catmull_clark_subdivision_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::catmull_clark_subdivision);
}

auto Sqrt3_subdivision_operation::describe() const -> std::string
{
    return fmt::format("Sqrt3 {}", Mesh_operation::describe());
}

Sqrt3_subdivision_operation::Sqrt3_subdivision_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::sqrt3_subdivision);
}

auto Triangulate_operation::describe() const -> std::string
{
    return fmt::format("Triangulate {}", Mesh_operation::describe());
}

Triangulate_operation::Triangulate_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::triangulate);
}

auto Join_operation::describe() const -> std::string
{
    return fmt::format("Join {}", Mesh_operation::describe());
}

Join_operation::Join_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::join);
}

auto Kis_operation::describe() const -> std::string
{
    return fmt::format("Kis {}", Mesh_operation::describe());
}

Kis_operation::Kis_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::kis);
}

auto Subdivide_operation::describe() const -> std::string
{
    return fmt::format("Subdivide {}", Mesh_operation::describe());
}

Subdivide_operation::Subdivide_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::subdivide);
}

auto Meta_operation::describe() const -> std::string
{
    return fmt::format("Meta {}", Mesh_operation::describe());
}

Meta_operation::Meta_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::meta);
}

auto Gyro_operation::describe() const -> std::string
{
    return fmt::format("Gyro {}", Mesh_operation::describe());
}

Gyro_operation::Gyro_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::gyro);
}

auto Chamfer_operation::describe() const -> std::string
{
    return fmt::format("Chamfer {}", Mesh_operation::describe());
}

Chamfer_operation::Chamfer_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::chamfer);
}

auto Dual_operation::describe() const -> std::string
{
    return fmt::format("Dual {}", Mesh_operation::describe());
}

Dual_operation::Dual_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::dual);
}

auto Ambo_operation::describe() const -> std::string
{
    return fmt::format("Ambo {}", Mesh_operation::describe());
}

Ambo_operation::Ambo_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::ambo);
}

auto Truncate_operation::describe() const -> std::string
{
    return fmt::format("Truncate {}", Mesh_operation::describe());
}

Truncate_operation::Truncate_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::truncate);
}

auto Reverse_operation::describe() const -> std::string
{
    return fmt::format("Reverse {}", Mesh_operation::describe());
}

Reverse_operation::Reverse_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::reverse);
}

auto Normalize_operation::describe() const -> std::string
{
    return fmt::format("Normalize {}", Mesh_operation::describe());
}

Normalize_operation::Normalize_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::normalize);
}

auto Bake_transform_operation::describe() const -> std::string
{
    return fmt::format("Bake tranform {}", Mesh_operation::describe());
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
            const glm::mat4 transform = node->world_from_node();
            erhe::geometry::operation::bake_transform(before_geometry, after_geometry, to_geo_mat4f(transform));
        }
    );
}


auto Repair_operation::describe() const -> std::string
{
    return fmt::format("Repair {}", Mesh_operation::describe());
}

Repair_operation::Repair_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::repair);
}

auto Weld_operation::describe() const -> std::string
{
    return fmt::format("Weld {}", Mesh_operation::describe());
}

Weld_operation::Weld_operation(Mesh_operation_parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::weld);
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
        glm::mat4                                 transform;
    };
    std::vector<Entry> lhs_entries;
    std::vector<Entry> rhs_entries;
    std::shared_ptr<erhe::primitive::Material> material{};
    erhe::primitive::Normal_style normal_style = erhe::primitive::Normal_style::none;

    auto& selected_items = parameters.context.selection->get_selection();
    glm::mat4 reference_node_from_world = glm::mat4{1};

    bool first_mesh = false;
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

        glm::mat4 transform;
        if (first_mesh) {
            reference_node_from_world = raw_node->node_from_world();
            transform                 = glm::mat4{1};
        } else {
            transform = reference_node_from_world * raw_node->world_from_node();
        }

        std::vector<Entry>& entries = lhs_entries.empty() ? lhs_entries : rhs_entries;
        for (auto& primitive : mesh->get_mutable_primitives()) {
            const auto& shape = primitive.render_shape;
            if (!shape) {
                continue;
            }
            const std::shared_ptr<erhe::geometry::Geometry>& geometry = shape->get_geometry();
            if (!geometry) {
                continue;
            }
            entries.emplace_back(geometry, transform);
            if (normal_style == erhe::primitive::Normal_style::none) {
                normal_style = shape->get_normal_style();
            }
            if (!material) {
                material = primitive.material;
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
        transformed_lhs.merge_with_transform(*entry.geometry.get(), to_geo_mat4f(entry.transform));
    }
    for (const Entry& entry : rhs_entries) {
        transformed_rhs.merge_with_transform(*entry.geometry.get(), to_geo_mat4f(entry.transform));
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

    erhe::primitive::Primitive primitive{out_geometry, material};
    const bool renderable_ok = primitive.make_renderable_mesh(parameters.build_info, normal_style);
    const bool raytrace_ok   = primitive.make_raytrace();
    ERHE_VERIFY(renderable_ok);
    ERHE_VERIFY(raytrace_ok);

    // Create new Node
    std::string name{"TODO"};
    std::shared_ptr<erhe::scene::Node> node = std::make_shared<erhe::scene::Node>(name);
    std::shared_ptr<erhe::scene::Mesh> mesh = std::make_shared<erhe::scene::Mesh>(name);
    mesh->add_primitive(primitive, material);
    mesh->layer_id = scene_root->layers().content()->id;
    mesh->enable_flag_bits   (mesh_flags);
    node->set_world_from_node(reference_node_from_world);
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
    return compound_operation_parameters;
}

}
