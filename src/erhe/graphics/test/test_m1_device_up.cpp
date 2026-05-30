#include "gpu_test_environment.hpp"
#include "gpu_test_fixture.hpp"

#include "erhe_graphics/device.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace erhe::graphics::test {

// Milestone 1: the headless Vulkan device comes up cleanly.
//
// The shared device is constructed once by Gpu_test_environment::SetUp under
// VK_LAYER_KHRONOS_validation (enabled by the default Graphics_config when the
// layer is loadable). This test asserts the device reported a backend API
// string and that no validation warning/error was emitted while it was being
// created. Clean destruction is exercised at process end by
// Gpu_test_environment::TearDown (wait_idle + destroy).
TEST_F(Gpu_test, device_up_clean)
{
    erhe::graphics::Device& graphics_device = device();

    const erhe::graphics::Device_info& info = graphics_device.get_info();
    EXPECT_FALSE(info.api_info.empty()) << "Device should report a backend API string";

    const std::vector<Gpu_test_environment::Message>& setup_messages = Gpu_test_environment::get().setup_messages();
    for (const Gpu_test_environment::Message& message : setup_messages) {
        if (message.first) {
            ADD_FAILURE() << "Validation error during device creation: " << message.second;
        }
    }
}

} // namespace erhe::graphics::test
