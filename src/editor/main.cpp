#include "application.hpp"
#include "renderdoc_capture_support.hpp"

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

    erhe::log::Log::console_init();

    editor::initialize_renderdoc_capture_support();

    int return_value = EXIT_FAILURE;

    auto g_application = std::make_shared<editor::Application>();
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
