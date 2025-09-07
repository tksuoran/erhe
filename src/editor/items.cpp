#include "items.hpp"
#include "operations/mesh_operation.hpp"
#include "scene/scene_root.hpp"
#include "renderers/mesh_memory.hpp"

namespace editor {

void async_for_nodes_with_mesh(
    App_context&                                                context,
    const std::vector<std::shared_ptr<erhe::Item_base>>&        input_items,
    std::function<void(Mesh_operation_parameters&& parameters)> op
)
{
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

    // Gather items with mesh and their tasks
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
        std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(raw_node);
        if (!mesh) {
            continue;
        }
        items.push_back(item);
        const tf::AsyncTask& previous_task = item->get_task();
        if (!previous_task.empty()) {
            item_tasks.push_back(item->get_task());
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
        item->set_task(task);
    }
}

}
