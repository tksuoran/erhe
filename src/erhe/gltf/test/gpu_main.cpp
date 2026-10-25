#include <gtest/gtest.h>

// The device-backed half: erhe::gpu_test_support's Gpu_test_environment sets
// up the log sinks and the graphics loggers before the first case; the
// remaining loggers and geogram are initialized by the fixture
// (test_gltf_texture_gpu.cpp) once the sinks exist.
int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
