#include "content_library/content_library.hpp"

#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace editor {

class Mesh_operation_parameters;

template <typename T>
auto get(const std::vector<std::shared_ptr<erhe::Item_base>>& items, const std::size_t index = 0) -> std::shared_ptr<T>
{
    std::size_t i = 0;
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        if (!item) {
            continue;
        }
        if (!erhe::utility::test_all_rhs_bits_set(item->get_type(), T::get_static_type())) {
            continue;
        }
        if (i == index) {
            return std::static_pointer_cast<T>(item);
        }
        ++i;
    }
    return {};
}

template <typename T>
auto get_all(const std::vector<std::shared_ptr<erhe::Item_base>>& items) -> std::vector<std::shared_ptr<T>>
{
    std::vector<std::shared_ptr<T>> result;

    std::size_t i = 0;
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        if (!item) {
            continue;
        }
        if (!erhe::utility::test_all_rhs_bits_set(item->get_type(), T::get_static_type())) {
            continue;
        }
        result.push_back(std::static_pointer_cast<T>(item));
        ++i;
    }
    return result;
}

template <typename T>
auto count(const std::vector<std::shared_ptr<erhe::Item_base>>& items) -> std::size_t
{
    std::size_t i = 0;
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        if (!item) {
            continue;
        }
        if (!erhe::utility::test_all_rhs_bits_set(item->get_type(), T::get_static_type())) {
            continue;
        }
        ++i;
    }
    return i;
}

// op_builds_gpu_meshes: true for ops that build renderable meshes on the
// worker (every mesh-operation construction) - the dispatcher then takes a
// Scoped_worker_context around op, and on a device with no worker contexts
// (GL, headless / null window) it builds the parameters and runs op inline
// on the calling (main) thread instead of dispatching. Pass false for an op
// that manages its own worker-context scope or does no GPU work (the
// deferred glTF finalize).
void async_for_nodes_with_mesh(
    App_context&                                         context,
    const std::vector<std::shared_ptr<erhe::Item_base>>& items,
    std::function<void(Mesh_operation_parameters&&)>     op,
    bool                                                 op_builds_gpu_meshes = true
);

// Drops the handles of completed async tasks. A retained tf::AsyncTask
// handle keeps the whole task node alive - including the task lambda and its
// captures (scene root, mesh node items); completion alone does not free
// them. Called once per frame (Editor::tick, main thread) so a closed
// scene's content is not pinned by handles of its already-finished tasks.
void purge_completed_item_async_tasks();

// RAII guard that clears async task handles on destruction.
// Must be destroyed before the executor and loggers.
class Item_async_task_guard
{
public:
    Item_async_task_guard();
    Item_async_task_guard(const Item_async_task_guard&) = delete;
    Item_async_task_guard& operator=(const Item_async_task_guard&) = delete;
    ~Item_async_task_guard() noexcept;
    void clear() noexcept;
};

}
