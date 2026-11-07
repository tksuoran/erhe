#include "renderers/shader_session_programs.hpp"

#include "editor_log.hpp"
#include "scene/scene_commit_queue.hpp"

#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/shader_variant_report.hpp"
#include "erhe_graphics/spirv_cache.hpp"
#include "erhe_hash/hash.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_task/task.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <taskflow/taskflow.hpp>

#include <algorithm>

namespace editor {

Shader_session_programs::Shader_session_programs(erhe::graphics::Device& device)
    : m_device             {device}
    , m_spirv_tools_version{erhe::graphics::get_spirv_tools_version()}
{
    m_device.set_shader_program_observer(
        [this](const erhe::graphics::Shader_stages_create_info& create_info, const std::span<const erhe::graphics::Shader_program_module> modules) {
            on_program(create_info, modules);
        }
    );
}

Shader_session_programs::~Shader_session_programs() noexcept
{
    stop();
}

void Shader_session_programs::stop()
{
    m_device.set_shader_program_observer({});
    std::unique_lock<std::mutex> lock{m_mutex};
    m_stopping = true;
    m_stats_jobs.clear();
    m_analysis_jobs.clear();
    m_idle.wait(lock, [this]() { return m_running == 0; });
}

void Shader_session_programs::start(Scene_commit_queue& main_thread_queue, tf::Executor& executor, const std::size_t max_jobs)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_main_thread_queue = &main_thread_queue;
    m_executor          = &executor;
    m_max_jobs          = std::max(max_jobs, std::size_t{1});
    post_kick_locked();
}

void Shader_session_programs::set_offline_compiler_tools(const Offline_compiler_tools& tools)
{
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_tools = tools;
        post_kick_locked();
    }
    notify_change(); // the window's analysis status names the compilers' readiness
}

void Shader_session_programs::queue_analysis_locked(Shader_session_program& program)
{
    if (program.analysis_queued || !program.analysis.is_null() || m_stopping) {
        return;
    }
    program.analysis_queued = true;
    m_analysis_jobs.push_back(program.key);
}

void Shader_session_programs::request_analysis(const std::string& key)
{
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        for (Shader_session_program& program : m_programs) {
            if (program.key == key) {
                // A selected program goes first.
                if (!program.analysis_queued && program.analysis.is_null() && !m_stopping) {
                    program.analysis_queued = true;
                    m_analysis_jobs.push_front(program.key);
                }
                break;
            }
        }
        post_kick_locked();
    }
    notify_change();
}

void Shader_session_programs::request_analysis_all()
{
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        for (Shader_session_program& program : m_programs) {
            queue_analysis_locked(program);
        }
        post_kick_locked();
    }
    notify_change();
}

auto Shader_session_programs::get_analysis_status() const -> Shader_session_analysis_status
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    Shader_session_analysis_status status{
        .compilers_ready = m_tools.has_value(),
        .program_count   = m_programs.size(),
        .analyzed        = 0,
        .queued          = 0
    };
    for (const Shader_session_program& program : m_programs) {
        if (!program.analysis.is_null()) {
            ++status.analyzed;
        } else if (program.analysis_queued) {
            ++status.queued;
        }
    }
    return status;
}

void Shader_session_programs::notify_change()
{
    const std::lock_guard<std::mutex> lock{m_listener_mutex};
    if (m_change_callback) {
        m_change_callback();
    }
}

// As Shader_analytics_service: programs build on any thread; tasks spawn from
// the main thread's commit queue, whose enqueue() takes only its own lock.
void Shader_session_programs::post_kick_locked()
{
    const bool has_work = !m_stats_jobs.empty() || (!m_analysis_jobs.empty() && m_tools.has_value());
    if ((m_main_thread_queue == nullptr) || m_kick_posted || m_stopping || (m_running >= m_max_jobs) || !has_work) {
        return;
    }
    m_kick_posted = true;
    m_main_thread_queue->enqueue([this]() { kick(); });
}

void Shader_session_programs::kick()
{
    std::size_t spawn_count = 0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_kick_posted = false;
        if (m_stopping) {
            return;
        }
        const std::size_t job_count = m_stats_jobs.size() + (m_tools.has_value() ? m_analysis_jobs.size() : 0);
        spawn_count = std::min((m_max_jobs > m_running) ? (m_max_jobs - m_running) : std::size_t{0}, job_count);
        m_running += spawn_count;
    }
    for (std::size_t i = 0; i < spawn_count; ++i) {
        erhe::task::spawn(*m_executor, [this]() { work(); });
    }
}

void Shader_session_programs::work()
{
    ERHE_PROFILE_FUNCTION();

    for (;;) {
        std::string                           key;
        std::optional<Offline_compiler_tools> analysis_tools;
        {
            const std::lock_guard<std::mutex> lock{m_mutex};
            // Stats first: they fill the table's SPIR-V columns.
            if (!m_stopping && !m_stats_jobs.empty()) {
                key = std::move(m_stats_jobs.back());
                m_stats_jobs.pop_back();
            } else if (!m_stopping && !m_analysis_jobs.empty() && m_tools.has_value()) {
                key = std::move(m_analysis_jobs.front());
                m_analysis_jobs.pop_front();
                analysis_tools = m_tools;
            } else {
                --m_running;
                m_idle.notify_all();
                return;
            }
        }
        if (analysis_tools.has_value()) {
            run_analysis_job(key, analysis_tools.value());
        } else {
            run_stats_job(key);
        }
    }
}

void Shader_session_programs::run_stats_job(const std::string& key)
{
    erhe::graphics::Spirv_cache& cache = m_device.get_spirv_cache();
    std::string                       name;
    std::vector<Shader_session_stage> stages;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        for (const Shader_session_program& program : m_programs) {
            if (program.key == key) {
                name   = program.name;
                stages = program.stages;
            }
        }
    }
    bool changed = false;
    for (Shader_session_stage& stage : stages) {
        if (stage.stats_known || stage.cache_key.empty()) {
            continue;
        }
        const std::optional<erhe::graphics::Spirv_cache_record> record = cache.get_record(stage.cache_key);
        const std::vector<unsigned int>                        module = cache.get_module(stage.cache_key);
        if (!record.has_value() || module.empty()) {
            continue;
        }
        const char* const stage_name = erhe::graphics::get_shader_variant_stage_short_name(stage.type);
        const erhe::graphics::Shader_variant_report_stage report_stage = erhe::graphics::make_report_stage(
            record.value(), module, stage.type, fmt::format("{} {} stage", name, (stage_name != nullptr) ? stage_name : "?")
        );
        const erhe::graphics::Spirv_cache_module_stats stats{
            .spirv_tools_version      = m_spirv_tools_version,
            .unoptimized_bytes        = report_stage.spirv.size() * sizeof(unsigned int),
            .unoptimized_instructions = erhe::graphics::count_spirv_instructions(report_stage.spirv),
            .optimized_bytes          = report_stage.optimized_spirv.size() * sizeof(unsigned int),
            .optimized_instructions   = erhe::graphics::count_spirv_instructions(report_stage.optimized_spirv)
        };
        cache.put_module_stats(stage.cache_key, stats);
        stage.stats_known              = true;
        stage.unoptimized_bytes        = stats.unoptimized_bytes;
        stage.unoptimized_instructions = stats.unoptimized_instructions;
        stage.optimized_bytes          = stats.optimized_bytes;
        stage.optimized_instructions   = stats.optimized_instructions;
        changed = true;
    }
    if (!changed) {
        return;
    }
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        for (Shader_session_program& program : m_programs) {
            if ((program.key == key) && (program.stages.size() == stages.size())) {
                program.stages = std::move(stages);
                break;
            }
        }
    }
    notify_change();
}

// Both modules of every stage from the records, then the offline compilers
// (analyze_program_stages(); doc/editor/shader_analytics.md "Session programs").
void Shader_session_programs::run_analysis_job(const std::string& key, const Offline_compiler_tools& tools)
{
    ERHE_PROFILE_FUNCTION();

    erhe::graphics::Spirv_cache& cache = m_device.get_spirv_cache();
    std::string                       name;
    std::vector<Shader_session_stage> stages;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        for (const Shader_session_program& program : m_programs) {
            if (program.key == key) {
                name   = program.name;
                stages = program.stages;
            }
        }
    }
    std::vector<Offline_compiler_program_stage> modules;
    bool complete = !stages.empty();
    for (const Shader_session_stage& stage : stages) {
        const std::optional<erhe::graphics::Spirv_cache_record> record = stage.cache_key.empty()
            ? std::optional<erhe::graphics::Spirv_cache_record>{}
            : cache.get_record(stage.cache_key);
        const std::vector<unsigned int> module = stage.cache_key.empty() ? std::vector<unsigned int>{} : cache.get_module(stage.cache_key);
        if (!record.has_value() || module.empty()) {
            complete = false;
            break;
        }
        const char* const stage_name = erhe::graphics::get_shader_variant_stage_short_name(stage.type);
        erhe::graphics::Shader_variant_report_stage report_stage = erhe::graphics::make_report_stage(
            record.value(), module, stage.type, fmt::format("{} {} stage", name, (stage_name != nullptr) ? stage_name : "?")
        );
        modules.push_back(Offline_compiler_program_stage{
            .type   = stage.type,
            .before = std::move(report_stage.spirv),
            .after  = std::move(report_stage.optimized_spirv)
        });
    }
    nlohmann::ordered_json analysis = complete
        ? analyze_program_stages(tools, modules, m_analysis_cache_directory)
        : nlohmann::ordered_json::object(); // analyzed, with nothing to show
    if (!complete) {
        log_programs->info("Shader analytics: {} has stages without SPIR-V cache records; not analyzed", name);
    }
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        for (Shader_session_program& program : m_programs) {
            if (program.key == key) {
                program.analysis        = std::move(analysis);
                program.analysis_queued = false;
                break;
            }
        }
    }
    notify_change();
}

auto Shader_session_programs::add_listener(erhe::graphics::Shader_program_observer listener) -> std::size_t
{
    const std::lock_guard<std::mutex> lock{m_listener_mutex};
    const std::size_t id = m_next_listener_id++;
    m_listeners.push_back(Listener{.id = id, .observer = std::move(listener)});
    return id;
}

void Shader_session_programs::remove_listener(const std::size_t listener_id)
{
    const std::lock_guard<std::mutex> lock{m_listener_mutex};
    std::erase_if(m_listeners, [listener_id](const Listener& listener) { return listener.id == listener_id; });
}

void Shader_session_programs::set_change_callback(std::function<void()> callback)
{
    const std::lock_guard<std::mutex> lock{m_listener_mutex};
    m_change_callback = std::move(callback);
}

void Shader_session_programs::copy_programs(std::vector<Shader_session_program>& out) const
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    out = m_programs;
}

void Shader_session_programs::on_program(
    const erhe::graphics::Shader_stages_create_info&             create_info,
    const std::span<const erhe::graphics::Shader_program_module> modules
)
{
    // The program's identity as the shader variant browser keys it: name and variant defines.
    Shader_session_program program{
        .key                = {},
        .id                 = {},
        .index              = 0,
        .name               = create_info.name,
        .defines            = create_info.get_variant_defines(),
        .define_value_names = {},
        .stages             = {},
        .analysis           = {},
        .analysis_queued    = false
    };
    nlohmann::json key_defines = nlohmann::json::array();
    for (const auto& [name, value] : program.defines) {
        key_defines.push_back(nlohmann::json::array({name, value}));
        if (const erhe::graphics::Shader_define_constants* constants = create_info.find_define_constants(name); constants != nullptr) {
            std::string expression = erhe::graphics::get_define_value_expression(*constants, value);
            if (!expression.empty()) {
                program.define_value_names.emplace_back(name, std::move(expression));
            }
        }
    }
    program.key = nlohmann::json::array({create_info.name, std::move(key_defines)}).dump();
    program.id  = fmt::format("{:016x}", erhe::hash::hash(program.key.data(), program.key.size()));
    bool stats_missing = false;
    for (const erhe::graphics::Shader_program_module& module : modules) {
        Shader_session_stage stage{
            .type                     = module.type,
            .cache_key                = std::string{module.cache_key},
            .module_bytes             = module.spirv.size() * sizeof(unsigned int),
            .module_instructions      = erhe::graphics::count_spirv_instructions(module.spirv),
            .stats_known              = false,
            .unoptimized_bytes        = 0,
            .unoptimized_instructions = 0,
            .optimized_bytes          = 0,
            .optimized_instructions   = 0
        };
        // Stats computed in an earlier session (with this SPIRV-Tools) are read; the others are queued.
        if (!stage.cache_key.empty()) {
            const std::optional<erhe::graphics::Spirv_cache_module_stats> stats = m_device.get_spirv_cache().get_module_stats(stage.cache_key);
            if (stats.has_value() && (stats->spirv_tools_version == m_spirv_tools_version)) {
                stage.stats_known              = true;
                stage.unoptimized_bytes        = stats->unoptimized_bytes;
                stage.unoptimized_instructions = stats->unoptimized_instructions;
                stage.optimized_bytes          = stats->optimized_bytes;
                stage.optimized_instructions   = stats->optimized_instructions;
            } else {
                stats_missing = true;
            }
        }
        program.stages.push_back(std::move(stage));
    }
    const std::string program_key = program.key;

    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        // A program built again (same key) replaces its entry and keeps its index.
        bool replaced = false;
        for (Shader_session_program& existing : m_programs) {
            if (existing.key == program.key) {
                // Same key, same modules: the analysis stays.
                program.index           = existing.index;
                program.analysis        = std::move(existing.analysis);
                program.analysis_queued = existing.analysis_queued;
                existing                = std::move(program);
                replaced      = true;
                break;
            }
        }
        if (!replaced) {
            program.index = m_programs.size();
            m_programs.push_back(std::move(program));
        }
        if (stats_missing && !m_stopping && (std::find(m_stats_jobs.begin(), m_stats_jobs.end(), program_key) == m_stats_jobs.end())) {
            m_stats_jobs.push_back(program_key);
            post_kick_locked();
        }
    }
    // Listeners and the change callback take their own locks and never call
    // back into this class; removing one waits for a call in progress.
    const std::lock_guard<std::mutex> lock{m_listener_mutex};
    for (const Listener& listener : m_listeners) {
        listener.observer(create_info, modules);
    }
    if (m_change_callback) {
        m_change_callback();
    }
}

} // namespace editor
