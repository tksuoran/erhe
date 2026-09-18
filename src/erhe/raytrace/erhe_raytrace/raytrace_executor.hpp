#pragma once

#include <functional>

namespace erhe::raytrace {

// Spawner used for background raytrace work, most notably scene level BVH
// builds. The application injects one at startup. When none is set, that work
// is done synchronously by the calling thread, which keeps tests and headless
// tools deterministic.
//
// A spawn FUNCTION rather than a tf::Executor: the application's spawner
// routes through its guarded spawn wrapper (proposal A of
// doc/erhe/gl_worker_context_enforcement.md), so this library's spawn is covered
// by the GL worker-context guard without a graphics dependency here, and no
// raw executor exists below the application to bypass it.
using Task_spawner = std::function<void(std::function<void()>)>;

void set_task_spawner(Task_spawner spawner);

[[nodiscard]] auto get_task_spawner() -> const Task_spawner&;

} // namespace erhe::raytrace
