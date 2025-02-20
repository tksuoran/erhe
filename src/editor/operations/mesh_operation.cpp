#include "operations/mesh_operation.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_settings.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_scene/scene.hpp"

namespace editor {

Mesh_operation::Mesh_operation(Mesh_operation_parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
}

auto Mesh_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << fmt::format("[{}] ", get_serial());
    bool first = true;
    for (const auto& entry : m_entries) {
        if (first) {
            first = false;
        } else {
            ss << ", ";
        }
        ss << entry.scene_mesh->get_name();
    }
    return ss.str();
}

Mesh_operation::~Mesh_operation() noexcept = default;

void Mesh_operation::execute(Editor_context&)
{
    log_operations->trace("Op Execute {}", describe());

    ERHE_VERIFY(!m_entries.empty());
    Entry& first_entry = m_entries.front();
    erhe::scene::Mesh* first_mesh = first_entry.scene_mesh.get();
    ERHE_VERIFY(first_mesh != nullptr);
    erhe::scene::Node* first_node = first_mesh->get_node();
    ERHE_VERIFY(first_node != nullptr);
    erhe::Item_host* item_host = first_node->get_item_host();
    ERHE_VERIFY(item_host != nullptr);
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

    for (const auto& entry : m_entries) {
        auto* node = entry.scene_mesh->get_node();

        // TODO Improve physics RAII and remove this workaround
        std::shared_ptr<erhe::Hierarchy> parent = node->get_parent().lock();

        // This keeps node alive while we modify it
        std::shared_ptr<erhe::scene::Node> node_shared = std::dynamic_pointer_cast<erhe::scene::Node>(node->shared_from_this());

        node->set_parent(std::shared_ptr<erhe::Hierarchy>{});

        auto old_node_physics = get_node_physics(node);
        if (old_node_physics) {
            node->detach(old_node_physics.get());
        }
        entry.scene_mesh->set_primitives(entry.after.primitives);
        if (entry.after.node_physics) {
            node->attach(entry.after.node_physics);
        }

        node->set_parent(parent);
    }
}

void Mesh_operation::undo(Editor_context&)
{
    log_operations->trace("Op Undo {}", describe());

    ERHE_VERIFY(!m_entries.empty());
    Entry& first_entry = m_entries.front();
    erhe::scene::Mesh* first_mesh = first_entry.scene_mesh.get();
    ERHE_VERIFY(first_mesh != nullptr);
    erhe::scene::Node* first_node = first_mesh->get_node();
    ERHE_VERIFY(first_node != nullptr);
    erhe::Item_host* item_host = first_node->get_item_host();
    ERHE_VERIFY(item_host != nullptr);
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

    for (const auto& entry : m_entries) {
        auto* node = entry.scene_mesh->get_node();

        // TODO Improve physics RAII and remove this workaround
        std::shared_ptr<erhe::Hierarchy> parent = node->get_parent().lock();

        // This keeps node alive while we modify it
        std::shared_ptr<erhe::scene::Node> node_shared = std::dynamic_pointer_cast<erhe::scene::Node>(node->shared_from_this());

        node->set_parent(std::shared_ptr<erhe::Hierarchy>{});

        auto old_node_physics = get_node_physics(node);
        if (old_node_physics) {
            node->detach(old_node_physics.get());
        }
        entry.scene_mesh->set_primitives(entry.before.primitives);
        if (entry.before.node_physics) {
            node->attach(entry.before.node_physics);
        }

        node->set_parent(parent);
    }
}

void Mesh_operation::make_entries(const std::function<void(const erhe::geometry::Geometry& before_geometry, erhe::geometry::Geometry& after_geometry)> operation)
{
    make_entries(
        [&operation](const erhe::geometry::Geometry& before_geometry, erhe::geometry::Geometry& after_geometry, erhe::scene::Node*) -> void {
            operation(before_geometry, after_geometry);
        }
    );
}

void Mesh_operation::make_entries(const std::function<void(const erhe::geometry::Geometry&, erhe::geometry::Geometry&, erhe::scene::Node*)> operation)
{
    Selection& selection = *m_parameters.context.selection;
    const auto& selected_items = selection.get_selection();
    if (selected_items.empty()) {
        return;
    }

    const auto first_mesh = selection.get<erhe::scene::Mesh>();
    const auto first_node = selection.get<erhe::scene::Node>();
    if (!first_mesh && !first_node) {
        return;
    }

    erhe::Item_host* item_host = first_node->get_item_host();
    ERHE_VERIFY(item_host != nullptr);
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

    auto* const first_node_raw = first_node ? first_node.get() : first_mesh->get_node();
    if (first_node_raw == nullptr) {
        // TODO Can this limitation be lifted?
        log_operations->error("First selected mesh does not have scene, cannot perform geometry operation");
        return;
    }

    auto* scene_root = static_cast<Scene_root*>(first_node_raw->node_data.host);
    if (scene_root == nullptr) {
        log_operations->error("First selected mesh does node not have item (scene) host");
        return;
    }

#if !defined(NDEBUG)
    const auto& scene = scene_root->get_scene();
    scene.sanity_check();
#endif

    for (auto& item : selected_items) {
        // Prevent hotbar etc. from being operated
        const bool is_content = erhe::bit::test_all_rhs_bits_set(
            item->get_flag_bits(),
            erhe::Item_flags::content
        );
        if (!is_content) {
            continue;
        }

        auto  node_shared = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        auto* node        = node_shared.get();
        auto  scene_mesh  = std::dynamic_pointer_cast<erhe::scene::Mesh>(item);

        // If we have node selected, get mesh from node
        if (!scene_mesh) {
            if (node != nullptr) {
                scene_mesh = erhe::scene::get_mesh(node);
            }
        }
        if (!scene_mesh) {
            continue;
        }
        // If we have mesh selected, get node from mesh
        if (node == nullptr) {
            node        = scene_mesh->get_node();
            node_shared = std::dynamic_pointer_cast<erhe::scene::Node>(node->shared_from_this());
        }

        Entry entry{
            // TODO consider keeping node alive always .node   = node_shared,
            .scene_mesh = scene_mesh,
            .before = {
                .node_physics = get_node_physics(node),
                .primitives   = scene_mesh->get_primitives()
            },
        };

        erhe::physics::IRigid_body* rigid_body  = entry.before.node_physics ? entry.before.node_physics->get_rigid_body() : nullptr;
        erhe::physics::Motion_mode  motion_mode = (rigid_body != nullptr) ? rigid_body->get_motion_mode() : erhe::physics::Motion_mode::e_invalid;
        if (entry.before.node_physics->physics_motion_mode != motion_mode) {
            motion_mode = entry.before.node_physics->physics_motion_mode;
        }

        for (auto& primitive : scene_mesh->get_mutable_primitives()) {
            const std::shared_ptr<erhe::primitive::Primitive_render_shape>& render_shape = primitive.render_shape;
            const std::shared_ptr<erhe::geometry::Geometry>& before_geometry = render_shape->get_geometry();
            if (!before_geometry) {
                continue;
            }
            auto after_geometry = std::make_shared<erhe::geometry::Geometry>();
            operation(*before_geometry.get(), *after_geometry.get(), node);

            const uint64_t flags =
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_build_edges |
                erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
                erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;

            after_geometry->process(flags);

            erhe::primitive::Primitive after_primitive{after_geometry, primitive.material};
            const bool renderable_ok = after_primitive.make_renderable_mesh(m_parameters.build_info, render_shape->get_normal_style());
            const bool raytrace_ok   = after_primitive.make_raytrace();
            ERHE_VERIFY(renderable_ok && raytrace_ok);
            entry.after.primitives.push_back(after_primitive);

            if (m_parameters.context.editor_settings->physics.static_enable) {

                GEO::Mesh convex_hull{};
                const bool convex_hull_ok = make_convex_hull(after_geometry->get_mesh(), convex_hull);
                ERHE_VERIFY(convex_hull_ok); // TODO handle error

                std::vector<float> coordinates;
                coordinates.resize(convex_hull.vertices.nb() * 3);
                for (GEO::index_t vertex : convex_hull.vertices) {
                    const GEO::vec3f p = get_pointf(convex_hull.vertices, vertex);
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
                    .debug_label     = after_geometry->get_name(),
                    .motion_mode     = motion_mode
                };

                entry.after.node_physics = std::make_shared<Node_physics>(rigid_body_create_info);
                entry.after.node_physics->physics_motion_mode = entry.before.node_physics->physics_motion_mode;
            }

        }
        add_entry(std::move(entry));
    }

#if !defined(NDEBUG)
    scene.sanity_check();
#endif
}

void Mesh_operation::add_entry(Entry&& entry)
{
    m_entries.emplace_back(entry);
}

} // namespace editor
