#pragma once

#include "renderers/offline_compiler.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"

#include <cstddef>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <optional>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace erhe::graphics { class Shader_stages_create_info; }
namespace tf             { class Executor; }

namespace editor {

class Scene_commit_queue;

class Shader_session_stage
{
public:
    erhe::graphics::Shader_type type{erhe::graphics::Shader_type::vertex_shader};
    std::string                 cache_key;          // Spirv_cache entry; empty when not cached
    std::size_t                 module_bytes{0};    // the module the device uses
    std::size_t                 module_instructions{0};
    // Both modules' sizes (Spirv_cache_module_stats), read or computed in the background.
    bool                        stats_known{false};
    std::size_t                 unoptimized_bytes{0};
    std::size_t                 unoptimized_instructions{0};
    std::size_t                 optimized_bytes{0}; // 0: the optimizer failed or its output did not validate
    std::size_t                 optimized_instructions{0};
};

class Shader_session_program
{
public:
    std::string                                       key;   // JSON [name, variant defines]
    std::string                                       id;    // as the shader variant browser names it
    std::size_t                                       index{0}; // first build order
    std::string                                       name;
    std::vector<std::pair<std::string, std::string>> defines;
    std::vector<std::pair<std::string, std::string>> define_value_names; // (define, value expression)
    std::vector<Shader_session_stage>                stages;
    // analyze_program_stages() of the program; null until analyzed.
    nlohmann::ordered_json                           analysis{};
    bool                                             analysis_queued{false};
};

class Shader_session_analysis_status
{
public:
    bool        compilers_ready{false}; // the background analysis selected its compilers
    std::size_t program_count  {0};
    std::size_t analyzed       {0};
    std::size_t queued         {0};
};

// The programs the device built in this session (doc/editor/shader_analytics.md
// "Session programs"): owns the device's shader program observer and
// forwards every program to the listeners. Programs build on any thread.
class Shader_session_programs
{
public:
    explicit Shader_session_programs(erhe::graphics::Device& device);
    ~Shader_session_programs() noexcept;
    Shader_session_programs(const Shader_session_programs&) = delete;
    auto operator=(const Shader_session_programs&) -> Shader_session_programs& = delete;

    // Called for every program, on the building thread, until removed.
    [[nodiscard]] auto add_listener(erhe::graphics::Shader_program_observer listener) -> std::size_t;
    // After this returns the listener is not called again (a call in progress has finished).
    void remove_listener(std::size_t listener_id);
    // Called after the list changed, on the building thread.
    void set_change_callback(std::function<void()> callback);

    void copy_programs(std::vector<Shader_session_program>& out) const;

    // Any thread: the compilers the full analysis runs (the background
    // analysis selected them); queued analysis starts with them.
    void set_offline_compiler_tools(const Offline_compiler_tools& tools);
    // Any thread: queue the full analysis of a program (by key), or of every
    // program not analyzed yet; a no-op for one analyzed or queued.
    void request_analysis    (const std::string& key);
    void request_analysis_all();
    [[nodiscard]] auto get_analysis_status() const -> Shader_session_analysis_status;

    // Main thread, once: module stats missing from the SPIR-V cache are
    // computed in at most max_jobs background tasks from then on.
    void start(Scene_commit_queue& main_thread_queue, tf::Executor& executor, std::size_t max_jobs);
    [[nodiscard]] auto get_device() -> erhe::graphics::Device& { return m_device; }

private:
    void post_kick_locked();
    void kick(); // main thread
    void work(); // worker task: runs stats and analysis jobs until both queues are empty
    void run_stats_job   (const std::string& key);
    void run_analysis_job(const std::string& key, const Offline_compiler_tools& tools);
    void queue_analysis_locked(Shader_session_program& program);
    void notify_change();
    void on_program(const erhe::graphics::Shader_stages_create_info& create_info, std::span<const erhe::graphics::Shader_program_module> modules);

    class Listener
    {
    public:
        std::size_t                             id{0};
        erhe::graphics::Shader_program_observer observer;
    };

    erhe::graphics::Device&                              m_device;
    mutable std::mutex                                   m_mutex;
    std::mutex                                           m_listener_mutex; // held while listeners and the change callback run
    std::vector<Listener>                                m_listeners;
    std::size_t                                          m_next_listener_id{1};
    std::function<void()>                                m_change_callback; // under m_listener_mutex
    std::vector<Shader_session_program>                  m_programs;

    // Module stats jobs (program keys), under m_mutex.
    Scene_commit_queue*                                  m_main_thread_queue{nullptr};
    tf::Executor*                                        m_executor{nullptr};
    std::size_t                                          m_max_jobs{1};
    std::vector<std::string>                             m_stats_jobs;
    std::deque<std::string>                              m_analysis_jobs;
    std::optional<Offline_compiler_tools>                m_tools;
    std::filesystem::path                                m_analysis_cache_directory{"cache/shader_analytics"};
    std::size_t                                          m_running{0};
    bool                                                 m_kick_posted{false};
    bool                                                 m_stopping{false};
    std::condition_variable                              m_idle;
    std::string                                          m_spirv_tools_version;
};

} // namespace editor
