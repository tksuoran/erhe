#pragma once

#include <cstdint>
#include <string>

namespace erhe::geometry {

// Thread-safe snapshot of the innermost running Geogram progress task.
//
// Geogram drives progress through GEO::ProgressTask / GEO::ProgressClient: an
// algorithm constructs a ProgressTask and calls progress()/next() on it from its
// own (worker) thread. register_geogram_progress() installs a client that copies
// the running task's name and percent into a single mutex-guarded snapshot, which
// the UI thread reads via get_geogram_progress().
//
// The caller is expected to keep one reused Geogram_progress_state instance and
// pass it to get_geogram_progress() every frame, so its std::string keeps its
// capacity (the reader assigns into it rather than reallocating).
class Geogram_progress_state
{
public:
    bool          active     {false}; // true while any ProgressTask is on the stack
    std::string   task_name  {};      // innermost task name (ASCII, from Geogram)
    unsigned int  step       {0};
    unsigned int  max_steps  {0};
    unsigned int  percent    {0};     // 0..100
    std::uint64_t generation {0};     // bumped on every begin/progress/end
};

// Installs an erhe ProgressClient as Geogram's progress client, replacing the
// default TerminalProgressClient. Call once, AFTER GEO::initialize() (which runs
// Progress::initialize() and installs the default client). Geogram takes
// ownership of the client via an intrusive SmartPointer (GEO::Progress::set_client),
// so it must not be deleted by erhe.
void register_geogram_progress();

// Detaches Geogram's progress client (installs a null client). Call during
// controlled shutdown, alongside unregister_geogram_logger(). Safe to call when
// no erhe client is registered.
void unregister_geogram_progress();

// Copies the current progress snapshot into out_state. Does not allocate once
// out_state.task_name has reached its capacity high-water mark (uses assign()).
// Returns out_state.active. Cheap and mutex-guarded; safe to call every frame
// from the UI thread while worker threads drive Geogram progress callbacks.
auto get_geogram_progress(Geogram_progress_state& out_state) -> bool;

} // namespace erhe::geometry
