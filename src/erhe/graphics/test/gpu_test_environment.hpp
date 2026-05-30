#pragma once

#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace erhe::window   { class Context_window; }
namespace erhe::graphics { class Device; }

namespace erhe::graphics::test {

// Process-wide GPU context for the real-GPU test target. One headless
// Vulkan VkInstance / VkDevice is created once (Environment::SetUp) and
// destroyed once (Environment::TearDown), mirroring the editor's proven
// single-device lifetime. The device is brought up under
// VK_LAYER_KHRONOS_validation (the default Graphics_config enables it when
// the layer is loadable); validation warnings/errors are routed through the
// Device_message_callback into a message list so a failing test can be
// attributed precisely instead of aborting the whole run.
class Gpu_test_environment final : public ::testing::Environment
{
public:
    ~Gpu_test_environment() noexcept override;

    void SetUp   () override;
    void TearDown() override;

    [[nodiscard]] static auto get() -> Gpu_test_environment&;

    [[nodiscard]] auto device      () -> erhe::graphics::Device&;
    [[nodiscard]] auto is_available() const -> bool;

    // Validation messages collected while the device was being created
    // (snapshotted at the end of SetUp). M1 asserts this is empty.
    [[nodiscard]] auto setup_messages() const -> const std::vector<std::string>&;

    // Runtime (per-test) validation messages. The fixture clears these in
    // its SetUp and asserts they are empty in its TearDown.
    void               add_message   (const std::string& message);
    void               clear_messages();
    [[nodiscard]] auto take_messages () -> std::vector<std::string>;

private:
    std::unique_ptr<erhe::window::Context_window> m_window;
    std::unique_ptr<erhe::graphics::Device>       m_device;
    std::mutex                                    m_messages_mutex;
    std::vector<std::string>                      m_messages;
    std::vector<std::string>                      m_setup_messages;
    bool                                          m_available{false};
};

} // namespace erhe::graphics::test
