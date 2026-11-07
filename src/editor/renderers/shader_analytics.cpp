#include "renderers/shader_analytics.hpp"
#include "editor_log.hpp"

#include "erhe_file/file.hpp"
#include "erhe_scene_renderer/shader_variant_cache.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace editor {

using json = nlohmann::json;

namespace {

constexpr const char* c_metric_names[] = {
    "mali_cycles",
    "mali_arithmetic_cycles",
    "mali_texture_cycles",
    "mali_load_store_cycles",
    "powervr_cycles",
    "powervr_instructions",
    "powervr_samples",
    "powervr_registers"
};
constexpr const char* c_metric_labels[] = {
    "cycles",
    "arithmetic cycles",
    "texture cycles",
    "load/store cycles",
    "cycles",
    "instructions",
    "samples",
    "temporary registers"
};
constexpr Offline_compiler c_metric_compilers[] = {
    Offline_compiler::mali,
    Offline_compiler::mali,
    Offline_compiler::mali,
    Offline_compiler::mali,
    Offline_compiler::powervr,
    Offline_compiler::powervr,
    Offline_compiler::powervr,
    Offline_compiler::powervr
};
static_assert(std::size(c_metric_names)     == c_shader_cost_metric_count);
static_assert(std::size(c_metric_labels)    == c_shader_cost_metric_count);
static_assert(std::size(c_metric_compilers) == c_shader_cost_metric_count);

constexpr double c_missing = std::numeric_limits<double>::quiet_NaN();

[[nodiscard]] auto read_json_file(const std::filesystem::path& path, const std::string_view js_prefix, std::string& error) -> std::optional<json>
{
    const std::optional<std::string> text = erhe::file::read("Shader_analytics::load", path);
    if (!text.has_value()) {
        error = fmt::format("cannot read {}", path.generic_string());
        return std::nullopt;
    }
    std::string_view body{text.value()};
    if (!js_prefix.empty()) {
        // analytics.js: "window.erhe_shader_analytics = {...};"
        const std::size_t start = body.find('{');
        const std::size_t end   = body.rfind('}');
        if (!body.starts_with(js_prefix) || (start == std::string_view::npos) || (end == std::string_view::npos)) {
            error = fmt::format("{} does not start with {}", path.generic_string(), js_prefix);
            return std::nullopt;
        }
        body = body.substr(start, end - start + 1);
    }
    json parsed = json::parse(body, nullptr, false);
    if (parsed.is_discarded()) {
        error = fmt::format("{} is not valid JSON", path.generic_string());
        return std::nullopt;
    }
    return parsed;
}

// The fragment stage summary of a program's core block: "after" when the
// recorded run optimized (the module the device ran), else "before".
[[nodiscard]] auto find_fragment_summary(const json& core_block, const std::string& program_id, const char* slot) -> const json*
{
    const auto programs = core_block.find("programs");
    if ((programs == core_block.end()) || !programs->contains(program_id)) {
        return nullptr;
    }
    const json& program = (*programs)[program_id];
    const auto stage = program.find("frag");
    if ((stage == program.end()) || !stage->contains(slot) || !(*stage)[slot].is_object()) {
        return nullptr;
    }
    const json& summary = (*stage)[slot];
    if (summary.contains("error") && !summary["error"].is_null()) {
        return nullptr;
    }
    return &summary;
}

[[nodiscard]] auto find_main_variant(const json& summary) -> const json*
{
    const auto variants = summary.find("variants");
    if ((variants == summary.end()) || !variants->is_array()) {
        return nullptr;
    }
    for (const json& variant : *variants) {
        if (variant.value("name", std::string{}) == "Main") {
            return &variant;
        }
    }
    return nullptr;
}

[[nodiscard]] auto number_or_missing(const json& value) -> double
{
    return value.is_number() ? value.get<double>() : c_missing;
}

void set_metric(Shader_cost_metrics& metrics, const Shader_cost_metric metric, const double value)
{
    metrics[static_cast<std::size_t>(metric)] = value;
}

} // anonymous namespace

auto c_str(const Offline_compiler compiler) -> const char*
{
    switch (compiler) {
        case Offline_compiler::mali:    return "Mali";
        case Offline_compiler::powervr: return "PowerVR";
        default:                        return "?";
    }
}

auto Offline_compiler_cores::get(const Offline_compiler compiler) const -> const std::string&
{
    switch (compiler) {
        case Offline_compiler::mali: return mali;
        default:                     return powervr;
    }
}

auto Offline_compiler_cores::get(const Offline_compiler compiler) -> std::string&
{
    switch (compiler) {
        case Offline_compiler::mali: return mali;
        default:                     return powervr;
    }
}

auto Offline_compiler_cores::empty() const -> bool
{
    return mali.empty() && powervr.empty();
}

auto c_str(const Shader_cost_metric metric) -> const char*
{
    const std::size_t index = static_cast<std::size_t>(metric);
    return (index < c_shader_cost_metric_count) ? c_metric_names[index] : "?";
}

auto get_label(const Shader_cost_metric metric) -> const char*
{
    const std::size_t index = static_cast<std::size_t>(metric);
    return (index < c_shader_cost_metric_count) ? c_metric_labels[index] : "?";
}

auto get_compiler(const Shader_cost_metric metric) -> Offline_compiler
{
    const std::size_t index = static_cast<std::size_t>(metric);
    return (index < c_shader_cost_metric_count) ? c_metric_compilers[index] : Offline_compiler::mali;
}

auto parse_shader_cost_metric(const std::string_view name) -> std::optional<Shader_cost_metric>
{
    for (std::size_t i = 0; i < c_shader_cost_metric_count; ++i) {
        if (name == c_metric_names[i]) {
            return static_cast<Shader_cost_metric>(i);
        }
    }
    return std::nullopt;
}

Shader_analytics::Shader_analytics(erhe::scene_renderer::Shader_variant_cache& shader_variant_cache)
    : m_shader_variant_cache{shader_variant_cache}
{
}

auto Shader_analytics::get_weight_scale(const Shader_cost_metric metric) -> double
{
    // Mali cycle counts are fractional; PowerVR counts are integers.
    return (get_compiler(metric) == Offline_compiler::mali) ? 0.01 : 1.0;
}

auto Shader_analytics::load(const std::filesystem::path& directory) -> std::string
{
    std::string error;
    std::optional<Shader_analytics_data> data = read(directory, error);
    if (!data.has_value()) {
        return error;
    }
    apply(std::move(data.value()));
    return {};
}

auto Shader_analytics::read(const std::filesystem::path& directory, std::string& error) -> std::optional<Shader_analytics_data>
{
    const std::optional<json> manifest  = read_json_file(directory / "variants" / "manifest.json", {}, error);
    if (!manifest.has_value()) {
        return std::nullopt;
    }
    const std::optional<json> analytics = read_json_file(directory / "analytics.js", "window.erhe_shader_analytics", error);
    if (!analytics.has_value()) {
        error += " (run scripts/shader_analytics.py on the directory)";
        return std::nullopt;
    }
    const json& cores = (*analytics)["cores"];
    const json  default_cores = analytics->value("default_cores", json::object());
    Offline_compiler_cores analytics_cores{
        .mali    = default_cores.value("malioc",  std::string{}),
        .powervr = default_cores.value("powervr", std::string{})
    };
    if (analytics_cores.empty()) {
        error = "analytics.js names no Mali or PowerVR core (schema 2 default_cores)";
        return std::nullopt;
    }
    const std::string& mali_core    = analytics_cores.mali;
    const std::string& powervr_core = analytics_cores.powervr;
    const bool  optimized = manifest->value("tools", json::object()).value("spirv_optimization", false);
    const char* slot      = optimized ? "after" : "before";

    std::unordered_map<std::string, Metrics> programs;
    for (const json& program : (*manifest)["programs"]) {
        // Manifest schema 2 ids are strings, schema 1 ids numbers.
        const json&       id_value = program.contains("id") ? program["id"] : json{};
        const std::string id       = id_value.is_string() ? id_value.get<std::string>() : id_value.dump();
        const std::string key = json::array({program.value("name", std::string{}), program.value("defines", json::array())}).dump();
        Metrics metrics;
        metrics.fill(c_missing);
        if (!mali_core.empty() && cores.contains(mali_core)) {
            const json& core_block = cores[mali_core];
            const json* summary = find_fragment_summary(core_block, id, slot);
            const json* main    = (summary != nullptr) ? find_main_variant(*summary) : nullptr;
            if ((main != nullptr) && (*main)["total"].is_array()) {
                const json& pipelines = core_block.value("pipelines", json::array());
                const json& total     = (*main)["total"];
                const auto cycles_of = [&](const std::string_view pipeline) -> double {
                    for (std::size_t i = 0; (i < pipelines.size()) && (i < total.size()); ++i) {
                        if (pipelines[i].get<std::string>() == pipeline) {
                            return number_or_missing(total[i]);
                        }
                    }
                    return c_missing;
                };
                double max_cycles = c_missing;
                for (const json& value : total) {
                    if (value.is_number()) {
                        max_cycles = std::isnan(max_cycles) ? value.get<double>() : std::max(max_cycles, value.get<double>());
                    }
                }
                metrics[static_cast<std::size_t>(Shader_cost_metric::mali_cycles)]            = max_cycles;
                metrics[static_cast<std::size_t>(Shader_cost_metric::mali_arithmetic_cycles)] = cycles_of("arith_total");
                metrics[static_cast<std::size_t>(Shader_cost_metric::mali_texture_cycles)]    = cycles_of("texture");
                metrics[static_cast<std::size_t>(Shader_cost_metric::mali_load_store_cycles)] = cycles_of("load_store");
            }
        }
        if (!powervr_core.empty() && cores.contains(powervr_core)) {
            const json* summary = find_fragment_summary(cores[powervr_core], id, slot);
            const json* main    = (summary != nullptr) ? find_main_variant(*summary) : nullptr;
            if (main != nullptr) {
                set_metric(metrics, Shader_cost_metric::powervr_cycles,       number_or_missing(main->value("cycles",         json{})));
                set_metric(metrics, Shader_cost_metric::powervr_instructions, number_or_missing(main->value("instructions",   json{})));
                set_metric(metrics, Shader_cost_metric::powervr_samples,      number_or_missing(main->value("samples",        json{})));
                set_metric(metrics, Shader_cost_metric::powervr_registers,    number_or_missing(main->value("temp_registers", json{})));
            }
        }
        programs[key] = metrics;
    }

    return Shader_analytics_data{
        .directory = directory,
        .cores     = std::move(analytics_cores),
        .modules   = slot,
        .programs  = std::move(programs)
    };
}

void Shader_analytics::apply(Shader_analytics_data&& data)
{
    m_directory = std::move(data.directory);
    m_cores     = std::move(data.cores);
    m_programs  = std::move(data.programs);
    log_render->info(
        "Shader analytics: {} programs from {} (Mali core '{}', PowerVR core '{}', {} modules)",
        m_programs.size(), m_directory.generic_string(), m_cores.mali, m_cores.powervr, data.modules
    );
    if (!has_metric(m_selected_metric)) {
        for (std::size_t i = 0; i < c_shader_cost_metric_count; ++i) {
            if (has_metric(static_cast<Shader_cost_metric>(i))) {
                m_selected_metric = static_cast<Shader_cost_metric>(i);
                break;
            }
        }
    }
    install_weights();
}

void Shader_analytics::add_programs(
    const Offline_compiler_cores&                               cores,
    std::vector<std::pair<std::string, Shader_cost_metrics>>&& programs
)
{
    for (const Offline_compiler compiler : {Offline_compiler::mali, Offline_compiler::powervr}) {
        if (m_cores.get(compiler).empty()) {
            m_cores.get(compiler) = cores.get(compiler);
        }
    }
    for (auto& [key, metrics] : programs) {
        for (std::size_t i = 0; i < c_shader_cost_metric_count; ++i) {
            const Offline_compiler compiler = get_compiler(static_cast<Shader_cost_metric>(i));
            if (m_cores.get(compiler) != cores.get(compiler)) {
                metrics[i] = c_missing;
            }
        }
        m_programs.insert_or_assign(std::move(key), metrics);
    }
    if (!has_metric(m_selected_metric)) {
        for (std::size_t i = 0; i < c_shader_cost_metric_count; ++i) {
            if (has_metric(static_cast<Shader_cost_metric>(i))) {
                m_selected_metric = static_cast<Shader_cost_metric>(i);
                break;
            }
        }
    }
    // Weights already resolved as 0 for these programs resolve again.
    install_weights();
}

auto Shader_analytics::get_core(const Shader_cost_metric metric) const -> const std::string&
{
    return m_cores.get(get_compiler(metric));
}

auto Shader_analytics::has_metric(const Shader_cost_metric metric) const -> bool
{
    return is_loaded() && !get_core(metric).empty();
}

auto Shader_analytics::get_metric_title(const Shader_cost_metric metric) const -> std::string
{
    // "PowerVR-BXM-8-256" -> "PowerVR BXM-8-256": the vendor prefix's dash only.
    std::string core = get_core(metric);
    if (const std::size_t dash = core.find('-'); dash != std::string::npos) {
        core[dash] = ' ';
    }
    return core.empty() ? std::string{get_label(metric)} : fmt::format("{} {}", core, get_label(metric));
}

auto Shader_analytics::find_fragment_metric(
    const std::string_view                                  program,
    const std::vector<std::pair<std::string, std::string>>& variant_defines,
    const Shader_cost_metric                                metric
) const -> std::optional<double>
{
    json key_defines = json::array();
    for (const auto& [name, value] : variant_defines) {
        key_defines.push_back(json::array({name, value}));
    }
    const auto it = m_programs.find(json::array({std::string{program}, std::move(key_defines)}).dump());
    if (it == m_programs.end()) {
        return std::nullopt;
    }
    const double value = it->second[static_cast<std::size_t>(metric)];
    if (std::isnan(value)) {
        return std::nullopt;
    }
    return value;
}

auto Shader_analytics::get_max_metric(const Shader_cost_metric metric) const -> double
{
    double result = 0.0;
    for (const auto& [key, metrics] : m_programs) {
        const double value = metrics[static_cast<std::size_t>(metric)];
        if (!std::isnan(value)) {
            result = std::max(result, value);
        }
    }
    return result;
}

void Shader_analytics::select_metric(const Shader_cost_metric metric)
{
    m_selected_metric = metric;
    install_weights();
}

auto get_default_shader_cost_max(const Shader_analytics& analytics, const Shader_cost_metric metric) -> uint32_t
{
    const double max_weight = analytics.get_max_metric(metric) / Shader_analytics::get_weight_scale(metric);
    return std::max(uint32_t{1}, static_cast<uint32_t>(std::lround(4.0 * max_weight)));
}

void Shader_analytics::install_weights()
{
    m_missing_variant_count = 0;
    m_shader_variant_cache.set_overdraw_weight_provider(
        [this](const std::string_view program, const std::vector<std::pair<std::string, std::string>>& variant_defines) -> uint32_t {
            const std::optional<double> value = find_fragment_metric(program, variant_defines, m_selected_metric);
            if (!value.has_value()) {
                ++m_missing_variant_count;
                // Common while background analysis catches up; the viewport shows the count.
                log_render->debug("Shader cost: no analytics for a {} variant ({} defines); it weighs 0", program, variant_defines.size());
                return 0u;
            }
            return static_cast<uint32_t>(std::lround(value.value() / get_weight_scale(m_selected_metric)));
        }
    );
}

} // namespace editor
