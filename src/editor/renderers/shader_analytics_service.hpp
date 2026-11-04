#pragma once

#include "renderers/offline_compiler.hpp"
#include "renderers/shader_analytics.hpp"

#include "erhe_graphics/device.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

class Shader_analytics_config;

namespace erhe::graphics { class Shader_stages_create_info; }
namespace tf             { class Executor; }

namespace editor {

// Offline compiler analysis of every program the device builds, in
// background tasks (doc/editor/shader_analytics.md).
class Scene_commit_queue;

class Shader_analytics_service
{
public:
    Shader_analytics_service(
        const Shader_analytics_config& config,
        erhe::graphics::Device&        device,
        tf::Executor&                  executor,
        Shader_analytics&              shader_analytics
    );
    ~Shader_analytics_service() noexcept;
    Shader_analytics_service(const Shader_analytics_service&) = delete;
    auto operator=(const Shader_analytics_service&) -> Shader_analytics_service& = delete;

    class Status
    {
    public:
        std::string mali_core;   // empty: no malioc
        std::size_t queued   {0};
        std::size_t running  {0};
        std::size_t analyzed {0};
        std::size_t failed   {0};
    };
    [[nodiscard]] auto get_status() const -> Status;

    // Main thread, once: programs built before this queue up; analysis starts
    // with the main loop's first Scene_commit_queue flush.
    void start(Scene_commit_queue& main_thread_queue);

private:
    class Job
    {
    public:
        std::string                         key; // JSON [name, variant defines]
        std::string                         name;
        std::vector<Offline_compiler_stage> stages;
    };
    class Result
    {
    public:
        std::string         key;
        Shader_cost_metrics metrics;
    };

    void on_program(const erhe::graphics::Shader_stages_create_info& create_info, std::span<const erhe::graphics::Shader_program_module> modules);
    void post_kick_locked();
    void kick();      // main thread (Scene_commit_queue)
    void work();      // worker task: runs jobs until the queue is empty
    void land();      // main thread (Scene_commit_queue)

    erhe::graphics::Device&     m_device;
    tf::Executor&               m_executor;
    Scene_commit_queue*         m_main_thread_queue{nullptr};
    Shader_analytics&           m_shader_analytics;
    Offline_compiler_tools      m_tools;
    std::filesystem::path       m_cache_directory{"cache/shader_analytics"};
    std::size_t                 m_max_jobs{2};

    mutable std::mutex              m_mutex;
    std::condition_variable         m_idle;
    std::unordered_set<std::string> m_seen;
    std::deque<Job>                 m_jobs;
    std::vector<Result>             m_results;
    std::size_t                     m_running {0};
    std::size_t                     m_analyzed{0};
    std::size_t                     m_failed  {0};
    bool                            m_kick_posted{false};
    bool                            m_land_posted{false};
    bool                            m_stopping{false};
};

} // namespace editor
