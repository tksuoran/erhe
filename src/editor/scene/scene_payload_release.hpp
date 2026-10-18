#pragma once

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
// Lifetime: the Editor waits for the pending releases and detaches the
// executor (set_executor(nullptr)) before it destroys the executor and the
// mesh memory; a release requested after that runs on the calling thread.
class Scene_payload_release
{
public:
    Scene_payload_release();
    ~Scene_payload_release() noexcept;

    Scene_payload_release(const Scene_payload_release&) = delete;
    Scene_payload_release& operator=(const Scene_payload_release&) = delete;

    // Main thread. Null: release() destroys on the calling thread.
    void set_executor(tf::Executor* executor);

    // Any thread (a Scene_root's last reference may drop on a worker that
    // owned it through a commit or an async load). Takes the references; the
    // payload is destroyed by one executor task (or right here without an
    // executor).
    void release(std::vector<std::shared_ptr<erhe::primitive::Primitive>>&& primitives, std::string_view scene_name);

    // Main thread. Blocks until every release task has finished.
    void wait_idle();

private:
    void on_task_finished();

    tf::Executor*                                m_executor{nullptr};
    std::mutex                                   m_mutex;
    std::condition_variable                      m_idle_condition;
    std::size_t                                  m_pending_count{0};
};

} // namespace editor
