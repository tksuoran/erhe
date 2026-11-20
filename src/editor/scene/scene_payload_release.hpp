#pragma once

#include "background_work.hpp"

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace erhe::primitive { class Primitive; }
namespace tf { class Executor; }

namespace editor {

// Destroys the mesh payload of a closed scene on a worker thread.
//
// Closing a large scene used to free every Primitive (Geometry, triangle
// soup, element mappings, raytrace, Buffer_mesh) on the main thread at the
// moment the last reference to the Scene_root dropped - seconds for Bistro.
// ~Scene_root collects a reference to every Primitive of its meshes before it
// severs the scene, so the item teardown on the main thread only drops
// references, and hands the collection to release(), which destroys it in
// one executor task. The main thread never waits for that task.
//
// What runs on the worker is what a Primitive destructor runs: CPU memory
// frees, and Buffer_mesh allocation retirement, which is mutex-protected and
// drained by Mesh_memory::flush() on the main thread
// (doc/erhe/mesh_memory_deferred_free.md). A Primitive still referenced
// elsewhere (undo history, a shared brush, an in-flight deferred finalize
// task) is not destroyed by the task, only released by it.
//
// The task runs on the background executor and reports itself as the
// background work owner `scene_payload_release` (doc/editor/editor.md
// "Worker executors").
//
// Lifetime: the Editor waits for the pending releases and detaches the
// executor (detach()) before it destroys the executor and the mesh memory; a
// release requested after that runs on the calling thread.
class Scene_payload_release
{
public:
    Scene_payload_release();
    ~Scene_payload_release() noexcept;

    Scene_payload_release(const Scene_payload_release&) = delete;
    Scene_payload_release& operator=(const Scene_payload_release&) = delete;

    // Main thread: release() spawns its task onto executor from now on and reports to executor_status.
    void attach(tf::Executor& executor, Executor_status& executor_status);
    // Main thread, after wait_idle(): release() destroys on the calling thread from now on.
    void detach();

    // Any thread (a Scene_root's last reference may drop on a worker that
    // owned it through a commit or an async load). Takes the references; the
    // payload is destroyed by one executor task (or right here without an
    // executor).
    void release(std::vector<std::shared_ptr<erhe::primitive::Primitive>>&& primitives, std::string_view scene_name);

    // Main thread. Blocks until every release task has finished.
    void wait_idle();

private:
    void on_task_started ();
    void on_task_finished();
    [[nodiscard]] auto get_background_work_counts() -> Background_work_counts;

    tf::Executor*                                m_executor{nullptr};
    std::mutex                                   m_mutex;
    std::condition_variable                      m_idle_condition;
    std::size_t                                  m_pending_count{0}; // spawned, not finished
    std::size_t                                  m_running_count{0}; // started, not finished
    std::unique_ptr<Background_work_owner>       m_background_work;  // while attached
};

} // namespace editor
