#include "scene/scene_commit_queue.hpp"

#include "erhe_profile/profile.hpp"

namespace editor {

void Scene_commit_queue::enqueue(Commit commit)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    m_pending.push_back(std::move(commit));
}

void Scene_commit_queue::flush()
{
    ERHE_PROFILE_FUNCTION();

    // Take the batch out under the lock, apply it outside: a commit may
    // enqueue follow-up work, and workers keep enqueueing meanwhile.
    std::vector<Commit> batch;
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
        batch.swap(m_pending);
    }
    for (Commit& commit : batch) {
        commit();
    }
}

void Scene_commit_queue::clear()
{
    std::vector<Commit> dropped;
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
        dropped.swap(m_pending);
    }
    // Destroyed outside the lock (closures own scene roots / meshes).
}

auto Scene_commit_queue::get_pending_count() -> std::size_t
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    return m_pending.size();
}

}
