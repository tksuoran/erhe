#include "app_context.hpp"

#include "assets/asset_manager.hpp"
#include "operations/operation_stack.hpp"
#include "scene/scene_commit_queue.hpp"

namespace editor {

auto App_context::get_async_in_flight_count() const -> std::size_t
{
    return
        static_cast<std::size_t>(pending_async_ops.load()) +
        static_cast<std::size_t>(running_async_ops.load()) +
        ((operation_stack    != nullptr) ? operation_stack->get_queued_count()     : 0u) +
        ((scene_commit_queue != nullptr) ? scene_commit_queue->get_pending_count() : 0u) +
        ((asset_manager      != nullptr) ? asset_manager->get_load_task_count()    : 0u);
}

}

