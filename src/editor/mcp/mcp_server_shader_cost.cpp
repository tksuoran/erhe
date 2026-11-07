// Shader cost tools (doc/plans/shader_cost.md): fragment invocations per
// pipeline of one frame (A2).

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"
#include "app_context.hpp"
#include "renderers/shader_analytics.hpp"
#if defined(ERHE_SPIRV)
#include "renderers/shader_analytics_service.hpp"
#include "renderers/shader_session_programs.hpp"
#include "windows/shader_variants_window.hpp"
#endif

#include "erhe_file/file.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/fragment_invocation_capture.hpp"
#include "erhe_graphics/generated/graphics_config.hpp"
#if defined(ERHE_SPIRV)
#include "erhe_graphics/shader_variant_report.hpp"
#endif

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace editor {

using json = nlohmann::json;
using namespace mcp_server_detail;

namespace {

using Defines = std::vector<std::pair<std::string, std::string>>;

// The key a program is matched by in the shader variant browser:
// JSON.stringify([name, defines]) of index.html's rows, so a capture
// matches a browser recorded by another session.
[[nodiscard]] auto make_program_key(const std::string& program, const Defines& defines) -> std::string
{
    json key_defines = json::array();
    for (const auto& [name, value] : defines) {
        key_defines.push_back(json::array({name, value}));
    }
    return json::array({program, std::move(key_defines)}).dump();
}

class Program_total
{
public:
    std::string           program;
    Defines               defines;
    uint64_t              invocations{0};
    uint32_t              ranges     {0};
    std::set<std::string> pipelines;
};

class Pipeline_total
{
public:
    std::string pipeline;
    std::string program;
    std::string key;
    uint64_t    invocations{0};
    uint32_t    ranges     {0};
};

[[nodiscard]] auto defines_to_json(const Defines& defines) -> json
{
    json result = json::array();
    for (const auto& [name, value] : defines) {
        result.push_back(json::array({name, value}));
    }
    return result;
}

} // anonymous namespace

auto Mcp_server::query_fragment_invocations(const json& args) -> std::string
{
    // The first pass requests a capture of the next frame and defers; the
    // re-runs poll until that frame retired.
    const bool continuation =
        (m_fragment_capture_request != nullptr) &&
        (m_fragment_capture_request == m_current_request) &&
        (m_fragment_capture_enqueued_at == m_current_request->enqueued_at);
    erhe::graphics::Device* const device = m_context.graphics_device;
    if (device == nullptr) {
        m_fragment_capture_request = nullptr;
        return make_error_content("capture_fragment_invocations: no graphics device");
    }
    if (!continuation) {
        if (!device->get_info().pipeline_statistics_query) {
            return make_error_content("capture_fragment_invocations: the device has no pipeline statistics queries (Vulkan pipelineStatisticsQuery and hostQueryReset)");
        }
        if (!device->request_fragment_invocation_capture()) {
            return make_error_content("capture_fragment_invocations: a capture is already in flight");
        }
        m_fragment_capture_request     = m_current_request;
        m_fragment_capture_enqueued_at = m_current_request->enqueued_at;
        m_defer_current_request        = true;
        return {};
    }
    std::optional<erhe::graphics::Fragment_invocation_capture> capture = device->take_fragment_invocation_capture();
    if (!capture.has_value()) {
        m_defer_current_request = true;
        return {};
    }
    m_fragment_capture_request = nullptr;

    const bool        include_defines = args.value("include_defines", false);
    const std::size_t top             = args.value("top", std::size_t{50});

    uint64_t                              total_invocations = 0;
    std::map<std::string, Program_total>  programs;
    std::map<std::string, Pipeline_total> pipelines;
    for (const erhe::graphics::Fragment_invocation_range& range : capture->ranges) {
        total_invocations += range.invocations;
        const std::string key = make_program_key(range.program, range.defines);
        Program_total& program_total = programs[key];
        if (program_total.ranges == 0) {
            program_total.program = range.program;
            program_total.defines = range.defines;
        }
        program_total.invocations += range.invocations;
        program_total.ranges      += 1;
        program_total.pipelines.insert(range.pipeline);

        Pipeline_total& pipeline_total = pipelines[range.pipeline + "\n" + key];
        if (pipeline_total.ranges == 0) {
            pipeline_total.pipeline = range.pipeline;
            pipeline_total.program  = range.program;
            pipeline_total.key      = key;
        }
        pipeline_total.invocations += range.invocations;
        pipeline_total.ranges      += 1;
    }

#if defined(ERHE_SPIRV)
    erhe::graphics::Shader_variant_report* const report = device->get_shader_variant_report();
#endif
    const auto share = [total_invocations](const uint64_t invocations) {
        return (total_invocations > 0) ? static_cast<double>(invocations) / static_cast<double>(total_invocations) : 0.0;
    };

    std::vector<const Program_total*> sorted_programs;
    for (const auto& [key, program_total] : programs) {
        sorted_programs.push_back(&program_total);
    }
    std::sort(sorted_programs.begin(), sorted_programs.end(), [](const Program_total* a, const Program_total* b) { return a->invocations > b->invocations; });
    json program_list = json::array();
    for (const Program_total* program_total : sorted_programs) {
        if ((top > 0) && (program_list.size() >= top)) {
            break;
        }
        json entry{
            {"program",     program_total->program},
            {"invocations", program_total->invocations},
            {"share",       share(program_total->invocations)},
            {"ranges",      program_total->ranges},
            {"pipelines",   program_total->pipelines}
        };
#if defined(ERHE_SPIRV)
        if (report != nullptr) {
            const std::optional<std::string> id = report->find_program_id(program_total->program, program_total->defines);
            entry["browser_id"] = id.has_value() ? json(id.value()) : json(nullptr);
        }
#endif
        if (include_defines) {
            entry["defines"] = defines_to_json(program_total->defines);
        }
        program_list.push_back(std::move(entry));
    }

    std::vector<const Pipeline_total*> sorted_pipelines;
    for (const auto& [key, pipeline_total] : pipelines) {
        sorted_pipelines.push_back(&pipeline_total);
    }
    std::sort(sorted_pipelines.begin(), sorted_pipelines.end(), [](const Pipeline_total* a, const Pipeline_total* b) { return a->invocations > b->invocations; });
    json pipeline_list = json::array();
    for (const Pipeline_total* pipeline_total : sorted_pipelines) {
        if ((top > 0) && (pipeline_list.size() >= top)) {
            break;
        }
        pipeline_list.push_back(json{
            {"pipeline",    pipeline_total->pipeline},
            {"program",     pipeline_total->program},
            {"invocations", pipeline_total->invocations},
            {"share",       share(pipeline_total->invocations)},
            {"ranges",      pipeline_total->ranges}
        });
    }

    json result{
        {"frame_id",          capture->frame_id},
        {"total_invocations", total_invocations},
        {"range_count",       capture->ranges.size()},
        {"dropped_ranges",    capture->dropped_ranges},
        {"program_count",     programs.size()},
        {"pipeline_count",    pipelines.size()},
        {"programs",          std::move(program_list)},
        {"pipelines",         std::move(pipeline_list)}
    };

    // The browser sidecar (doc/plans/shader_cost.md A2): every program, by key.
    std::string browser_directory = args.value("browser_directory", std::string{});
    if (browser_directory.empty()) {
        browser_directory = device->get_graphics_config().shader_variant_browser_directory;
    }
    if (!browser_directory.empty()) {
        json sidecar_programs = json::object();
        for (const auto& [key, program_total] : programs) {
            sidecar_programs[key] = json{
                {"invocations", program_total.invocations},
                {"ranges",      program_total.ranges}
            };
        }
        const json sidecar{
            {"schema",            {{"name", "erhe_shader_fragments"}, {"version", 1}}},
            {"frame_id",          capture->frame_id},
            {"total_invocations", total_invocations},
            {"programs",          std::move(sidecar_programs)}
        };
        std::string text = sidecar.dump(1);
        // '<' escaped so no "</script>" ends the element loading the file; "<" decodes to '<'.
        std::string escaped;
        escaped.reserve(text.size());
        for (const char c : text) {
            if (c == '<') {
                escaped += "\\u003c";
            } else {
                escaped += c;
            }
        }
        const std::filesystem::path path = std::filesystem::path{browser_directory} / "fragments.js";
        const bool written = erhe::file::write_file_via_temp(path, "window.erhe_shader_fragments = " + escaped + ";\n");
        result["sidecar"] = written ? json(path.generic_string()) : json(nullptr);
#if defined(ERHE_SPIRV)
        if (written && (m_context.shader_variants_window != nullptr)) {
            m_context.shader_variants_window->request_reload();
        }
#endif
        if (!written) {
            result["sidecar_error"] = fmt::format("could not write {}", path.generic_string());
        }
    }
    return make_json_content(result).dump();
}

auto Mcp_server::action_load_shader_analytics(const json& args) -> std::string
{
    // doc/plans/shader_cost.md B1: the analytics the cost overdraw mode weighs by.
    Shader_analytics* const analytics = m_context.shader_analytics;
    if (analytics == nullptr) {
        return make_error_content("load_shader_analytics: no shader analytics in this editor");
    }
    const std::string directory = args.value("directory", std::string{});
    if (!directory.empty()) {
        const std::string error = analytics->load(directory);
        if (!error.empty()) {
            return make_error_content("load_shader_analytics: " + error);
        }
    }
    // The background analysis (doc/editor/shader_analytics.md); null when off.
    json service = nullptr;
#if defined(ERHE_SPIRV)
    if (m_context.shader_analytics_service != nullptr) {
        const Shader_analytics_service::Status status = m_context.shader_analytics_service->get_status();
        service = json{
            {"mali_core",    status.mali_core},
            {"powervr_core", status.powervr_core},
            {"queued",       status.queued},
            {"running",      status.running},
            {"analyzed",     status.analyzed},
            {"failed",       status.failed}
        };
    }
#endif
    if (!analytics->is_loaded()) {
        return make_json_content(json{{"loaded", false}, {"service", std::move(service)}}).dump();
    }
    json metrics = json::array();
    for (std::size_t i = 0; i < c_shader_cost_metric_count; ++i) {
        const Shader_cost_metric metric = static_cast<Shader_cost_metric>(i);
        if (!analytics->has_metric(metric)) {
            continue;
        }
        metrics.push_back(json{
            {"metric",       c_str(metric)},
            {"title",        analytics->get_metric_title(metric)},
            {"core",         analytics->get_core(metric)},
            {"max_value",    analytics->get_max_metric(metric)},
            {"weight_scale", Shader_analytics::get_weight_scale(metric)}
        });
    }
    return make_json_content(json{
        {"loaded",           true},
        {"directory",        analytics->get_directory().generic_string()},
        {"program_count",    analytics->get_program_count()},
        {"metrics",          std::move(metrics)},
        {"selected_metric",  c_str(analytics->get_selected_metric())},
        {"missing_variants", analytics->get_missing_variant_count()},
        {"service",          std::move(service)}
    }).dump();
}


auto Mcp_server::action_analyze_shader_programs(const json& args) -> std::string
{
#if !defined(ERHE_SPIRV)
    static_cast<void>(args);
    return make_error_content("analyze_shader_programs: this build has no SPIR-V (ERHE_SPIRV off), so no session program list");
#else
    // doc/editor/shader_analytics.md "Session programs": the programs this
    // session built, analyzed by the offline compilers in background tasks.
    Shader_session_programs* const session_programs = m_context.shader_session_programs;
    if (session_programs == nullptr) {
        return make_error_content("analyze_shader_programs: no session program list in this editor");
    }
    const std::string analyze = args.value("analyze", std::string{"none"});
    if ((analyze != "none") && (analyze != "all") && (analyze != "listed")) {
        return make_error_content("analyze_shader_programs: 'analyze' must be \"none\", \"all\" or \"listed\"");
    }
    std::vector<std::string> program_ids;
    if (args.contains("program_ids")) {
        if (!args["program_ids"].is_array()) {
            return make_error_content("analyze_shader_programs: 'program_ids' must be an array of program ids");
        }
        for (const json& id : args["program_ids"]) {
            if (!id.is_string()) {
                return make_error_content("analyze_shader_programs: 'program_ids' must be an array of program ids");
            }
            program_ids.push_back(id.get<std::string>());
        }
    }
    std::vector<Shader_session_program> programs;
    session_programs->copy_programs(programs);
    const auto find_program = [&programs](const std::string& id) -> const Shader_session_program* {
        for (const Shader_session_program& program : programs) {
            if (program.id == id) {
                return &program;
            }
        }
        return nullptr;
    };
    for (const std::string& id : program_ids) {
        if (find_program(id) == nullptr) {
            return make_error_content(fmt::format("analyze_shader_programs: no session program with id {}", id));
        }
    }
    if (analyze == "all") {
        session_programs->request_analysis_all();
    } else if (analyze == "listed") {
        for (const std::string& id : program_ids) {
            session_programs->request_analysis(find_program(id)->key);
        }
    }
    const Shader_session_analysis_status status = session_programs->get_analysis_status();
    json listed = json::array();
    for (const std::string& id : program_ids) {
        const Shader_session_program& program = *find_program(id);
        listed.push_back(json{
            {"id",       program.id},
            {"name",     program.name},
            {"key",      program.key},
            {"analysis", json::parse(program.analysis.dump())}
        });
    }
    json all = json::array();
    for (const Shader_session_program& program : programs) {
        all.push_back(json{{"id", program.id}, {"name", program.name}, {"analyzed", !program.analysis.is_null()}});
    }
    return make_json_content(json{
        {"compilers_ready", status.compilers_ready},
        {"program_count",   status.program_count},
        {"analyzed",        status.analyzed},
        {"queued",          status.queued},
        {"programs",        std::move(all)},
        {"listed",          std::move(listed)}
    }).dump();
#endif
}

} // namespace editor
