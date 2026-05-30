#include "gpu_test_environment.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/generated/graphics_config.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_dataformat/dataformat_log.hpp"
#include "erhe_log/log.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_configuration.hpp"
#include "erhe_window/window_log.hpp"

#include <glm/glm.hpp>

namespace erhe::graphics::test {

namespace {

Gpu_test_environment* g_environment{nullptr};

// Self-register with GoogleTest in a static initializer so main.cpp stays
// identical to the deviceless erhe_graphics_tests target. GoogleTest takes
// ownership of the pointer and deletes it after the run.
const bool g_registered = []() -> bool {
    g_environment = new Gpu_test_environment{};
    ::testing::AddGlobalTestEnvironment(g_environment);
    return true;
}();

} // namespace

Gpu_test_environment::~Gpu_test_environment() noexcept = default;

auto Gpu_test_environment::get() -> Gpu_test_environment&
{
    return *g_environment;
}

void Gpu_test_environment::SetUp()
{
    // Loggers first: the Device backends log through these spdlog loggers.
    erhe::log::initialize_log_sinks();
    erhe::graphics::initialize_logging();
    erhe::window::initialize_logging();
    erhe::dataformat::initialize_logging();

    const erhe::window::Window_configuration window_configuration{
        .show  = false,
        .size  = glm::ivec2{64, 64},
        .title = std::string{"erhe_graphics_gpu_tests"}
    };
    m_window = std::make_unique<erhe::window::Context_window>(window_configuration);

    const erhe::graphics::Surface_create_info surface_create_info{
        .context_window            = m_window.get(),
        .prefer_low_bandwidth      = false,
        .prefer_high_dynamic_range = false
    };

    // Default-constructed Graphics_config: vulkan.vulkan_validation_layers
    // defaults to true, so the device is created under validation when the
    // layer is loadable. No JSON load and therefore no working-directory
    // dependency.
    // Graphics_config is generated into the global namespace (see
    // erhe_graphics/generated/graphics_config.hpp); device.hpp references it
    // unqualified for the same reason.
    const Graphics_config graphics_config{};

    erhe::graphics::Device_message_callback message_callback =
        [this](erhe::graphics::Message_severity severity, const std::string& message, const std::string&) {
            if (
                (severity == erhe::graphics::Message_severity::warning) ||
                (severity == erhe::graphics::Message_severity::error)
            ) {
                add_message(message);
            }
        };

    m_device = std::make_unique<erhe::graphics::Device>(surface_create_info, graphics_config, message_callback);
    m_available = true;

    // Snapshot any messages emitted during device creation, then clear the
    // runtime list so per-test message attribution starts from empty.
    {
        std::lock_guard<std::mutex> lock{m_messages_mutex};
        m_setup_messages = m_messages;
        m_messages.clear();
    }
}

void Gpu_test_environment::TearDown()
{
    if (m_device) {
        m_device->wait_idle();
    }
    m_device.reset();
    m_window.reset();
    m_available = false;
}

auto Gpu_test_environment::device() -> erhe::graphics::Device&
{
    return *m_device;
}

auto Gpu_test_environment::is_available() const -> bool
{
    return m_available;
}

auto Gpu_test_environment::setup_messages() const -> const std::vector<std::string>&
{
    return m_setup_messages;
}

void Gpu_test_environment::add_message(const std::string& message)
{
    std::lock_guard<std::mutex> lock{m_messages_mutex};
    m_messages.push_back(message);
}

void Gpu_test_environment::clear_messages()
{
    std::lock_guard<std::mutex> lock{m_messages_mutex};
    m_messages.clear();
}

auto Gpu_test_environment::take_messages() -> std::vector<std::string>
{
    std::lock_guard<std::mutex> lock{m_messages_mutex};
    std::vector<std::string> out;
    out.swap(m_messages);
    return out;
}

} // namespace erhe::graphics::test
