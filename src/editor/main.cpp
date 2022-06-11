#include "log.hpp"

#include "erhe/application/application.hpp"
#include "erhe/application/renderdoc_capture_support.hpp"

#include "erhe/application/log.hpp"
#include "erhe/components/log.hpp"
#include "erhe/geometry/log.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/graphics/log.hpp"
#include "erhe/physics/log.hpp"
#include "erhe/primitive/log.hpp"
#include "erhe/raytrace/log.hpp"
#include "erhe/scene/log.hpp"
#include "erhe/toolkit/log.hpp"
#include "erhe/ui/log.hpp"
#include "erhe/log/log.hpp"


#if defined(ERHE_PROFILE_LIBRARY_SUPERLUMINAL) && defined(_WIN32)
#   include <PerformanceAPI.h>

namespace {

class Performance_api_support
{
public:
    Performance_api_support()
    {
        PerformanceAPI::InstrumentationScope::initialize(L"C:\\Program Files\\Superluminal\\Performance\\API\\dll\\x64\\PerformanceAPI.dll");
    }
    ~Performance_api_support()
    {
        PerformanceAPI::InstrumentationScope::deinitialize();
    }
};

}
#   define ERHE_PROFILE_SUPPORT Performance_api_support performance_api_support;
#else
#   define ERHE_PROFILE_SUPPORT
#endif

auto main(int argc, char** argv) -> int
{
    ERHE_PROFILE_SUPPORT

    erhe::log::console_init();
    erhe::log::initialize_log_sinks();
    erhe::application::initialize_logging();
    erhe::components::initialize_logging();
    gl::initialize_logging();
    erhe::geometry::initialize_logging();
    erhe::graphics::initialize_logging();
    erhe::physics::initialize_logging();
    erhe::primitive::initialize_logging();
    erhe::raytrace::initialize_logging();
    erhe::scene::initialize_logging();
    erhe::toolkit::initialize_logging();
    erhe::ui::initialize_logging();

    editor::initialize_logging();

    //editor::initialize_renderdoc_capture_support();

    int return_value = EXIT_FAILURE;

    auto g_application = std::make_shared<erhe::application::Application>();
    if (g_application->initialize_components(argc, argv))
    {
        g_application->run();
        return_value = EXIT_SUCCESS;
    }
    else
    {
        return_value = EXIT_FAILURE;
    }

    g_application.reset();

    return return_value;
}
