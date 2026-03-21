#include "items.hpp"
#include "operations/mesh_operation.hpp"
#include "scene/scene_root.hpp"
#include "renderers/mesh_memory.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

#include <taskflow/taskflow.hpp>

#include <unordered_map>

namespace editor {

// Maps item ID -> pending async task handle.
// Entries are purged when completed to prevent unbounded growth.
// Item IDs are monotonically increasing (never reused), so stale
// entries for destroyed items are harmless and get purged here.
namespace {

std::unordered_map<std::size_t, tf::AsyncTask> s_item_tasks;

void purge_completed_tasks()
{
    std::erase_if(s_item_tasks, [](const auto& entry) {
        return entry.second.empty() || entry.second.is_done();
    });
}

} // anonymous namespace

void async_for_nodes_with_mesh(
    App_context&                                                context,
    const std::vector<std::shared_ptr<erhe::Item_base>>&        input_items,
    std::function<void(Mesh_operation_parameters&& parameters)> op
)
{
    purge_completed_tasks();

    // Locate item host
    erhe::Item_host* item_host = nullptr;
    {
        for (const auto& item : input_items) {
            if (item_host == nullptr) {
                item_host = item->get_item_host();
                break;
            }
        }
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
        items.push_back(item);
        auto it = s_item_tasks.find(item->get_id());
        if (it != s_item_tasks.end() && !it->second.empty()) {
            item_tasks.push_back(it->second);
        }
    }

    if (items.empty()) {
        return;
    }

    ++context.pending_async_ops;
    tf::AsyncTask task = context.executor->silent_dependent_async(
        [&context, op, items]()
        {
            ++context.running_async_ops;
            Mesh_operation_parameters parameters{
                .context = context,
                .build_info{
                    .primitive_types = {
                        .fill_triangles  = true,
                        .edge_lines      = true,
                        .corner_points   = true,
                        .centroid_points = true
                    },
                    .buffer_info     = context.mesh_memory->buffer_info
                }
            };

            parameters.items = items;
            op(std::move(parameters));
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

Item_async_task_guard::~Item_async_task_guard() noexcept
{
    s_item_tasks.clear();
}

}
