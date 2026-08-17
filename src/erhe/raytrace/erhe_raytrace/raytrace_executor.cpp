#include "erhe_raytrace/raytrace_executor.hpp"

namespace erhe::raytrace {

namespace {

tf::Executor* g_executor{nullptr};

}

void set_executor(tf::Executor* executor)
{
    g_executor = executor;
}

auto get_executor() -> tf::Executor*
{
    return g_executor;
}

} // namespace erhe::raytrace
