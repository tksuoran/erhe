#include "renderers/shader_analytics_service.hpp"
#include "renderers/shader_session_programs.hpp"

#include "config/generated/shader_analytics_config.hpp"
#include "editor_log.hpp"
#include "scene/scene_commit_queue.hpp"

#include "erhe_graphics/shader_stages.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_task/task.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>
#include <taskflow/taskflow.hpp>

#include <algorithm>

namespace editor {

Shader_analytics_service::Shader_analytics_service(
    const Shader_analytics_config& config,
    Shader_session_programs&       session_programs,
    tf::Executor&                  executor,
    Shader_analytics&              shader_analytics
)
    : m_session_programs{session_programs}
    , m_executor        {executor}
    , m_shader_analytics{shader_analytics}
    , m_max_jobs        {static_cast<std::size_t>(std::max(config.max_jobs, 1))}
{
    const Offline_compiler_candidates malioc  = find_offline_compiler_candidates(config.malioc_search_paths,  "ERHE_MALIOC",  "malioc");
    const Offline_compiler_candidates powervr = find_offline_compiler_candidates(config.powervr_search_paths, "ERHE_POWERVR", c_powervr_executable);
    m_tools.malioc_candidates  = malioc.paths;
    m_tools.powervr_candidates = powervr.paths;
    m_tools.mali_core          = config.mali_core;
    m_tools.powervr_core       = config.powervr_core;
    for (const auto& [name, candidates] : {std::pair{"malioc", &malioc}, std::pair{c_powervr_executable, &powervr}}) {
        for (const std::filesystem::path& path : candidates->paths) {
            log_programs->info("Shader analytics: found {} {}", name, path.generic_string());
        }
        if (candidates->paths.empty()) {
            log_programs->info("Shader analytics: {} not found; searched {}", name, fmt::join(candidates->searched, ", "));
        }
    }
    if (malioc.paths.empty() && powervr.paths.empty()) {
        log_programs->info("Shader analytics: no malioc or {} found; shader cost modes unavailable", c_powervr_executable);
        return;
    }
    log_programs->info(
        "Shader analytics: programs are analyzed in background tasks with the newest version found of each compiler, reports cached in {}",
        m_cache_directory.generic_string()
    );
    m_listener_id = m_session_programs.add_listener(
        [this](const erhe::graphics::Shader_stages_create_info& create_info, const std::span<const erhe::graphics::Shader_program_module> modules) {
            on_program(create_info, modules);
        }
    );
}

Shader_analytics_service::~Shader_analytics_service() noexcept
{
    if (m_listener_id != 0) {
        m_session_programs.remove_listener(m_listener_id);
    }
    std::unique_lock<std::mutex> lock{m_mutex};
    m_stopping = true;
    m_jobs.clear();
    m_idle.wait(lock, [this]() { return m_running == 0; });
}

void Shader_analytics_service::start(Scene_commit_queue& main_thread_queue)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_main_thread_queue = &main_thread_queue;
    post_kick_locked();
}

// Until the version queries ran, a compiler counts as present when it has a candidate.
auto Shader_analytics_service::has_malioc_locked() const -> bool
{
    return m_versions_queried ? !m_tools.malioc.empty() : !m_tools.malioc_candidates.empty();
}

auto Shader_analytics_service::has_powervr_locked() const -> bool
{
    return m_versions_queried ? !m_tools.powervr.empty() : !m_tools.powervr_candidates.empty();
}

auto Shader_analytics_service::get_status() const -> Status
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return Status{
        .mali_core    = has_malioc_locked()  ? m_tools.mali_core    : std::string{},
        .powervr_core = has_powervr_locked() ? m_tools.powervr_core : std::string{},
        .queued       = m_jobs.size(),
        .running     = m_running,
        .analyzed    = m_analyzed,
        .failed      = m_failed
    };
}

void Shader_analytics_service::on_program(
    const erhe::graphics::Shader_stages_create_info&             create_info,
    const std::span<const erhe::graphics::Shader_program_module> modules
)
{
    const bool has_fragment = std::any_of(modules.begin(), modules.end(), [](const erhe::graphics::Shader_program_module& module) {
        return module.type == erhe::graphics::Shader_type::fragment_shader;
    });
    if (!has_fragment) {
        return; // the cost modes weigh fragments only
    }
    const std::vector<std::pair<std::string, std::string>> defines = create_info.get_variant_defines();
    nlohmann::json key_defines = nlohmann::json::array();
    for (const auto& [name, value] : defines) {
        if ((name == "ERHE_OVERDRAW") && (value != "0")) {
            return; // a counting variant: weighed by its OVERDRAW off twin
        }
        key_defines.push_back(nlohmann::json::array({name, value}));
    }
    std::string key = nlohmann::json::array({create_info.name, std::move(key_defines)}).dump();

    const std::lock_guard<std::mutex> lock{m_mutex};
    if (m_stopping || !m_seen.insert(key).second) {
        return;
    }
    Job job{.key = std::move(key), .name = create_info.name, .stages = {}};
    for (const erhe::graphics::Shader_program_module& module : modules) {
        job.stages.push_back(Offline_compiler_stage{.type = module.type, .spirv = {module.spirv.begin(), module.spirv.end()}});
    }
    m_jobs.push_back(std::move(job));
    post_kick_locked();
}

// Programs build on any thread, some of which may not spawn tasks; the kick
// runs on the main thread. Scene_commit_queue::enqueue() takes only its own
// lock and flush() runs outside it, so posting under m_mutex cannot deadlock.
void Shader_analytics_service::post_kick_locked()
{
    if ((m_main_thread_queue == nullptr) || m_kick_posted || (m_running >= m_max_jobs) || m_jobs.empty()) {
        return;
    }
    m_kick_posted = true;
    m_main_thread_queue->enqueue([this]() { kick(); });
}

void Shader_analytics_service::kick()
{
    std::size_t spawn_count = 0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_kick_posted = false;
        if (m_stopping) {
            return;
        }
        spawn_count = std::min((m_max_jobs > m_running) ? (m_max_jobs - m_running) : std::size_t{0}, m_jobs.size());
        m_running += spawn_count;
    }
    for (std::size_t i = 0; i < spawn_count; ++i) {
        erhe::task::spawn(m_executor, [this]() { work(); });
    }
}

void Shader_analytics_service::work()
{
    ERHE_PROFILE_FUNCTION();

    // The version queries run once, in the first task, not at startup.
    static std::once_flag s_versions_queried;
    std::call_once(s_versions_queried, [this]() {
        Offline_compiler_tools tools = m_tools;
        query_offline_compiler_versions(tools);
        {
            const std::lock_guard<std::mutex> lock{m_mutex};
            m_tools            = tools;
            m_versions_queried = true;
        }
        // The Shader Variants window's full analysis uses the same compilers.
        m_session_programs.set_offline_compiler_tools(tools);
        log_programs->info(
            "Shader analytics: malioc {} ({}), PowerVR {} ({})",
            m_tools.malioc_version, m_tools.malioc.empty() ? std::string{"off"} : m_tools.mali_core,
            m_tools.powervr_version, m_tools.powervr.empty() ? std::string{"off"} : m_tools.powervr_core
        );
    });

    for (;;) {
        Job job;
        {
            const std::lock_guard<std::mutex> lock{m_mutex};
            if (m_stopping || m_jobs.empty()) {
                --m_running;
                m_idle.notify_all();
                return;
            }
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }
        std::string         error;
        Shader_cost_metrics metrics{};
        {
            ERHE_PROFILE_SCOPE("analyze_program");
            metrics = analyze_program(m_tools, job.stages, m_cache_directory, error);
        }
        if (!error.empty()) {
            log_programs->warn("Shader analytics: {}: {}", job.name, error);
        }
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (error.empty()) {
            ++m_analyzed;
        } else {
            ++m_failed;
        }
        m_results.push_back(Result{.key = std::move(job.key), .metrics = metrics});
        if (!m_land_posted && !m_stopping) {
            m_land_posted = true;
            m_main_thread_queue->enqueue([this]() { land(); });
        }
    }
}

void Shader_analytics_service::land()
{
    std::vector<std::pair<std::string, Shader_cost_metrics>> programs;
    Offline_compiler_cores cores;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_land_posted = false;
        for (Result& result : m_results) {
            programs.emplace_back(std::move(result.key), result.metrics);
        }
        m_results.clear();
        cores.mali    = m_tools.malioc.empty()  ? std::string{} : m_tools.mali_core;
        cores.powervr = m_tools.powervr.empty() ? std::string{} : m_tools.powervr_core;
    }
    if (!programs.empty()) {
        m_shader_analytics.add_programs(cores, std::move(programs));
    }
}

} // namespace editor
