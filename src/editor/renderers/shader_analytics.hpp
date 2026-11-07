#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace erhe::scene_renderer { class Shader_variant_cache; }

namespace editor {

enum class Offline_compiler : unsigned int
{
    mali = 0,
    powervr
};

// "Mali", "PowerVR"
[[nodiscard]] auto c_str(Offline_compiler compiler) -> const char*;

// The core each compiler analyzes for; empty: that compiler contributes nothing.
class Offline_compiler_cores
{
public:
    [[nodiscard]] auto get(Offline_compiler compiler) const -> const std::string&;
    [[nodiscard]] auto get(Offline_compiler compiler) -> std::string&;
    [[nodiscard]] auto empty() const -> bool;

    std::string mali;    // "Mali-G310"
    std::string powervr; // "PowerVR-BXM-8-256"
};

// The static fragment shader costs the cost overdraw mode weighs fragments by
// (doc/plans/shader_cost.md B1): Mali and PowerVR cycle metrics are cycles.
enum class Shader_cost_metric : unsigned int
{
    mali_cycles = 0,
    mali_arithmetic_cycles,
    mali_texture_cycles,
    mali_load_store_cycles,
    powervr_cycles,
    powervr_instructions,
    powervr_samples,
    powervr_registers,
    count
};
inline constexpr std::size_t c_shader_cost_metric_count = static_cast<std::size_t>(Shader_cost_metric::count);

// MCP and settings names ("mali_cycles", ...), and labels without the core.
[[nodiscard]] auto c_str       (Shader_cost_metric metric) -> const char*;
[[nodiscard]] auto get_label   (Shader_cost_metric metric) -> const char*;
[[nodiscard]] auto get_compiler(Shader_cost_metric metric) -> Offline_compiler;
[[nodiscard]] auto parse_shader_cost_metric(std::string_view name) -> std::optional<Shader_cost_metric>;

using Shader_cost_metrics = std::array<double, c_shader_cost_metric_count>; // NaN: not reported

// The analytics of one browser directory as read, before they are applied.
class Shader_analytics_data
{
public:
    std::filesystem::path                                directory;
    Offline_compiler_cores                               cores;
    std::string                                          modules; // "after" or "before"
    std::unordered_map<std::string, Shader_cost_metrics> programs; // by JSON [name, variant defines]
};

// The analytics of a shader variant browser directory (variants/manifest.json
// and analytics.js of scripts/shader_analytics.py), indexed by program name
// and variant defines, and the metric the cost overdraw mode shows. Main thread.
class Shader_analytics
{
public:
    explicit Shader_analytics(erhe::scene_renderer::Shader_variant_cache& shader_variant_cache);

    // Empty on success, else the reason; the previous analytics stay on failure.
    [[nodiscard]] auto load(const std::filesystem::path& directory) -> std::string;
    // Any thread, no editor state: read() before a recording session clears the directory, apply() later.
    [[nodiscard]] static auto read(const std::filesystem::path& directory, std::string& error) -> std::optional<Shader_analytics_data>;
    void apply(Shader_analytics_data&& data);

    // Main thread: results of Shader_analytics_service, merged by program key;
    // a compiler whose core differs from the loaded one contributes nothing.
    void add_programs(const Offline_compiler_cores& cores, std::vector<std::pair<std::string, Shader_cost_metrics>>&& programs);

    [[nodiscard]] auto is_loaded       () const -> bool { return !m_programs.empty(); }
    [[nodiscard]] auto get_directory   () const -> const std::filesystem::path& { return m_directory; }
    [[nodiscard]] auto get_program_count() const -> std::size_t { return m_programs.size(); }
    // The core of a metric's compiler ("Mali-G310"); empty when not loaded.
    [[nodiscard]] auto get_core        (Shader_cost_metric metric) const -> const std::string&;
    [[nodiscard]] auto has_metric      (Shader_cost_metric metric) const -> bool;
    // "Mali G310 cycles", "PowerVR BXM-8-256 cycles"
    [[nodiscard]] auto get_metric_title(Shader_cost_metric metric) const -> std::string;
    // The fragment stage metric of a recorded program; nullopt when not recorded.
    [[nodiscard]] auto find_fragment_metric(
        std::string_view                                        program,
        const std::vector<std::pair<std::string, std::string>>& variant_defines,
        Shader_cost_metric                                      metric
    ) const -> std::optional<double>;
    // The largest fragment value of a metric among the recorded programs.
    [[nodiscard]] auto get_max_metric  (Shader_cost_metric metric) const -> double;

    // One weight unit in metric units: weights are integers (cycles in hundredths).
    [[nodiscard]] static auto get_weight_scale(Shader_cost_metric metric) -> double;

    // The metric the cost variants are weighted by; installs the weights in
    // the shader variant cache (a change recompiles the cost variants).
    void select_metric(Shader_cost_metric metric);
    [[nodiscard]] auto get_selected_metric() const -> Shader_cost_metric { return m_selected_metric; }
    // The cost variants drawn whose OVERDRAW off twin the analytics lack (weight 0).
    [[nodiscard]] auto get_missing_variant_count() const -> std::size_t { return m_missing_variant_count; }

private:
    using Metrics = Shader_cost_metrics;

    void install_weights();

    erhe::scene_renderer::Shader_variant_cache& m_shader_variant_cache;
    std::filesystem::path                       m_directory;
    Offline_compiler_cores                      m_cores;
    std::unordered_map<std::string, Metrics>    m_programs; // by JSON [name, variant defines]
    Shader_cost_metric                          m_selected_metric{Shader_cost_metric::mali_cycles};
    std::size_t                                 m_missing_variant_count{0};
};

// The cost view's default white point, in weight units: four fragments of
// the costliest recorded variant.
[[nodiscard]] auto get_default_shader_cost_max(const Shader_analytics& analytics, Shader_cost_metric metric) -> uint32_t;

} // namespace editor
