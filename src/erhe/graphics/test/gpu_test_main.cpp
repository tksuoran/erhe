#include "gpu_test_results.hpp"

#include <gtest/gtest.h>

// main() of erhe_graphics_gpu_tests. The deviceless erhe_graphics_tests keeps
// the plain main.cpp; this one additionally installs the listener that writes
// <results>/results.json (see gpu_test_results.hpp).
int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    erhe::graphics::test::install_results_listener();
    return RUN_ALL_TESTS();
}
