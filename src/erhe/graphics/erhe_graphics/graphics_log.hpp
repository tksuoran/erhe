#pragma once

#include <spdlog/spdlog.h>

#include <memory>

// Define ERHE_LOG_VULKAN to enable the high-volume Vulkan sync/desc traces.
// When this define is not present, the ERHE_VULKAN_* macros below expand to
// no-ops so that no CPU work (argument evaluation, formatting, spdlog filter
// dispatch) happens for those logging sites.
#define ERHE_LOG_VULKAN 1

namespace erhe::graphics {

extern std::shared_ptr<spdlog::logger> log_buffer;
extern std::shared_ptr<spdlog::logger> log_context;
extern std::shared_ptr<spdlog::logger> log_swapchain;
extern std::shared_ptr<spdlog::logger> log_debug;
extern std::shared_ptr<spdlog::logger> log_framebuffer;
extern std::shared_ptr<spdlog::logger> log_fragment_outputs;
extern std::shared_ptr<spdlog::logger> log_glsl;
extern std::shared_ptr<spdlog::logger> log_load_png;
extern std::shared_ptr<spdlog::logger> log_program;
extern std::shared_ptr<spdlog::logger> log_save_png;
extern std::shared_ptr<spdlog::logger> log_shader_monitor;
extern std::shared_ptr<spdlog::logger> log_texture;
extern std::shared_ptr<spdlog::logger> log_texture_frame;
extern std::shared_ptr<spdlog::logger> log_texture_heap;
extern std::shared_ptr<spdlog::logger> log_threads;
extern std::shared_ptr<spdlog::logger> log_vertex_attribute_mappings;
extern std::shared_ptr<spdlog::logger> log_vertex_stream;
extern std::shared_ptr<spdlog::logger> log_render_pass;
extern std::shared_ptr<spdlog::logger> log_startup;
extern std::shared_ptr<spdlog::logger> log_vulkan_sync;
extern std::shared_ptr<spdlog::logger> log_vulkan_desc;
extern std::shared_ptr<spdlog::logger> log_vulkan;

void initialize_logging();

} // namespace erhe::graphics

#if defined(ERHE_LOG_VULKAN)
#   define ERHE_VULKAN_SYNC_TRACE(...)      ::erhe::graphics::log_vulkan_sync->trace(__VA_ARGS__)
#   define ERHE_VULKAN_DESC_TRACE(...)      ::erhe::graphics::log_vulkan_desc->trace(__VA_ARGS__)
#   define ERHE_VULKAN_TRACE(...)           ::erhe::graphics::log_vulkan->trace(__VA_ARGS__)
#   define ERHE_VULKAN_SYNC_LOG(level, ...) ::erhe::graphics::log_vulkan_sync->log(level, __VA_ARGS__)
#   define ERHE_VULKAN_DESC_LOG(level, ...) ::erhe::graphics::log_vulkan_desc->log(level, __VA_ARGS__)
#   define ERHE_VULKAN_LOG(level, ...)      ::erhe::graphics::log_vulkan->log(level, __VA_ARGS__)
#else
#   define ERHE_VULKAN_SYNC_TRACE(...)      ((void)0)
#   define ERHE_VULKAN_DESC_TRACE(...)      ((void)0)
#   define ERHE_VULKAN_TRACE(...)           ((void)0)
#   define ERHE_VULKAN_SYNC_LOG(level, ...) ((void)0)
#   define ERHE_VULKAN_DESC_LOG(level, ...) ((void)0)
#   define ERHE_VULKAN_LOG(level, ...)      ((void)0)
#endif
