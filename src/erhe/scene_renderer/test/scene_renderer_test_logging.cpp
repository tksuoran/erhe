#include "scene_renderer_test_logging.hpp"

#include "erhe_file/file_log.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_property/property_log.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <mutex>

namespace erhe::scene_renderer::test {

void initialize_scene_renderer_test_logging()
{
    static std::once_flag s_once;
    std::call_once(
        s_once,
        []() {
            // erhe::file's own logger is bootstrapped by hand: its
            // initialize_logging() reads the logging configuration through
            // erhe::file while that logger is still null (doc/testing.md).
            // Program_interface reads shader files through erhe::file.
            if (!erhe::file::log_file) {
                erhe::file::log_file = spdlog::stdout_color_mt("erhe.file.bootstrap");
            }
            erhe::property::initialize_logging();
            erhe::item::initialize_logging();
            erhe::scene::initialize_logging();
            erhe::primitive::initialize_logging();
            erhe::scene_renderer::initialize_logging();
        }
    );
}

} // namespace erhe::scene_renderer::test
