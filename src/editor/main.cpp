#include "editor_application.hpp"

#include "erhe/application/renderdoc_capture_support.hpp"

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

    //editor::initialize_renderdoc_capture_support();

    editor::Application application;
    application.initialize_components(argc, argv);
    application.run();

    return EXIT_SUCCESS;
}
