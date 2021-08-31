#include "application.hpp"
#include "renderdoc_capture_support.hpp"

#include "erhe/log/log.hpp"

auto main(int argc, char** argv) -> int
{
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
