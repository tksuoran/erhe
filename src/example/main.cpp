#include "example_application.hpp"

#include "erhe/application/renderdoc_capture_support.hpp"

auto main(int argc, char** argv) -> int
{
    example::Application application;
    application.initialize_components(argc, argv);
    application.run();

    return EXIT_SUCCESS;
}
