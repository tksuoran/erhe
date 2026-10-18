#include "scene/scene_payload_release.hpp"

#include "editor_log.hpp"

#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_task/task.hpp"
#include "erhe_verify/verify.hpp"

#include <chrono>
#include <string>

namespace editor {

namespace {

void destroy_payload(std::vector<std::shared_ptr<erhe::primitive::Primitive>>& primitives, const std::string& scene_name)
{
    ERHE_PROFILE_SCOPE("Scene_payload_release destroy");
    const std::size_t                           count = primitives.size();
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    primitives.clear();
    primitives.shrink_to_fit();
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    const long long duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    log_scene->info("scene payload release: {} primitive references of closed scene '{}' released in {} ms", count, scene_name, duration_ms);
}

} // anonymous namespace

Scene_payload_release::Scene_payload_release() = default;

Scene_payload_release::~Scene_payload_release() noexcept
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    ERHE_VERIFY(m_pending_count == 0);
}

void Scene_payload_release::set_executor(tf::Executor* executor)
{
    m_executor = executor;
}

void Scene_payload_release::release(std::vector<std::shared_ptr<erhe::primitive::Primitive>>&& primitives, const std::string_view scene_name)
{
    if (primitives.empty()) {
        return;
    }
    if (m_executor == nullptr) {
        std::vector<std::shared_ptr<erhe::primitive::Primitive>> local{std::move(primitives)};
        destroy_payload(local, std::string{scene_name});
        return;
    }
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        ++m_pending_count;
    }
    // One task: the frees serialize on the process heap, so splitting the
    // payload over several tasks makes every task slower (measured: eight
    // tasks took longer in total than one). A shared_ptr because the executor
    // may copy the callable; the task empties the vector, so the payload dies
    // in the task whatever copies exist.
    std::shared_ptr<std::vector<std::shared_ptr<erhe::primitive::Primitive>>> payload =
        std::make_shared<std::vector<std::shared_ptr<erhe::primitive::Primitive>>>(std::move(primitives));
    erhe::task::spawn(
        *m_executor,
        [this, payload, name = std::string{scene_name}]() {
            destroy_payload(*payload, name);
            on_task_finished();
        }
    );
}

void Scene_payload_release::on_task_finished()
{
    // Notify under the lock: wait_idle() may return, and its caller destroy
    // this object, as soon as the count reads zero.
    const std::lock_guard<std::mutex> lock{m_mutex};
    ERHE_VERIFY(m_pending_count > 0);
    --m_pending_count;
    m_idle_condition.notify_all();
}

void Scene_payload_release::wait_idle()
{
    std::unique_lock<std::mutex> lock{m_mutex};
    m_idle_condition.wait(lock, [this]() { return m_pending_count == 0; });
}

} // namespace editor
