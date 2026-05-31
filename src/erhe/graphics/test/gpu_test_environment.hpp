#pragma once

#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace erhe::window   { class Context_window; }
namespace erhe::graphics { class Device; }

namespace erhe::graphics::test {

// Process-wide GPU context for the real-GPU test target. One Device is created
// once (Environment::SetUp) and destroyed once (Environment::TearDown),
// mirroring the editor's proven single-device lifetime. The backend is whatever
// the build was configured with (headless Vulkan, or OpenGL / Metal / windowed
// Vulkan on a hidden show=false window); the tests render offscreen and read
// back, so they are backend-neutral. When the backend exposes a debug/validation
// channel (e.g. Vulkan's VK_LAYER_KHRONOS_validation, enabled by the default
// Graphics_config when loadable), warnings/errors are routed through the
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

    // A validation message: is_error (true = Message_severity::error, i.e. a
    // correctness VUID; false = a warning / best-practices advisory) plus the
    // text. The fixture fails the case on errors and surfaces warnings without
    // failing, matching the editor's device-message policy.
    using Message = std::pair<bool, std::string>;

    // Messages collected while the device was being created (snapshotted at
    // the end of SetUp). M1 asserts there were no errors.
    [[nodiscard]] auto setup_messages() const -> const std::vector<Message>&;

    // Runtime (per-test) validation messages. The fixture clears these in its
    // SetUp and drains them in its TearDown.
    void               add_message   (bool is_error, const std::string& message);
    void               clear_messages();
    [[nodiscard]] auto take_messages () -> std::vector<Message>;

private:
    std::unique_ptr<erhe::window::Context_window> m_window;
    std::unique_ptr<erhe::graphics::Device>       m_device;
    std::mutex                                    m_messages_mutex;
    std::vector<Message>                          m_messages;
    std::vector<Message>                          m_setup_messages;
    bool                                          m_available{false};
};

} // namespace erhe::graphics::test
