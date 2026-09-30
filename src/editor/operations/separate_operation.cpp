#include "operations/separate_operation.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_settings.hpp"
#include "editor_log.hpp"
#include "operations/mesh_primitive_swap.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/split_components.hpp"
#include "erhe_item/item.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <mutex>
#include <set>
#include <string>

namespace editor {

namespace {

constexpr uint64_t result_process_flags =
    erhe::geometry::Geometry::process_flag_connect |
    erhe::geometry::Geometry::process_flag_build_edges |
    erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
    erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;

// Sanitizes, validates and processes a result geometry and builds its
// primitive; empty (logged) when the geometry does not validate.
[[nodiscard]] auto make_result_primitive(
    const std::shared_ptr<erhe::geometry::Geometry>& shared_geometry,
    const erhe::primitive::Build_info&               build_info,
    const erhe::primitive::Normal_style              normal_style,
    const std::string&                               label
) -> std::shared_ptr<erhe::primitive::Primitive>
{
    erhe::geometry::Geometry& geometry = *shared_geometry;
    for (const std::string& warning : geometry.sanitize()) {
        log_operations->warn("Separate: {} geometry sanitized: {}", label, warning);
    }
    const std::string validation_error = geometry.validate();
    if (!validation_error.empty()) {
        log_operations->error("Separate: {} geometry validation failed: {}", label, validation_error);
        return {};
    }
    geometry.process({.flags = result_process_flags});
    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(shared_geometry);
    const bool renderable_ok = primitive->make_renderable_mesh(build_info, normal_style);
    const bool raytrace_ok   = primitive->make_raytrace();
    ERHE_VERIFY(renderable_ok && raytrace_ok);
    return primitive;
}

} // anonymous namespace

Separate_selection_operation::Separate_selection_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    set_description("Separate");
    const bool physics_enabled = m_parameters.context.editor_settings->physics.static_enable;

    for (const std::shared_ptr<erhe::Item_base>& item : m_parameters.items) {
        if (!item || !erhe::utility::test_bit_set(item->get_flag_bits(), erhe::Item_flags::content)) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (!node) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Mesh> original = erhe::scene::get_mesh(node.get());
        if (!original || (original->get_item_host() == nullptr)) {
            continue;
        }
        erhe::Item_host* const item_host = original->get_item_host();
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

        Entry entry{};
        entry.original        = original;
        entry.original_before = original->get_primitives();
        std::vector<erhe::scene::Mesh_primitive> separated_primitives;
        std::shared_ptr<erhe::geometry::Geometry> first_kept;
        std::shared_ptr<erhe::geometry::Geometry> first_extracted;
        bool failed = false;
        for (const erhe::scene::Mesh_primitive& mesh_primitive : entry.original_before) {
            const std::shared_ptr<erhe::primitive::Primitive_render_shape>& render_shape =
                mesh_primitive.primitive ? mesh_primitive.primitive->render_shape : std::shared_ptr<erhe::primitive::Primitive_render_shape>{};
            const std::shared_ptr<erhe::geometry::Geometry> geometry = render_shape ? render_shape->get_geometry() : std::shared_ptr<erhe::geometry::Geometry>{};
            const auto selection_i = geometry ? m_parameters.component_selection.find(geometry.get()) : m_parameters.component_selection.end();
            std::set<GEO::index_t> facets;
            if (selection_i != m_parameters.component_selection.end()) {
                erhe::geometry::operation::get_selection_facets(*geometry, selection_i->second, facets);
            }
            if (facets.empty()) {
                entry.original_after.push_back(mesh_primitive);
                continue;
            }
            std::shared_ptr<erhe::geometry::Geometry> kept      = std::make_shared<erhe::geometry::Geometry>();
            std::shared_ptr<erhe::geometry::Geometry> extracted = std::make_shared<erhe::geometry::Geometry>();
            {
                // The operation reaches Geogram mesh code (see
                // erhe::geometry::geogram_lock()).
                const std::lock_guard<std::recursive_mutex> geogram_guard{erhe::geometry::geogram_lock()};
                erhe::geometry::operation::extract_facets(*geometry, *kept, *extracted, facets);
            }
            const erhe::primitive::Normal_style normal_style = render_shape->get_normal_style();
            std::shared_ptr<erhe::primitive::Primitive> kept_primitive      = make_result_primitive(kept,      m_parameters.build_info, normal_style, "kept");
            std::shared_ptr<erhe::primitive::Primitive> extracted_primitive = make_result_primitive(extracted, m_parameters.build_info, normal_style, "extracted");
            if (!kept_primitive || !extracted_primitive) {
                failed = true;
                break;
            }
            entry.original_after.emplace_back(kept_primitive, mesh_primitive.material);
            separated_primitives.emplace_back(extracted_primitive, mesh_primitive.material);
            if (!first_kept) {
                first_kept      = kept;
                first_extracted = extracted;
            }
        }
        if (failed || separated_primitives.empty()) {
            continue;
        }

        // Physics (see the class comment).
        entry.original_physics_before = Mesh_operation::capture_physics(*original);
        if (physics_enabled && (entry.original_physics_before.motion_mode != erhe::physics::Motion_mode::e_none)) {
            std::shared_ptr<erhe::physics::ICollision_shape> shape = Mesh_operation::make_convex_hull_collision_shape(*first_kept);
            if (shape) {
                entry.original_physics_after.collision_shape = shape;
                entry.original_physics_after.motion_mode     = entry.original_physics_before.motion_mode;
            }
        }
        if (physics_enabled && (entry.original_physics_before.motion_mode == erhe::physics::Motion_mode::e_static)) {
            std::shared_ptr<erhe::physics::ICollision_shape> shape = Mesh_operation::make_convex_hull_collision_shape(*first_extracted);
            if (shape) {
                entry.separated_physics.collision_shape = shape;
                entry.separated_physics.motion_mode     = erhe::physics::Motion_mode::e_static;
            }
        }

        // The new mesh (the Brush::make_instance() recipe): a Mesh is its own node.
        std::shared_ptr<erhe::scene::Mesh> separated = std::make_shared<erhe::scene::Mesh>(fmt::format("{} separated", original->get_name()));
        for (const erhe::scene::Mesh_primitive& mesh_primitive : separated_primitives) {
            separated->add_primitive(mesh_primitive.primitive, mesh_primitive.material);
        }
        separated->layer_id = original->layer_id;
        // The original's persistent flags: not its presentation state
        // (selection, hover), not the derived bits (written through their
        // properties below), not the import root marker, and lock_edit last.
        const uint64_t original_flags = original->get_flag_bits();
        const uint64_t excluded_flags =
            erhe::Item_flags::transient | erhe::Item_flags::derived | erhe::Item_flags::import_root | erhe::Item_flags::lock_edit;
        separated->enable_flag_bits(original_flags & ~excluded_flags);
        if (erhe::utility::test_bit_set(original_flags, erhe::Item_flags::shadow_cast)) {
            separated->set_value(erhe::scene::Mesh::shadow_cast_property, true);
        }
        if (erhe::utility::test_bit_set(original_flags, erhe::Item_flags::lightmapped)) {
            separated->set_value(erhe::scene::Mesh::lightmapped_property, true);
        }
        if (!original->is_visible()) {
            separated->set_visible(false);
        }
        separated->set_parent_from_node(original->parent_from_node_transform());
        if (erhe::utility::test_bit_set(original_flags, erhe::Item_flags::lock_edit)) {
            separated->enable_flag_bits(erhe::Item_flags::lock_edit);
        }
        entry.separated = separated;
        m_entries.push_back(std::move(entry));
    }

    std::string names;
    for (const Entry& entry : m_entries) {
        names += names.empty() ? entry.original->get_name() : (", " + entry.original->get_name());
    }
    set_description(fmt::format("[{}] Separate {}", get_serial(), names));
}

auto Separate_selection_operation::is_empty() const -> bool
{
    return m_entries.empty();
}

auto Separate_selection_operation::get_separated_meshes() const -> std::vector<std::shared_ptr<erhe::scene::Mesh>>
{
    std::vector<std::shared_ptr<erhe::scene::Mesh>> meshes;
    for (const Entry& entry : m_entries) {
        meshes.push_back(entry.separated);
    }
    return meshes;
}

void Separate_selection_operation::execute(App_context& context)
{
    ERHE_PROFILE_FUNCTION();

    if (m_entries.empty()) {
        return;
    }
    erhe::Item_host* const item_host = m_entries.front().original->get_item_host();
    if (item_host == nullptr) {
        set_error("Original mesh is not in a scene");
        log_operations->warn("Op Execute {} failed: {}", describe(), get_error());
        return;
    }
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

    for (Entry& entry : m_entries) {
        swap_mesh_primitives(entry.original, entry.original_after);
        Mesh_operation::restore_physics(*entry.original, entry.original_physics_after);

        std::shared_ptr<erhe::scene::Node> parent = entry.separated_parent;
        std::size_t                        index  = entry.separated_index_in_parent;
        if (!parent) {
            parent = entry.original->get_parent_node();
            index  = entry.original->get_index_in_parent() + 1;
        }
        ERHE_VERIFY(parent);
        entry.separated->set_parent(parent, index);
        if (entry.separated_physics.motion_mode != erhe::physics::Motion_mode::e_none) {
            Mesh_operation::restore_physics(*entry.separated, entry.separated_physics);
        }
    }
    for (const Entry& entry : m_entries) {
        context.app_message_bus->mesh_geometry_changed.send_message(
            Mesh_geometry_changed_message{.mesh = entry.original}
        );
    }
}

void Separate_selection_operation::undo(App_context& context)
{
    ERHE_PROFILE_FUNCTION();

    if (m_entries.empty() || has_error()) {
        return;
    }
    erhe::Item_host* const item_host = m_entries.front().original->get_item_host();
    if (item_host == nullptr) {
        return;
    }
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

    for (auto i = m_entries.rbegin(), end = m_entries.rend(); i != end; ++i) {
        Entry& entry = *i;
        entry.separated_parent          = entry.separated->get_parent_node();
        entry.separated_index_in_parent = entry.separated->get_index_in_parent();
        entry.separated->set_parent(std::shared_ptr<erhe::Hierarchy>{});

        swap_mesh_primitives(entry.original, entry.original_before);
        Mesh_operation::restore_physics(*entry.original, entry.original_physics_before);
    }
    for (const Entry& entry : m_entries) {
        context.app_message_bus->mesh_geometry_changed.send_message(
            Mesh_geometry_changed_message{.mesh = entry.original}
        );
    }
}

} // namespace editor
