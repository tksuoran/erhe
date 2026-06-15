#include "erhe_geometry/geometry_progress.hpp"

#include <geogram/basic/progress.h>

#include <mutex>

namespace erhe::geometry {

namespace {

// All three fields below are protected by g_progress_mutex.
std::mutex             g_progress_mutex;
Geogram_progress_state g_progress_state;
int                    g_progress_depth{0}; // ProgressTask nesting depth

// Refresh the snapshot from the innermost task currently on Geogram's stack.
// Must be called with g_progress_mutex held. begin()/progress() run on the
// algorithm's worker thread, where current_progress_task() is consistent (the
// stack is only mutated by that same thread during these callbacks).
void refresh_from_current_task_locked()
{
    const GEO::ProgressTask* task = GEO::Progress::current_progress_task();
    if (task != nullptr) {
        g_progress_state.task_name.assign(task->task_name()); // assign keeps capacity
        g_progress_state.step      = static_cast<unsigned int>(task->step());
        g_progress_state.max_steps = static_cast<unsigned int>(task->max_steps());
        g_progress_state.percent   = static_cast<unsigned int>(task->percent());
    }
    ++g_progress_state.generation;
}

// Forwards Geogram progress callbacks into g_progress_state. Geogram owns this
// object via an intrusive SmartPointer (see register_geogram_progress()), so its
// destructor is protected and it is never deleted by erhe. GEO::ProgressClient
// derives from GEO::Counted; its own destructor is protected for the same reason.
class Geogram_progress_client : public GEO::ProgressClient
{
public:
    void begin() override
    {
        std::lock_guard<std::mutex> lock{g_progress_mutex};
        ++g_progress_depth;
        g_progress_state.active = true;
        refresh_from_current_task_locked();
    }

    void progress(GEO::index_t step, GEO::index_t percent) override
    {
        // step / percent mirror what the current task reports; read the task
        // object so name and percent stay consistent even with nested tasks.
        static_cast<void>(step);
        static_cast<void>(percent);
        std::lock_guard<std::mutex> lock{g_progress_mutex};
        g_progress_state.active = (g_progress_depth > 0);
        refresh_from_current_task_locked();
    }

    void end(bool canceled) override
    {
        static_cast<void>(canceled);
        std::lock_guard<std::mutex> lock{g_progress_mutex};
        if (g_progress_depth > 0) {
            --g_progress_depth;
        }
        g_progress_state.active = (g_progress_depth > 0);
        // Leave task_name as-is when going idle: the reader ignores the fields
        // while active is false, and keeping the string avoids freeing capacity.
        ++g_progress_state.generation;
    }

protected:
    ~Geogram_progress_client() noexcept override = default;
};

} // anonymous namespace

void register_geogram_progress()
{
    // Geogram's Progress::initialize() (run by GEO::initialize()) installed a
    // default TerminalProgressClient. set_client() drops it (last SmartPointer
    // reference) and takes ownership of ours via intrusive refcount, so we must
    // not delete it ourselves; unregister_geogram_progress() detaches it.
    GEO::Progress::set_client(new Geogram_progress_client());

    std::lock_guard<std::mutex> lock{g_progress_mutex};
    g_progress_depth        = 0;
    g_progress_state.active = false;
    ++g_progress_state.generation;
}

void unregister_geogram_progress()
{
    // Installs a null client; subsequent task callbacks become no-ops and the
    // last SmartPointer reference to our client is dropped (destroying it).
    GEO::Progress::set_client(nullptr);

    std::lock_guard<std::mutex> lock{g_progress_mutex};
    g_progress_depth        = 0;
    g_progress_state.active = false;
    ++g_progress_state.generation;
}

auto get_geogram_progress(Geogram_progress_state& out_state) -> bool
{
    std::lock_guard<std::mutex> lock{g_progress_mutex};
    out_state.active     = g_progress_state.active;
    out_state.step       = g_progress_state.step;
    out_state.max_steps  = g_progress_state.max_steps;
    out_state.percent    = g_progress_state.percent;
    out_state.generation = g_progress_state.generation;
    out_state.task_name.assign(g_progress_state.task_name); // no realloc once capacity reached
    return out_state.active;
}

} // namespace erhe::geometry
