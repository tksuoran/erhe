#include "items.hpp"
#include "app_context.hpp"
#include "editor_log.hpp"
#include "operations/mesh_operation.hpp"
#include "scene/scene_root.hpp"
#include "tools/mesh_component_selection.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

#include <taskflow/taskflow.hpp>

#include <exception>
#include <unordered_map>

namespace editor {

// Maps item ID -> pending async task handle. Main thread only.
// Item IDs are monotonically increasing (never reused), so stale
// entries for destroyed items are harmless and get purged here.
//
// Completed entries MUST be purged deterministically (once per frame via
// purge_completed_item_async_tasks), not just opportunistically on the next
// submission: a retained tf::AsyncTask handle keeps the underlying task node
// alive, and taskflow destroys the task lambda - and so its captures (scene
// root, mesh node items) - only when the last handle is released, not at
// completion. A handle left here past its task's completion would pin a
// closed scene's content indefinitely (the load/import raytrace kickoff
// captures the Scene_root).
namespace {

std::unordered_map<std::size_t, tf::AsyncTask> s_item_tasks;

void purge_completed_tasks()
{
    std::erase_if(s_item_tasks, [](const auto& entry) {
        return entry.second.empty() || entry.second.is_done();
    });
}

} // anonymous namespace

void purge_completed_item_async_tasks()
{
    purge_completed_tasks();
}

void async_for_nodes_with_mesh(
    App_context&                                                context,
    const std::vector<std::shared_ptr<erhe::Item_base>>&        input_items,
    std::function<void(Mesh_operation_parameters&& parameters)> op
)
{
    purge_completed_tasks();

    // Locate the item host from the first mesh-carrying content node. A
    // leading non-scene item (e.g. a selected material, whose host is null)
    // must not abort the whole operation. Callers pass a single scene's
    // items (the active scene's selection bucket), so one host covers all
    // qualifying nodes; the VERIFY below documents that invariant.
    erhe::Item_host* item_host = nullptr;
    for (const std::shared_ptr<erhe::Item_base>& item : input_items) {
        if (!erhe::utility::test_bit_set(item->get_flag_bits(), erhe::Item_flags::content)) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (!node || !erhe::scene::get_attachment<erhe::scene::Mesh>(node.get())) {
            continue;
        }
        item_host = item->get_item_host();
        break;
    }
    if (item_host == nullptr) {
        return;
    }
    Scene_root* scene_root = static_cast<Scene_root*>(item_host);

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};

    // Gather items with mesh and collect their pending tasks as dependencies
    std::vector<std::shared_ptr<erhe::Item_base>> items;
    std::vector<tf::AsyncTask> item_tasks;
    for (const std::shared_ptr<erhe::Item_base>& item : input_items) {
        const bool is_content = erhe::utility::test_bit_set(item->get_flag_bits(), erhe::Item_flags::content);
        if (!is_content) {
            continue;
        }

        const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (!node) {
            continue;
        }
        const erhe::scene::Node* raw_node = node.get();
        std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(raw_node);
        if (!mesh) {
            continue;
        }
        // All participating nodes share one host: the scene_lock above is
        // only correct under this invariant (callers pass a single scene's
        // items - the active scene's selection bucket).
        ERHE_VERIFY(item->get_item_host() == item_host);
        items.push_back(item);
        auto it = s_item_tasks.find(item->get_id());
        if (it != s_item_tasks.end() && !it->second.empty()) {
            item_tasks.push_back(it->second);
        }
    }

    if (items.empty()) {
        return;
    }

    // Snapshot the per-Geometry component selection while the scene lock is held on
    // the main thread. The Mesh_component_selection store is mutated on the main
    // thread, so it must not be read from the async worker; the snapshots are
    // captured by value into the worker lambda below. Both are keyed by Geometry
    // identity, matching the lookups in Mesh_operation::make_entries, and only live
    // entries contribute. selected_facets (face mode only) drives a selection-aware
    // operation's topology restriction; component_selection carries the selected
    // components of the active mode so the operation can remap them onto its result.
    std::unordered_map<const erhe::geometry::Geometry*, std::set<GEO::index_t>> selected_facets;
    std::unordered_map<const erhe::geometry::Geometry*, erhe::geometry::operation::Geometry_component_selection> component_selection;
    if (context.mesh_component_selection != nullptr) {
        const Mesh_component_mode mode = context.mesh_component_selection->get_mode();
        if (mode != Mesh_component_mode::object) {
            for (const Mesh_component_entry& entry : context.mesh_component_selection->get_entries()) {
                if (!context.mesh_component_selection->is_live(entry)) {
                    continue;
                }
                const std::shared_ptr<erhe::geometry::Geometry> geometry = entry.geometry.lock();
                if (!geometry) {
                    continue;
                }
                switch (mode) {
                    case Mesh_component_mode::face: {
                        if (entry.facets.empty()) {
                            continue;
                        }
                        selected_facets[geometry.get()]            = entry.facets;
                        component_selection[geometry.get()].facets = entry.facets;
                        break;
                    }
                    case Mesh_component_mode::vertex: {
                        if (entry.vertices.empty()) {
                            continue;
                        }
                        component_selection[geometry.get()].vertices = entry.vertices;
                        break;
                    }
                    case Mesh_component_mode::edge: {
                        if (entry.edges.empty()) {
                            continue;
                        }
                        component_selection[geometry.get()].edges = entry.edges;
                        break;
                    }
                    default: {
                        break;
                    }
                }
            }
        }
    }

    ++context.pending_async_ops;
    tf::AsyncTask task = context.executor->silent_dependent_async(
        [&context, op, items, selected_facets = std::move(selected_facets), component_selection = std::move(component_selection)]()
        {
            ++context.running_async_ops;
            // Geometry operations call into Geogram, whose assertion mechanism
            // throws by default (GEO::ASSERT_THROW). An exception escaping this
            // worker task would call std::terminate (process abort + a modal
            // "abort() has been called" dialog that hangs headless runs) and
            // leak the async counters. Catch at the task boundary: log the
            // failure (Geogram puts the assertion text in what()) and let the
            // counters settle so the operation simply does not happen.
            try {
                Mesh_operation_parameters parameters{
                    .context = context,
                    .build_info{
                        .primitive_types = {
                            .fill_triangles  = true,
                            .fill_triangles_expanded = true,
                            .edge_lines      = true,
                            .corner_points   = true,
                            .centroid_points = true
                        },
                        .buffer_info     = context.mesh_memory->make_primitive_buffer_info()
                    }
                };

                parameters.items               = items;
                parameters.selected_facets     = selected_facets;
                parameters.component_selection = component_selection;
                op(std::move(parameters));
            } catch (const std::exception& e) {
                log_operations->error("Async mesh operation failed: {}", e.what());
            } catch (...) {
                log_operations->error("Async mesh operation failed: unknown exception");
            }
            --context.running_async_ops;
            --context.pending_async_ops;
        },
        item_tasks.begin(), item_tasks.end()
    );

    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        s_item_tasks.insert_or_assign(item->get_id(), task);
    }
}

Item_async_task_guard::Item_async_task_guard() = default;

void Item_async_task_guard::clear() noexcept
{
    s_item_tasks.clear();
}

Item_async_task_guard::~Item_async_task_guard() noexcept
{
    s_item_tasks.clear();
}

}
