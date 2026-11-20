#include "erhe_raytrace/raytrace_executor.hpp"

#if defined(ERHE_RAYTRACE_LIBRARY_BVH)
#   include "erhe_raytrace/bvh/bvh_geometry.hpp"
#elif defined(ERHE_RAYTRACE_LIBRARY_EMBREE)
#   include "erhe_raytrace/embree/embree_device.hpp"
#endif

namespace erhe::raytrace {

namespace {

Task_spawner g_task_spawner{};

}

void set_task_spawner(Task_spawner spawner)
{
    g_task_spawner = std::move(spawner);
}

auto get_task_spawner() -> const Task_spawner&
{
    return g_task_spawner;
}

void start_thread_pool()
{
#if defined(ERHE_RAYTRACE_LIBRARY_BVH)
    start_bvh_thread_pool();
#elif defined(ERHE_RAYTRACE_LIBRARY_EMBREE)
    static_cast<void>(Embree_device::get_instance());
#endif
}

} // namespace erhe::raytrace
