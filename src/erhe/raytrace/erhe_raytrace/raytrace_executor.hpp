#pragma once

namespace tf {
    class Executor;
}

namespace erhe::raytrace {

// Executor used for background raytrace work, most notably scene level BVH
// builds. The application injects one at startup. When none is set, that work
// is done synchronously by the calling thread, which keeps tests and headless
// tools deterministic.
void set_executor(tf::Executor* executor);

[[nodiscard]] auto get_executor() -> tf::Executor*;

} // namespace erhe::raytrace
