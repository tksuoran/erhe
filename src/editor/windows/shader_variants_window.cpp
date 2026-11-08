#include "windows/shader_variants_window.hpp"
#include "renderers/shader_analytics_service.hpp"
#include "renderers/shader_session_programs.hpp"

#include "editor_log.hpp"

#include "erhe_file/file.hpp"
#include "erhe_graphics/generated/graphics_config.hpp"
#include "erhe_graphics/shader_variant_report.hpp"
#include "erhe_graphics/spirv_cache.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <optional>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace editor {

using ordered_json = nlohmann::ordered_json;

namespace {

constexpr std::size_t c_none = static_cast<std::size_t>(-1);
const ImVec4 c_good_color{0.36f, 0.76f, 0.46f, 1.0f};
const ImVec4 c_bad_color {0.94f, 0.44f, 0.40f, 1.0f};

// The HTML index's compact axis codes (shader_variant_report.cpp write_index()).
constexpr std::pair<std::string_view, std::string_view> c_compact_codes[] = {
    {"ERHE_USE_SKINNING", "Sk"}, {"ERHE_WORLD_SPACE_VERTICES", "Ws"},
    {"ERHE_USE_VERTEX_VARYING_NORMAL", "vN"}, {"ERHE_USE_VERTEX_VARYING_COLOR", "vC"}, {"ERHE_USE_VERTEX_VARYING_TANGENT", "vT"},
    {"ERHE_USE_VERTEX_VARYING_BITANGENT", "vB"}, {"ERHE_USE_VERTEX_VARYING_TEXCOORD0", "vU0"}, {"ERHE_USE_VERTEX_VARYING_TEXCOORD1", "vU1"},
    {"ERHE_USE_VERTEX_VARYING_TEXCOORD2", "vU2"}, {"ERHE_USE_VERTEX_VARYING_ANISO_CONTROL", "vA"}, {"ERHE_USE_VERTEX_VARYING_NODE_POSITION", "vP"},
    {"ERHE_LIGHT_COUNT_DIRECTIONAL_SHADOWMAPPED", "Ld"}, {"ERHE_LIGHT_COUNT_DIRECTIONAL_NOT_SHADOWMAPPED", "ld"},
    {"ERHE_LIGHT_COUNT_SPOT_SHADOWMAPPED", "Ls"}, {"ERHE_LIGHT_COUNT_SPOT_NOT_SHADOWMAPPED", "ls"},
    {"ERHE_LIGHT_COUNT_POINT_SHADOWMAPPED", "Lp"}, {"ERHE_LIGHT_COUNT_POINT_NOT_SHADOWMAPPED", "lp"},
    {"ERHE_LIGHT_SHADING_CLUSTERED", "LC"}, {"ERHE_USE_LIGHTMAP", "Lm"},
    {"ERHE_BXDF_MODEL", "B"}, {"ERHE_MATERIAL_BLENDING_MODE", "Bl"},
    {"ERHE_SHADOW_FILTER", "SF"}, {"ERHE_SHADOW_BIAS", "SB"}, {"ERHE_SHADOW_DEPTH_BITS", "SD"},
    {"ERHE_USE_CIRCULAR_BRUSHED_METAL", "Br"}, {"ERHE_IDENTITY_TEXTURE_TRANSFORM", "It"}, {"ERHE_CIRCULAR_BRUSHED_METAL_TEXGEN_MODE", "BrT"},
    {"ERHE_SPECULAR_ANTI_ALIASING", "SAA"}, {"ERHE_FP16_PATHS", "H"},
    {"ERHE_USE_BASE_COLOR_TEXTURE", "tB"}, {"ERHE_USE_METALLIC_ROUGHNESS_TEXTURE", "tM"}, {"ERHE_USE_NORMAL_TEXTURE", "tN"},
    {"ERHE_USE_OCCLUSION_TEXTURE", "tO"}, {"ERHE_USE_EMISSION_TEXTURE", "tE"},
    {"ERHE_SOLID_WIREFRAME", "Wf"}, {"ERHE_SHADER_DEBUG", "Dbg"}, {"ERHE_SHADER_MULTIVIEW_COUNT", "MV"}, {"ERHE_OVERDRAW", "OD"},
    {"ERHE_VARIANT_DEPTH_ONLY", "Dp"}, {"ERHE_VARIANT_POINTS", "Pt"}, {"ERHE_VARIANT_BRUSH_PREVIEW", "BP"},
    {"ERHE_VARIANT_SHADOW_DISTANCE", "SDi"}, {"ERHE_VARIANT_SHADOW_CUBE", "SCu"},
    {"SOURCE", "src"}, {"FIRST_PASS", "FP"}, {"LAST_PASS", "LP"}
};

[[nodiscard]] auto compact_name(std::string_view name) -> std::string
{
    for (const auto& [define, code] : c_compact_codes) {
        if (define == name) {
            return std::string{code};
        }
    }
    for (const std::string_view prefix : {std::string_view{"ERHE_"}, std::string_view{"USE_"}, std::string_view{"VARIANT_"}}) {
        if (name.starts_with(prefix)) {
            name.remove_prefix(prefix.size());
        }
    }
    std::string result;
    bool word_start = true;
    for (const char c : name) {
        if (c == '_') {
            word_start = true;
        } else if (word_start) {
            result += c;
            word_start = false;
        }
    }
    return result;
}

[[nodiscard]] auto is_integer_text(const std::string_view text) -> bool
{
    const std::string_view digits = text.starts_with('-') ? text.substr(1) : text;
    return !digits.empty() && std::all_of(digits.begin(), digits.end(), [](const char c) { return (c >= '0') && (c <= '9'); });
}

[[nodiscard]] auto to_lower(std::string_view text) -> std::string
{
    std::string result{text};
    for (char& c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

[[nodiscard]] auto format_number(const double value) -> std::string
{
    if ((value == std::floor(value)) && (std::abs(value) < 1.0e15)) {
        return fmt::format("{}", static_cast<long long>(value));
    }
    return fmt::format("{:.2f}", value);
}

[[nodiscard]] auto stage_title(const std::string_view stage) -> const char*
{
    if (stage == "vert") return "Vertex";
    if (stage == "frag") return "Fragment";
    if (stage == "comp") return "Compute";
    if (stage == "geom") return "Geometry";
    if (stage == "tesc") return "Tessellation control";
    if (stage == "tese") return "Tessellation evaluation";
    return "Stage";
}

[[nodiscard]] auto short_core(const std::string_view core) -> std::string
{
    if (core.starts_with("Mali-")) {
        return std::string{core.substr(5)};
    }
    if (core.starts_with("PowerVR-")) {
        return std::string{core.substr(8)};
    }
    return std::string{core};
}

// "Mali-G310" -> "Mali G310", "PowerVR-BXM-8-256" -> "PowerVR BXM-8-256": the vendor prefix's dash only.
[[nodiscard]] auto core_title(const std::string_view core) -> std::string
{
    std::string title{core};
    if (const std::size_t dash = title.find('-'); dash != std::string::npos) {
        title[dash] = ' ';
    }
    return title;
}

constexpr const char* c_vendor_names[] = { "Mali", "PowerVR" };

// JS-like truthiness lookups; null when absent.
[[nodiscard]] auto member(const ordered_json* object, const char* key) -> const ordered_json*
{
    if ((object == nullptr) || !object->is_object()) {
        return nullptr;
    }
    const auto i = object->find(key);
    if ((i == object->end()) || i->is_null()) {
        return nullptr;
    }
    return &(*i);
}

[[nodiscard]] auto has_error(const ordered_json* summary) -> bool
{
    return member(summary, "error") != nullptr;
}

[[nodiscard]] auto is_true(const ordered_json* object, const char* key) -> bool
{
    const ordered_json* value = member(object, key);
    return (value != nullptr) && value->is_boolean() && value->get<bool>();
}

[[nodiscard]] auto get_number(const ordered_json* value) -> std::optional<double>
{
    if ((value == nullptr) || !value->is_number()) {
        return std::nullopt;
    }
    return value->get<double>();
}

[[nodiscard]] auto array_item(const ordered_json* array, const std::size_t index) -> const ordered_json*
{
    if ((array == nullptr) || !array->is_array() || (index >= array->size()) || (*array)[index].is_null()) {
        return nullptr;
    }
    return &(*array)[index];
}

[[nodiscard]] auto find_variant(const ordered_json* variants, const std::string_view name) -> const ordered_json*
{
    if ((variants == nullptr) || !variants->is_array()) {
        return nullptr;
    }
    for (const ordered_json& variant : *variants) {
        const ordered_json* variant_name = member(&variant, "name");
        if ((variant_name != nullptr) && variant_name->is_string() && (variant_name->get_ref<const std::string&>() == name)) {
            return &variant;
        }
    }
    return nullptr;
}

[[nodiscard]] auto max_cycles(const ordered_json* variant) -> std::optional<double>
{
    const ordered_json* total = member(variant, "total");
    if ((total == nullptr) || !total->is_array()) {
        return std::nullopt;
    }
    double result = 0.0;
    for (const ordered_json& value : *total) {
        if (value.is_number()) {
            result = std::max(result, value.get<double>());
        }
    }
    return result;
}

void format_value(const ordered_json* value, char* buffer, const std::size_t buffer_size)
{
    if ((value == nullptr) || value->is_null()) {
        std::snprintf(buffer, buffer_size, "-");
    } else if (value->is_number()) {
        const double number = value->get<double>();
        if ((number == std::floor(number)) && (std::abs(number) < 1.0e15)) {
            std::snprintf(buffer, buffer_size, "%lld", static_cast<long long>(number));
        } else {
            std::snprintf(buffer, buffer_size, "%.2f", number);
        }
    } else if (value->is_boolean()) {
        std::snprintf(buffer, buffer_size, "%s", value->get<bool>() ? "true" : "false");
    } else if (value->is_string()) {
        std::snprintf(buffer, buffer_size, "%s", value->get_ref<const std::string&>().c_str());
    } else {
        std::snprintf(buffer, buffer_size, "?");
    }
}

// A label / before / after / delta row; sense 1: lower is better, -1: higher is better, 0: neither.
void compare_row(const char* label, const ordered_json* before, const ordered_json* after, const int sense)
{
    char buffer[64];
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    format_value(before, buffer, sizeof(buffer));
    ImGui::TextUnformatted(buffer);
    ImGui::TableSetColumnIndex(2);
    format_value(after, buffer, sizeof(buffer));
    ImGui::TextUnformatted(buffer);
    ImGui::TableSetColumnIndex(3);
    const std::optional<double> x = get_number(before);
    const std::optional<double> y = get_number(after);
    if (x.has_value() && y.has_value()) {
        const double d = y.value() - x.value();
        const ordered_json delta = d;
        format_value(&delta, buffer, sizeof(buffer));
        const char* const sign = (d > 0.0) ? "+" : "";
        if ((d == 0.0) || (sense == 0)) {
            ImGui::Text("%s%s", sign, buffer);
        } else {
            ImGui::TextColored(((d * sense) < 0.0) ? c_good_color : c_bad_color, "%s%s", sign, buffer);
        }
    }
}

[[nodiscard]] auto begin_compare_table(const char* id) -> bool
{
    if (!ImGui::BeginTable(id, 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        return false;
    }
    ImGui::TableSetupColumn("");
    ImGui::TableSetupColumn("Before");
    ImGui::TableSetupColumn("After");
    ImGui::TableSetupColumn("Delta");
    ImGui::TableHeadersRow();
    return true;
}

void error_lines(const ordered_json* before, const ordered_json* after)
{
    for (const auto& [label, summary] : {std::pair{"before", before}, std::pair{"after", after}}) {
        const ordered_json* error = member(summary, "error");
        if (error != nullptr) {
            char buffer[512];
            format_value(error, buffer, sizeof(buffer));
            if (!error->is_string()) {
                const std::string text = error->dump();
                std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
            }
            ImGui::PushStyleColor(ImGuiCol_Text, c_bad_color);
            ImGui::TextWrapped("%s: %s", label, buffer);
            ImGui::PopStyleColor();
        }
    }
}

void string_lines_node(const char* label, const ordered_json* first, const ordered_json* second)
{
    std::size_t count = 0;
    for (const ordered_json* lines : {first, second}) {
        if ((lines != nullptr) && lines->is_array()) {
            count += lines->size();
        }
    }
    if (count == 0) {
        return;
    }
    if (ImGui::TreeNode(label, "%s (%zu)", label, count)) {
        for (const ordered_json* lines : {first, second}) {
            if ((lines == nullptr) || !lines->is_array()) {
                continue;
            }
            for (const ordered_json& line : *lines) {
                if (line.is_string()) {
                    ImGui::TextWrapped("%s", line.get_ref<const std::string&>().c_str());
                } else {
                    const std::string text = line.dump();
                    ImGui::TextWrapped("%s", text.c_str());
                }
            }
        }
        ImGui::TreePop();
    }
}

void command_line(const char* label, const ordered_json* summary)
{
    const ordered_json* command = member(summary, "command");
    if ((command != nullptr) && command->is_string()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s: %s", label, command->get_ref<const std::string&>().c_str());
        ImGui::PopStyleColor();
    }
}

[[nodiscard]] auto read_spirv(const std::filesystem::path& path) -> std::optional<std::vector<unsigned int>>
{
    std::error_code error_code;
    if (!std::filesystem::is_regular_file(path, error_code)) {
        return std::nullopt;
    }
    const std::optional<std::string> bytes = erhe::file::read("Shader_variants_window", path);
    if (!bytes.has_value()) {
        return std::nullopt;
    }
    std::vector<unsigned int> words(bytes->size() / sizeof(unsigned int));
    std::memcpy(words.data(), bytes->data(), words.size() * sizeof(unsigned int));
    return words;
}

[[nodiscard]] auto read_text(const std::filesystem::path& path) -> std::optional<std::string>
{
    std::error_code error_code;
    if (!std::filesystem::is_regular_file(path, error_code)) {
        return std::nullopt;
    }
    return erhe::file::read("Shader_variants_window", path);
}

// analytics.js / fragments.js: "<js_prefix> = {...};"; null when absent or unreadable.
[[nodiscard]] auto read_js_sidecar(const std::filesystem::path& path, const std::string_view js_prefix) -> ordered_json
{
    const std::optional<std::string> text = read_text(path);
    if (!text.has_value()) {
        return nullptr;
    }
    std::string_view body{text.value()};
    const std::size_t start = body.find('{');
    const std::size_t end   = body.rfind('}');
    if (!body.starts_with(js_prefix) || (start == std::string_view::npos) || (end == std::string_view::npos)) {
        log_programs->warn("Shader variants window: {} does not start with {}", path.generic_string(), js_prefix);
        return nullptr;
    }
    ordered_json parsed = ordered_json::parse(body.substr(start, end - start + 1), nullptr, false);
    if (parsed.is_discarded()) {
        log_programs->warn("Shader variants window: {} is not valid JSON", path.generic_string());
        return nullptr;
    }
    return parsed;
}

} // anonymous namespace

void Shader_variants_window::Text_block::set(std::string new_text)
{
    text = std::move(new_text);
    line_starts.clear();
    line_starts.push_back(0);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((text[i] == '\n') && ((i + 1) < text.size())) {
            line_starts.push_back(static_cast<std::uint32_t>(i + 1));
        }
    }
}

void Shader_variants_window::Text_block::imgui(const char* label, const bool default_open) const
{
    if (!ImGui::CollapsingHeader(label, default_open ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None)) {
        return;
    }
    ImGui::PushID(label);
    if (ImGui::SmallButton("Copy")) {
        ImGui::SetClipboardText(text.c_str());
    }
    ImGui::PopID();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(line_starts.size()));
    while (clipper.Step()) {
        for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line) {
            const std::size_t begin = line_starts[static_cast<std::size_t>(line)];
            std::size_t end = ((static_cast<std::size_t>(line) + 1) < line_starts.size()) ? line_starts[static_cast<std::size_t>(line) + 1] : text.size();
            if ((end > begin) && (text[end - 1] == '\n')) {
                --end;
            }
            ImGui::TextUnformatted(text.data() + begin, text.data() + end);
        }
    }
    ImGui::Spacing();
}

Shader_variants_window::Shader_variants_window(
    erhe::imgui::Imgui_renderer&                 imgui_renderer,
    erhe::imgui::Imgui_windows&                  imgui_windows,
    erhe::graphics::Shader_variant_report* const report,
    Shader_session_programs* const               session_programs,
    const Shader_analytics_service* const        shader_analytics_service
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Shader Variants", "shader_variants", true}
    , m_report          {report}
    , m_session_programs        {session_programs}
    , m_shader_analytics_service{shader_analytics_service}
{
    set_min_size(480.0f, 240.0f);
    set_initial_placement("", 0.8f, 0.8f);
    if (m_report != nullptr) {
        m_directory       = m_report->get_directory();
        m_directory_input = m_directory.generic_string();
        m_report->set_add_callback([this]() { request_reload(); });
    }
    // Change-driven: the session list reloads when a program builds, shown or not.
    if (m_session_programs != nullptr) {
        m_session_programs->set_change_callback([this]() {
            const std::lock_guard<std::mutex> lock{m_reload_mutex};
            if (m_showing_session) {
                m_reload_requested = true;
            }
        });
    }
}

Shader_variants_window::~Shader_variants_window() noexcept
{
    if (m_report != nullptr) {
        m_report->set_add_callback({});
    }
    if (m_session_programs != nullptr) {
        m_session_programs->set_change_callback({});
    }
}

void Shader_variants_window::request_reload()
{
    std::lock_guard<std::mutex> lock{m_reload_mutex};
    m_reload_requested = true;
}

auto Shader_variants_window::load_manifest(std::string& error) -> bool
{
    const std::filesystem::path path = m_directory / "variants" / "manifest.json";
    const std::optional<std::string> text = read_text(path);
    if (!text.has_value()) {
        error = fmt::format("No {} (it is written when the first program is recorded)", path.generic_string());
        return false;
    }
    const ordered_json manifest = ordered_json::parse(text.value(), nullptr, false);
    if (manifest.is_discarded() || !manifest.contains("programs") || !manifest["programs"].is_array()) {
        error = fmt::format("{} is not a shader variant manifest", path.generic_string());
        return false;
    }
    for (const ordered_json& entry : manifest["programs"]) {
        Program program;
        // Manifest schema 2: string id + index; schema 1: the number is both.
        const ordered_json* id = member(&entry, "id");
        program.id    = (id == nullptr) ? std::string{} : (id->is_string() ? id->get<std::string>() : id->dump());
        program.index = entry.value("index", (id != nullptr) && id->is_number_unsigned() ? id->get<std::size_t>() : m_programs.size());
        program.name = entry.value("name", std::string{});
        const ordered_json* value_names = member(&entry, "define_value_names");
        ordered_json key_defines = ordered_json::array();
        if (const ordered_json* defines = member(&entry, "defines"); (defines != nullptr) && defines->is_array()) {
            for (const ordered_json& define : *defines) {
                if (!define.is_array() || (define.size() < 2)) {
                    continue;
                }
                Define d{
                    .name       = define[0].get<std::string>(),
                    .value      = define[1].get<std::string>(),
                    .value_name = {}
                };
                if (const ordered_json* value_name = member(value_names, d.name.c_str()); (value_name != nullptr) && value_name->is_string()) {
                    d.value_name = value_name->get<std::string>();
                }
                key_defines.push_back(ordered_json::array({d.name, d.value}));
                program.defines.push_back(std::move(d));
            }
        }
        program.fragments_key = ordered_json::array({program.name, std::move(key_defines)}).dump();
        if (const ordered_json* includes = member(&entry, "includes"); (includes != nullptr) && includes->is_array()) {
            for (const ordered_json& include : *includes) {
                program.includes.push_back(include.get<std::string>());
            }
        }
        if (const ordered_json* stages = member(&entry, "stages"); (stages != nullptr) && stages->is_array()) {
            for (const ordered_json& s : *stages) {
                const auto string_or_empty = [&s](const char* key) -> std::string {
                    const ordered_json* v = member(&s, key);
                    return ((v != nullptr) && v->is_string()) ? v->get<std::string>() : std::string{};
                };
                program.stages.push_back(Stage{
                    .type                        = string_or_empty("type"),
                    .main_name                   = string_or_empty("main_name"),
                    .source                      = string_or_empty("source"),
                    .preamble                    = string_or_empty("preamble"),
                    .spirv                       = string_or_empty("spirv"),
                    .optimized_spirv             = string_or_empty("optimized_spirv"),
                    .spirv_bytes                 = s.value("spirv_bytes",                 std::size_t{0}),
                    .optimized_spirv_bytes       = s.value("optimized_spirv_bytes",       std::size_t{0}),
                    .instruction_count           = s.value("instruction_count",           std::size_t{0}),
                    .optimized_instruction_count = s.value("optimized_instruction_count", std::size_t{0}),
                    .cache_key                   = {},
                    .shader_type                 = erhe::graphics::Shader_type::vertex_shader,
                    .spirv_known                 = true,
                    .optimized_spirv_known       = true
                });
            }
        }
        m_programs.push_back(std::move(program));
    }
    return true;
}

void Shader_variants_window::load_session()
{
    const Shader_session_analysis_status analysis_status = m_session_programs->get_analysis_status();
    m_session_analysis_info = !analysis_status.compilers_ready
        ? ((m_shader_analytics_service == nullptr)
            ? std::string{"Analysis needs the background shader analytics (editor settings shader_analytics.enabled) and an offline compiler"}
            : std::string{"Analysis starts once the offline compilers are selected"})
        : fmt::format(
            "This session's programs: {} of {} analyzed{}; selecting a program analyzes it",
            analysis_status.analyzed, analysis_status.program_count,
            (analysis_status.queued > 0) ? fmt::format(", {} queued", analysis_status.queued) : std::string{}
        );
    // One column group per offline compiler the background analysis uses.
    const Shader_analytics_service::Status status = (m_shader_analytics_service != nullptr)
        ? m_shader_analytics_service->get_status()
        : Shader_analytics_service::Status{};
    m_mali_core    = status.mali_core;
    m_powervr_core = status.powervr_core;
    m_session_programs->copy_programs(m_session_scratch);

    // The analyzed programs' results in the analytics.js shape: per core its
    // block, with the programs by id (doc/erhe/graphics.md "Mali analytics").
    ordered_json cores = ordered_json::object();
    for (Shader_session_program& session_program : m_session_scratch) {
        if (!session_program.analysis.is_object()) {
            continue;
        }
        for (auto& [core_name, entry] : session_program.analysis.items()) {
            if (!cores.contains(core_name)) {
                ordered_json block = entry.contains("block") ? entry["block"] : ordered_json::object();
                block["programs"] = ordered_json::object();
                cores[core_name] = std::move(block);
            }
            if (entry.contains("program")) {
                cores[core_name]["programs"][session_program.id] = std::move(entry["program"]);
            }
        }
    }
    m_analytics = ordered_json{
        {"default_cores", ordered_json{{"malioc", m_mali_core}, {"powervr", m_powervr_core}}},
        {"cores",         std::move(cores)}
    };
    const bool optimized = m_session_programs->get_device().get_graphics_config().spirv_optimization;
    for (const Shader_session_program& session_program : m_session_scratch) {
        Program program;
        program.id    = session_program.id;
        program.index = session_program.index;
        program.name  = session_program.name;
        program.fragments_key = session_program.key;
        for (const auto& [name, value] : session_program.defines) {
            Define define{.name = name, .value = value, .value_name = {}};
            for (const auto& [value_name_define, value_name] : session_program.define_value_names) {
                if (value_name_define == name) {
                    define.value_name = value_name;
                }
            }
            program.defines.push_back(std::move(define));
        }
        const auto detail = m_session_details.find(program.id);
        if (detail != m_session_details.end()) {
            program.stages   = detail->second.stages;
            program.includes = detail->second.includes;
        } else {
            // Both modules' sizes once the background stats arrived; until
            // then only the module the device uses is known.
            for (const Shader_session_stage& session_stage : session_program.stages) {
                const char* const type = erhe::graphics::get_shader_variant_stage_short_name(session_stage.type);
                const bool known = session_stage.stats_known;
                program.stages.push_back(Stage{
                    .type                        = (type != nullptr) ? std::string{type} : std::string{"?"},
                    .main_name                   = {},
                    .source                      = {},
                    .preamble                    = {},
                    .spirv                       = {},
                    .optimized_spirv             = {},
                    .spirv_bytes                 = known ? session_stage.unoptimized_bytes        : (optimized ? 0 : session_stage.module_bytes),
                    .optimized_spirv_bytes       = known ? session_stage.optimized_bytes          : (optimized ? session_stage.module_bytes : 0),
                    .instruction_count           = known ? session_stage.unoptimized_instructions : (optimized ? 0 : session_stage.module_instructions),
                    .optimized_instruction_count = known ? session_stage.optimized_instructions   : (optimized ? session_stage.module_instructions : 0),
                    .cache_key                   = session_stage.cache_key,
                    .shader_type                 = session_stage.type,
                    .spirv_known                 = known || !optimized,
                    .optimized_spirv_known       = known || optimized
                });
            }
        }
        m_programs.push_back(std::move(program));
    }
}

// The Sources and SPIR-V tabs of a session program, from its stages'
// SPIR-V cache records (doc/erhe/graphics.md "SPIR-V cache records").
void Shader_variants_window::select_session(const std::size_t program_index)
{
    Program& program = m_programs[program_index];
    const auto cached = m_session_details.find(program.id);
    const bool have_detail = (cached != m_session_details.end());
    erhe::graphics::Spirv_cache& cache = m_session_programs->get_device().get_spirv_cache();
    std::vector<std::string> includes;
    for (Stage& stage : program.stages) {
        Stage_detail& detail = m_stage_details.emplace_back();
        const std::optional<erhe::graphics::Spirv_cache_record> record = stage.cache_key.empty()
            ? std::optional<erhe::graphics::Spirv_cache_record>{}
            : cache.get_record(stage.cache_key);
        const std::vector<unsigned int> module = stage.cache_key.empty() ? std::vector<unsigned int>{} : cache.get_module(stage.cache_key);
        if (!record.has_value() || module.empty()) {
            const char* const reason = "(no SPIR-V cache record for this stage: Graphics_config::spirv_cache_records is off, or the program is not cached)";
            detail.source.set(reason);
            detail.preamble.set(reason);
            detail.before.set(reason);
            detail.after.set(reason);
            continue;
        }
        const erhe::graphics::Shader_variant_report_stage report_stage = erhe::graphics::make_report_stage(
            record.value(), module, stage.shader_type, fmt::format("{} {} stage", program.name, stage.type)
        );
        detail.source  .set(report_stage.main_source);
        detail.preamble.set(report_stage.preamble);
        detail.before  .set(erhe::graphics::disassemble_spirv(report_stage.spirv));
        detail.after   .set(
            report_stage.optimized_spirv.empty()
                ? std::string{"(the optimizer failed or its output did not validate; see logs/log.txt)"}
                : erhe::graphics::disassemble_spirv(report_stage.optimized_spirv)
        );
        stage.main_name                   = report_stage.main_name;
        stage.spirv_bytes                 = report_stage.spirv.size() * sizeof(unsigned int);
        stage.optimized_spirv_bytes       = report_stage.optimized_spirv.size() * sizeof(unsigned int);
        stage.instruction_count           = erhe::graphics::count_spirv_instructions(report_stage.spirv);
        stage.optimized_instruction_count = erhe::graphics::count_spirv_instructions(report_stage.optimized_spirv);
        stage.spirv_known                 = true;
        stage.optimized_spirv_known       = true;
        // The stage's own includes, the top-level files left out, once each.
        for (const std::string& include : record->include_paths) {
            if ((include != record->main_name) && (std::find(includes.begin(), includes.end(), include) == includes.end())) {
                includes.push_back(include);
            }
        }
    }
    // Analyzed in the background unless done (the window reloads when it lands).
    m_session_programs->request_analysis(program.fragments_key);
    if (!have_detail) {
        program.includes = std::move(includes);
        m_session_details[program.id] = Session_detail{.stages = program.stages, .includes = program.includes};
        set_spirv_cells(program);
        m_sort_dirty = true;
        update_shown();
    }
    for (const std::string& include : program.includes) {
        Include_detail& include_detail = m_include_details.emplace_back();
        include_detail.path = include;
        std::optional<std::string> text = read_text(include);
        include_detail.text.set(text.has_value() ? std::move(text.value()) : std::string{"(unreadable)"});
    }
}

void Shader_variants_window::load_sidecars()
{
    m_analytics = read_js_sidecar(m_directory / "analytics.js", "window.erhe_shader_analytics");
    m_fragments = read_js_sidecar(m_directory / "fragments.js", "window.erhe_shader_fragments");
    m_mali_core.clear();
    m_powervr_core.clear();
    const ordered_json* cores = member(&m_analytics, "cores");
    const auto core_name = [cores](const ordered_json* name) -> std::string {
        if ((name == nullptr) || !name->is_string() || (member(cores, name->get_ref<const std::string&>().c_str()) == nullptr)) {
            return {};
        }
        return name->get<std::string>();
    };
    if (const ordered_json* default_cores = member(&m_analytics, "default_cores"); default_cores != nullptr) {
        m_mali_core    = core_name(member(default_cores, "malioc"));
        m_powervr_core = core_name(member(default_cores, "powervr"));
    } else {
        m_mali_core   = core_name(member(&m_analytics, "default_core"));
    }
}

void Shader_variants_window::build_key_slots()
{
    // One slot per define axis that varies among the programs of one name,
    // in first-appearance order, as wide as its widest label.
    class Axis
    {
    public:
        std::string                     name;
        std::string                     code;
        bool                            boolean{true};
        bool                            used{false};
        std::size_t                     width{0};
    };
    std::vector<Axis> axes;
    const auto find_axis = [&axes](const std::string& name) -> std::size_t {
        for (std::size_t i = 0; i < axes.size(); ++i) {
            if (axes[i].name == name) {
                return i;
            }
        }
        return c_none;
    };
    for (const Program& program : m_programs) {
        for (const Define& define : program.defines) {
            std::size_t index = find_axis(define.name);
            if (index == c_none) {
                index = axes.size();
                axes.push_back(Axis{.name = define.name, .code = compact_name(define.name)});
            }
            if (!define.value.empty() && (define.value != "0") && (define.value != "1")) {
                axes[index].boolean = false;
            }
        }
    }
    const auto compact_label = [&axes](const std::size_t axis, const std::string& value) -> std::string {
        const std::string& code = axes[axis].code;
        if (axes[axis].boolean) {
            return code;
        }
        if (is_integer_text(value)) {
            return code + value;
        }
        return code + "=" + (std::string_view{value}.starts_with("s_") ? value.substr(2) : value);
    };
    const auto value_of = [](const Program& program, const std::string& name) -> const Define* {
        for (const Define& define : program.defines) {
            if (define.name == name) {
                return &define;
            }
        }
        return nullptr;
    };
    // An axis is key when some program name sees it with more than one value (absent counts as "0").
    for (Axis& axis : axes) {
        std::vector<std::pair<std::string_view, std::string_view>> first_values; // program name -> first value
        for (const Program& program : m_programs) {
            const Define* const define = value_of(program, axis.name);
            const std::string_view value = (define != nullptr) ? std::string_view{define->value} : std::string_view{"0"};
            auto i = std::find_if(first_values.begin(), first_values.end(), [&program](const auto& p) { return p.first == program.name; });
            if (i == first_values.end()) {
                first_values.emplace_back(program.name, value);
            } else if (i->second != value) {
                axis.used = true;
                break;
            }
        }
    }
    for (const Program& program : m_programs) {
        for (const Define& define : program.defines) {
            const std::size_t index = find_axis(define.name);
            if (axes[index].used && (define.value != "0")) {
                axes[index].width = std::max(axes[index].width, compact_label(index, define.value).size());
            }
        }
    }
    for (Program& program : m_programs) {
        program.key_text.clear();
        program.key_full.clear();
        program.key_slots.clear();
        for (const Define& define : program.defines) {
            if (define.value != "0") {
                if (!program.key_text.empty()) {
                    program.key_text += ' ';
                }
                program.key_text += compact_label(find_axis(define.name), define.value);
            }
            if (!program.key_full.empty()) {
                program.key_full += '\n';
            }
            program.key_full += define.value.empty() ? define.name : fmt::format("{}={}", define.name, define.value);
            if (!define.value_name.empty()) {
                program.key_full += fmt::format(" ({})", define.value_name);
            }
        }
        for (std::size_t index = 0; index < axes.size(); ++index) {
            const Axis& axis = axes[index];
            if (!axis.used || (axis.width == 0)) { // width 0: never set, the HTML slot shows nothing
                continue;
            }
            const Define* const define = value_of(program, axis.name);
            Key_slot slot;
            slot.set = (define != nullptr) && (define->value != "0");
            slot.label = slot.set ? compact_label(index, define->value) : std::string{"."};
            slot.label.resize(axis.width, ' ');
            program.key_slots.push_back(std::move(slot));
        }
    }
    std::string key_tooltip{"One slot per define axis that varies among the variants of a program (the full defines are in the program's Shader key tab), in order:"};
    for (const Axis& axis : axes) {
        if (axis.used && (axis.width > 0)) {
            key_tooltip += fmt::format("\n{} = {}", axis.code, axis.name);
        }
    }
    m_columns[2].tooltip = std::move(key_tooltip);
}

// Columns 0 - 8: index, name, key, stages, SPIR-V bytes and instructions
// before / after optimization ("-" for a module a session program has not
// read yet).
void Shader_variants_window::set_spirv_cells(Program& program) const
{
    const auto number_cell = [](const std::optional<double> value) -> Cell {
        return value.has_value()
            ? Cell{.number = value.value(), .has_number = true, .bad = false, .text = format_number(value.value())}
            : Cell{.number = 0.0, .has_number = false, .bad = false, .text = "-"};
    };
    const auto text_cell = [](std::string text) -> Cell {
        return Cell{.number = 0.0, .has_number = false, .bad = false, .text = std::move(text)};
    };
    std::string stages;
    std::size_t bytes = 0, optimized_bytes = 0, instructions = 0, optimized_instructions = 0;
    bool before_known = true;
    bool after_known  = true;
    for (const Stage& stage : program.stages) {
        if (!stages.empty()) {
            stages += ' ';
        }
        stages                 += stage.type;
        bytes                  += stage.spirv_bytes;
        optimized_bytes        += stage.optimized_spirv_bytes;
        instructions           += stage.instruction_count;
        optimized_instructions += stage.optimized_instruction_count;
        before_known = before_known && stage.spirv_known;
        after_known  = after_known  && stage.optimized_spirv_known;
    }
    const auto known = [](const bool is_known, const std::size_t value) -> std::optional<double> {
        return is_known ? std::optional<double>{static_cast<double>(value)} : std::nullopt;
    };
    const double percent = (bytes > 0) ? (100.0 * static_cast<double>(optimized_bytes) / static_cast<double>(bytes)) : 0.0;
    std::array<Cell, 9> cells{
        number_cell(static_cast<double>(program.index)),
        text_cell(program.name),
        text_cell(program.key_text),
        text_cell(std::move(stages)),
        number_cell(known(before_known, bytes)),
        number_cell(known(after_known, optimized_bytes)),
        (before_known && after_known)
            ? Cell{.number = percent, .has_number = true, .bad = false, .text = fmt::format("{:.1f}", percent)}
            : Cell{.number = 0.0, .has_number = false, .bad = false, .text = "-"},
        number_cell(known(before_known, instructions)),
        number_cell(known(after_known, optimized_instructions))
    };
    if (program.cells.size() < cells.size()) {
        program.cells.resize(cells.size());
    }
    for (std::size_t i = 0; i < cells.size(); ++i) {
        program.cells[i] = std::move(cells[i]);
    }
}

void Shader_variants_window::build_cells()
{
    const auto number_cell = [](const std::optional<double> value) -> Cell {
        return value.has_value()
            ? Cell{.number = value.value(), .has_number = true, .bad = false, .text = format_number(value.value())}
            : Cell{.number = 0.0, .has_number = false, .bad = false, .text = "-"};
    };
    const auto text_cell = [](std::string text) -> Cell {
        return Cell{.number = 0.0, .has_number = false, .bad = false, .text = std::move(text)};
    };

    for (Program& program : m_programs) {
        program.cells.clear();
        set_spirv_cells(program);
    }

    const ordered_json* cores = member(&m_analytics, "cores");
    std::vector<std::string> core_group_names;

    // Mali, per program: fragment (or compute) Main cycles before / after and
    // the bound pipelines, vertex Position / Varying cycles, max work
    // registers, min occupancy, spill bytes summed, flags.
    if (!m_mali_core.empty()) {
        const std::string tag = short_core(m_mali_core) + " ";
        const std::size_t offset = m_columns.size();
        for (const auto& [label, numeric, bad] : {
            std::tuple{"Main cyc. before", true, Bad_rule::none}, std::tuple{"Main cyc. after", true, Bad_rule::none},
            std::tuple{"Bound", false, Bad_rule::none}, std::tuple{"Pos. cyc.", true, Bad_rule::none},
            std::tuple{"Var. cyc.", true, Bad_rule::none}, std::tuple{"Regs", true, Bad_rule::none},
            std::tuple{"Occ. %", true, Bad_rule::below_100}, std::tuple{"Spill B", true, Bad_rule::above_0},
            std::tuple{"Flags", false, Bad_rule::has_error}
        }) {
            m_columns.push_back(Column{.label = tag + label, .tooltip = m_mali_core, .numeric = numeric, .bad = bad, .group = core_title(m_mali_core), .vendor = Column_vendor::mali});
        }
        m_groups.push_back(Column_group{.tag = short_core(m_mali_core), .offset = offset, .sum_a = 0, .sum_b = 1, .sum_label = "Main cycles", .unit = "cyc.", .fragments = false, .vendor = Column_vendor::mali});
        core_group_names.push_back(m_mali_core);
        const ordered_json* programs = member(member(cores, m_mali_core.c_str()), "programs");
        for (Program& program : m_programs) {
            const ordered_json* prog = member(programs, program.id.c_str());
            std::optional<double> main_before, main_after, position, varying, regs, occ;
            double spill = 0.0;
            std::string bound;
            std::vector<std::string> flags;
            const auto flag = [&flags](const char* name) {
                if (std::find(flags.begin(), flags.end(), name) == flags.end()) {
                    flags.emplace_back(name);
                }
            };
            if ((prog != nullptr) && prog->is_object()) {
                for (const auto& [stage, s] : prog->items()) {
                    const ordered_json* b  = member(&s, "before");
                    const ordered_json* af = member(&s, "after");
                    if (af == nullptr) {
                        af = b;
                    }
                    if (has_error(b) || has_error(af)) {
                        flag("error");
                    }
                    if ((af == nullptr) || has_error(af)) {
                        continue;
                    }
                    const ordered_json* variants = member(af, "variants");
                    if ((variants != nullptr) && variants->is_array()) {
                        for (const ordered_json& v : *variants) {
                            if (const std::optional<double> r = get_number(member(&v, "work_registers")); r.has_value()) {
                                regs = regs.has_value() ? std::max(regs.value(), r.value()) : r.value();
                            }
                            if (const std::optional<double> o = get_number(member(&v, "occupancy")); o.has_value()) {
                                occ = occ.has_value() ? std::min(occ.value(), o.value()) : o.value();
                            }
                            if (const std::optional<double> sp = get_number(member(&v, "spill_bytes")); sp.has_value()) {
                                spill += sp.value();
                                if (sp.value() > 0.0) {
                                    flag("spill");
                                }
                            }
                        }
                    }
                    const ordered_json* p = member(af, "properties");
                    if (is_true(p, "uses_late_zs_test") || is_true(p, "uses_late_zs_update")) flag("late-zs");
                    if (is_true(p, "modifies_coverage"))       flag("coverage");
                    if (is_true(p, "has_side_effects"))        flag("side-effects");
                    if (is_true(p, "has_uniform_computation")) flag("uniform-comp");
                    if ((stage == "frag") || (stage == "comp")) {
                        const ordered_json* mb = has_error(b) ? nullptr : array_item(member(b, "variants"), 0);
                        const ordered_json* ma = array_item(variants, 0);
                        main_before = max_cycles(mb);
                        main_after  = max_cycles(ma);
                        bound.clear();
                        if (const ordered_json* bound_total = member(ma, "bound_total"); (bound_total != nullptr) && bound_total->is_array()) {
                            for (const ordered_json& unit : *bound_total) {
                                if (!bound.empty()) {
                                    bound += ' ';
                                }
                                bound += unit.is_string() ? unit.get<std::string>() : unit.dump();
                            }
                        }
                    }
                    if ((stage == "vert") && (variants != nullptr) && variants->is_array()) {
                        for (const ordered_json& v : *variants) {
                            if (v.value("name", std::string{}) == "Varying") {
                                varying = max_cycles(&v);
                            } else {
                                position = max_cycles(&v);
                            }
                        }
                    }
                }
            }
            std::string flag_text;
            for (const std::string& f : flags) {
                flag_text += (flag_text.empty() ? "" : " ") + f;
            }
            const bool has_prog = (prog != nullptr);
            program.cells.push_back(number_cell(main_before));
            program.cells.push_back(number_cell(main_after));
            program.cells.push_back(text_cell(std::move(bound)));
            program.cells.push_back(number_cell(position));
            program.cells.push_back(number_cell(varying));
            program.cells.push_back(number_cell(regs));
            program.cells.push_back(number_cell(occ));
            program.cells.push_back(number_cell(has_prog ? std::optional<double>{spill} : std::nullopt));
            program.cells.push_back(text_cell(std::move(flag_text)));
        }
    }

    // PowerVR: fragment (or compute) Main cycles before / after, its
    // instructions and samples after, vertex Main cycles, max temporary
    // registers, min USC utilisation, flags.
    if (!m_powervr_core.empty()) {
        const std::string tag = short_core(m_powervr_core) + " ";
        const std::size_t offset = m_columns.size();
        for (const auto& [label, numeric, bad] : {
            std::tuple{"Cyc. before", true, Bad_rule::none}, std::tuple{"Cyc. after", true, Bad_rule::none},
            std::tuple{"Instr.", true, Bad_rule::none}, std::tuple{"Samples", true, Bad_rule::none},
            std::tuple{"VS cyc.", true, Bad_rule::none}, std::tuple{"Temps", true, Bad_rule::none},
            std::tuple{"Util. %", true, Bad_rule::none}, std::tuple{"Flags", false, Bad_rule::has_error}
        }) {
            m_columns.push_back(Column{.label = tag + label, .tooltip = m_powervr_core, .numeric = numeric, .bad = bad, .group = core_title(m_powervr_core), .vendor = Column_vendor::powervr});
        }
        m_groups.push_back(Column_group{.tag = short_core(m_powervr_core), .offset = offset, .sum_a = 0, .sum_b = 1, .sum_label = "FS/CS cycles", .unit = "cyc.", .fragments = false, .vendor = Column_vendor::powervr});
        core_group_names.push_back(m_powervr_core);
        const ordered_json* programs = member(member(cores, m_powervr_core.c_str()), "programs");
        const auto main_value = [](const ordered_json* summary, const char* key) -> std::optional<double> {
            if ((summary == nullptr) || has_error(summary)) {
                return std::nullopt;
            }
            return get_number(member(find_variant(member(summary, "variants"), "Main"), key));
        };
        for (Program& program : m_programs) {
            const ordered_json* prog = member(programs, program.id.c_str());
            std::optional<double> cycles_before, cycles_after, instructions, samples, vs_cycles, temps, utilisation;
            bool error = false;
            if ((prog != nullptr) && prog->is_object()) {
                for (const auto& [stage, s] : prog->items()) {
                    const ordered_json* b  = member(&s, "before");
                    const ordered_json* af = member(&s, "after");
                    if (af == nullptr) {
                        af = b;
                    }
                    if (has_error(b) || has_error(af)) {
                        error = true;
                    }
                    if ((af == nullptr) || has_error(af)) {
                        continue;
                    }
                    if (const ordered_json* variants = member(af, "variants"); (variants != nullptr) && variants->is_array()) {
                        for (const ordered_json& v : *variants) {
                            if (const std::optional<double> r = get_number(member(&v, "temp_registers")); r.has_value()) {
                                temps = temps.has_value() ? std::max(temps.value(), r.value()) : r.value();
                            }
                            if (const std::optional<double> u = get_number(member(&v, "utilisation")); u.has_value()) {
                                utilisation = utilisation.has_value() ? std::min(utilisation.value(), u.value()) : u.value();
                            }
                        }
                    }
                    if ((stage == "frag") || (stage == "comp")) {
                        cycles_before = main_value(b, "cycles");
                        cycles_after  = main_value(af, "cycles");
                        instructions  = main_value(af, "instructions");
                        samples       = main_value(af, "samples");
                    }
                    if (stage == "vert") {
                        vs_cycles = main_value(af, "cycles");
                    }
                }
            }
            program.cells.push_back(number_cell(cycles_before));
            program.cells.push_back(number_cell(cycles_after));
            program.cells.push_back(number_cell(instructions));
            program.cells.push_back(number_cell(samples));
            program.cells.push_back(number_cell(vs_cycles));
            program.cells.push_back(number_cell(temps));
            program.cells.push_back(number_cell(utilisation));
            program.cells.push_back(text_cell(error ? std::string{"error"} : std::string{}));
        }
    }

    m_analytics_info.clear();
    if (!core_group_names.empty()) {
        std::string names;
        for (const std::string& name : core_group_names) {
            names += (names.empty() ? "" : ", ") + name;
        }
        m_analytics_info = is_session()
            ? fmt::format("Analytics: {} (the offline compilers of this session)", names)
            : fmt::format("Analytics: {} ({} core(s) in analytics.js)", names, (cores != nullptr) ? cores->size() : 0);
    }

    // Fragments of a captured frame (fragments.js), matched by JSON [name, defines]:
    // invocations, their frame share, and per analytics group the estimated
    // work (fragments times the fragment Main cost after optimization) and its share.
    m_fragments_info.clear();
    const ordered_json* fragment_programs = member(&m_fragments, "programs");
    if (fragment_programs != nullptr) {
        class Estimate
        {
        public:
            std::string   tag;
            std::string   unit;
            std::size_t   column;
            Column_vendor vendor;
        };
        std::vector<Estimate> estimates;
        for (const Column_group& group : m_groups) {
            estimates.push_back(Estimate{
                .tag    = group.tag,
                .unit   = group.unit,
                .column = group.offset + 1,
                .vendor = group.vendor
            });
        }
        const std::string frame_id = member(&m_fragments, "frame_id") != nullptr ? m_fragments["frame_id"].dump() : std::string{"?"};
        const std::size_t base = m_columns.size();
        m_columns.push_back(Column{.label = "Fragments", .tooltip = fmt::format("fragments.js: frame {}", frame_id), .numeric = true, .bad = Bad_rule::none, .group = "Fragments"});
        m_columns.push_back(Column{.label = "Frame %",   .tooltip = m_columns.back().tooltip, .numeric = true, .bad = Bad_rule::none, .group = "Fragments"});
        for (const Estimate& e : estimates) {
            m_columns.push_back(Column{.label = fmt::format("{} est. {}", e.tag, e.unit), .tooltip = m_columns[base].tooltip, .numeric = true, .bad = Bad_rule::none, .group = "Fragments", .vendor = e.vendor});
            m_columns.push_back(Column{.label = fmt::format("{} est. %", e.tag),         .tooltip = m_columns[base].tooltip, .numeric = true, .bad = Bad_rule::none, .group = "Fragments", .vendor = e.vendor});
        }
        std::unordered_set<std::string> matched;
        std::unordered_set<std::string> unique_seen;
        std::vector<bool>               unique_row(m_programs.size(), false);
        double total = 0.0;
        for (std::size_t i = 0; i < m_programs.size(); ++i) {
            Program& program = m_programs[i];
            unique_row[i] = unique_seen.insert(program.fragments_key).second;
            const ordered_json* entry = member(fragment_programs, program.fragments_key.c_str());
            const std::optional<double> count = get_number(member(entry, "invocations"));
            if (count.has_value() && matched.insert(program.fragments_key).second) {
                total += count.value();
            }
            program.cells.push_back(number_cell(count));
            program.cells.push_back(number_cell(std::nullopt));
            for (const Estimate& e : estimates) {
                const Cell& cost = program.cells[e.column];
                program.cells.push_back(number_cell((count.has_value() && cost.has_number) ? std::optional<double>{count.value() * cost.number} : std::nullopt));
                program.cells.push_back(number_cell(std::nullopt));
            }
        }
        const std::optional<double> total_invocations = get_number(member(&m_fragments, "total_invocations"));
        const double frame_total = (total_invocations.has_value() && (total_invocations.value() != 0.0)) ? total_invocations.value() : total;
        std::vector<double> estimate_totals(estimates.size(), 0.0);
        for (std::size_t i = 0; i < m_programs.size(); ++i) {
            for (std::size_t e = 0; e < estimates.size(); ++e) {
                const Cell& cell = m_programs[i].cells[base + 2 + (2 * e)];
                if (unique_row[i] && cell.has_number) {
                    estimate_totals[e] += cell.number;
                }
            }
        }
        for (Program& program : m_programs) {
            if (program.cells[base].has_number) {
                program.cells[base + 1] = number_cell(100.0 * program.cells[base].number / std::max(frame_total, 1.0));
            }
            for (std::size_t e = 0; e < estimates.size(); ++e) {
                const Cell& value = program.cells[base + 2 + (2 * e)];
                if (value.has_number) {
                    program.cells[base + 3 + (2 * e)] = number_cell(100.0 * value.number / std::max(estimate_totals[e], 1.0));
                }
            }
        }
        m_groups.push_back(Column_group{.tag = {}, .offset = base, .sum_a = 0, .sum_b = 2, .sum_label = {}, .unit = {}, .fragments = true, .vendor = Column_vendor::none});
        m_fragments_info = fmt::format(
            "Fragments: frame {}, {} of {} captured programs recorded here ({:.1f} % of the frame's invocations)",
            frame_id, matched.size(), fragment_programs->size(), 100.0 * total / std::max(frame_total, 1.0)
        );
    }

    // Bad values and the filter text (name, key, full defines, flags).
    for (Program& program : m_programs) {
        for (std::size_t c = 0; c < m_columns.size(); ++c) {
            Cell& cell = program.cells[c];
            switch (m_columns[c].bad) {
                case Bad_rule::below_100: cell.bad = cell.has_number && (cell.number < 100.0); break;
                case Bad_rule::above_0:   cell.bad = cell.has_number && (cell.number > 0.0);   break;
                case Bad_rule::has_error: cell.bad = cell.text.find("error") != std::string::npos; break;
                default: break;
            }
        }
        std::string filter_text = program.name + " " + program.key_text + " " + program.key_full;
        for (std::size_t c = 0; c < m_columns.size(); ++c) {
            if (m_columns[c].bad == Bad_rule::has_error) {
                filter_text += " " + program.cells[c].text;
            }
        }
        program.filter_text = to_lower(filter_text);
    }
}

void Shader_variants_window::reload()
{
    ERHE_PROFILE_FUNCTION();

    const std::string selected_id = (m_selected < m_programs.size()) ? m_programs[m_selected].id : std::string{};
    m_programs.clear();
    m_columns.clear();
    m_groups.clear();
    m_shown.clear();
    m_load_error.clear();
    m_selected = c_none;
    m_analytics = nullptr;
    m_fragments = nullptr;
    m_analytics_info.clear();
    m_fragments_info.clear();
    {
        const std::lock_guard<std::mutex> lock{m_reload_mutex};
        m_showing_session = is_session();
    }
    if (m_directory.empty() && (m_session_programs == nullptr)) {
        m_load_error = "No browser directory: start the editor with --shader-variant-browser <directory>, or open a recorded one";
        m_stage_details.clear();
        m_include_details.clear();
        return;
    }

    for (const char* label : {"#", "Program", "Variant key", "Stages", "Bytes before", "Bytes after", "Bytes %", "Instr. before", "Instr. after"}) {
        const bool numeric = (std::strcmp(label, "#") == 0) || std::string_view{label}.starts_with("Bytes") || std::string_view{label}.starts_with("Instr");
        m_columns.push_back(Column{.label = label, .tooltip = {}, .numeric = numeric, .bad = Bad_rule::none, .group = {}, .vendor = Column_vendor::none});
    }
    if (is_session()) {
        load_session();
    } else if (!load_manifest(m_load_error)) {
        m_columns.clear();
        m_stage_details.clear();
        m_include_details.clear();
        return;
    } else {
        load_sidecars();
    }
    build_key_slots();
    build_cells();
    m_vendor_columns_dirty = true; // a new column set: apply the vendor choice to it
    m_sort_dirty = true;
    update_shown();

    for (std::size_t i = 0; i < m_programs.size(); ++i) {
        if (!selected_id.empty() && (m_programs[i].id == selected_id)) {
            m_selected = i; // the files of a recorded id do not change; its details stay
            return;
        }
    }
    m_stage_details.clear();
    m_include_details.clear();
}

void Shader_variants_window::update_shown()
{
    std::vector<std::string> words;
    {
        const std::string filter = to_lower(m_filter);
        std::size_t i = 0;
        while (i < filter.size()) {
            while ((i < filter.size()) && std::isspace(static_cast<unsigned char>(filter[i]))) {
                ++i;
            }
            std::size_t j = i;
            while ((j < filter.size()) && !std::isspace(static_cast<unsigned char>(filter[j]))) {
                ++j;
            }
            if (j > i) {
                words.push_back(filter.substr(i, j - i));
            }
            i = j;
        }
    }
    m_shown.clear();
    for (std::size_t i = 0; i < m_programs.size(); ++i) {
        const std::string& text = m_programs[i].filter_text;
        if (std::all_of(words.begin(), words.end(), [&text](const std::string& w) { return text.find(w) != std::string::npos; })) {
            m_shown.push_back(i);
        }
    }
    const std::size_t column = std::min(static_cast<std::size_t>(std::max(m_sort_column, 0)), m_columns.size() - 1);
    const bool numeric = m_columns[column].numeric;
    std::stable_sort(m_shown.begin(), m_shown.end(), [this, column, numeric](const std::size_t a, const std::size_t b) {
        const Cell& x = m_programs[a].cells[column];
        const Cell& y = m_programs[b].cells[column];
        if (numeric) {
            const double nx = x.has_number ? x.number : -1.0;
            const double ny = y.has_number ? y.number : -1.0;
            return m_sort_ascending ? (nx < ny) : (nx > ny);
        }
        const int c = x.text.compare(y.text);
        return m_sort_ascending ? (c < 0) : (c > 0);
    });

    // Sums over the shown programs; a sum some program has no value for
    // (a session program not selected yet) names how many it covers.
    std::size_t before = 0;
    std::size_t after  = 0;
    std::size_t before_count = 0;
    std::size_t after_count  = 0;
    for (const std::size_t i : m_shown) {
        const Program& program = m_programs[i];
        if (program.cells[4].has_number) {
            before += static_cast<std::size_t>(program.cells[4].number);
            ++before_count;
        }
        if (program.cells[5].has_number) {
            after += static_cast<std::size_t>(program.cells[5].number);
            ++after_count;
        }
    }
    const auto sum_text = [this](const std::size_t sum, const std::size_t count) -> std::string {
        if (count == m_shown.size()) {
            return fmt::format("{}", sum);
        }
        return (count == 0) ? std::string{"-"} : fmt::format("{} ({} programs)", sum, count);
    };
    m_summary = fmt::format("{} / {} programs, SPIR-V {} -> {} bytes", m_shown.size(), m_programs.size(), sum_text(before, before_count), sum_text(after, after_count));
    for (const Column_group& group : m_groups) {
        if (group.fragments) {
            std::unordered_set<std::string_view> keys;
            double shown_fragments = 0.0;
            for (const std::size_t i : m_shown) {
                const Cell& cell = m_programs[i].cells[group.offset];
                if (cell.has_number && keys.insert(m_programs[i].fragments_key).second) {
                    shown_fragments += cell.number;
                }
            }
            const std::optional<double> total_invocations = get_number(member(&m_fragments, "total_invocations"));
            m_summary += fmt::format(", fragments {} of the frame's {}", format_number(shown_fragments), format_number(total_invocations.value_or(0.0)));
            continue;
        }
        double sum_a = 0.0;
        double sum_b = 0.0;
        std::size_t count_a = 0;
        std::size_t count_b = 0;
        for (const std::size_t i : m_shown) {
            const Cell& a = m_programs[i].cells[group.offset + group.sum_a];
            const Cell& b = m_programs[i].cells[group.offset + group.sum_b];
            if (a.has_number) {
                sum_a += a.number;
                ++count_a;
            }
            if (b.has_number) {
                sum_b += b.number;
                ++count_b;
            }
        }
        const auto group_sum_text = [this](const double sum, const std::size_t count) -> std::string {
            return (count == 0) ? std::string{"-"} : (count == m_shown.size()) ? format_number(sum) : fmt::format("{} ({} programs)", format_number(sum), count);
        };
        m_summary += fmt::format(", {} {} {} -> {}", group.tag, group.sum_label, group_sum_text(sum_a, count_a), group_sum_text(sum_b, count_b));
    }
}

void Shader_variants_window::select(const std::size_t program_index)
{
    ERHE_PROFILE_FUNCTION();

    m_selected = program_index;
    m_stage_details.clear();
    m_include_details.clear();
    if (is_session()) {
        select_session(program_index);
        return;
    }
    const Program& program = m_programs[program_index];
    const std::filesystem::path variants = m_directory / "variants";
    const auto text_or = [&variants](const std::string& file, const char* missing) -> std::string {
        if (file.empty()) {
            return missing;
        }
        std::optional<std::string> text = read_text(variants / file);
        return text.has_value() ? std::move(text.value()) : fmt::format("(cannot read variants/{})", file);
    };
    for (const Stage& stage : program.stages) {
        Stage_detail& detail = m_stage_details.emplace_back();
        detail.source  .set(text_or(stage.source,   "(not recorded: the directory predates source files)"));
        detail.preamble.set(text_or(stage.preamble, "(not recorded: the directory predates source files)"));
        const std::optional<std::vector<unsigned int>> before = read_spirv(variants / stage.spirv);
        detail.before.set(before.has_value() ? erhe::graphics::disassemble_spirv(before.value()) : fmt::format("(cannot read variants/{})", stage.spirv));
        if (stage.optimized_spirv.empty()) {
            detail.after.set("(the optimizer failed or its output did not validate; see logs/log.txt)");
        } else {
            const std::optional<std::vector<unsigned int>> after = read_spirv(variants / stage.optimized_spirv);
            detail.after.set(after.has_value() ? erhe::graphics::disassemble_spirv(after.value()) : fmt::format("(cannot read variants/{})", stage.optimized_spirv));
        }
    }
    for (const std::string& include : program.includes) {
        Include_detail& detail = m_include_details.emplace_back();
        detail.path = include;
        std::optional<std::string> text = read_text(include);
        detail.text.set(text.has_value() ? std::move(text.value()) : std::string{"(unreadable)"});
    }
}

void Shader_variants_window::bar_imgui()
{
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - 220.0f, 120.0f));
    const bool open = ImGui::InputTextWithHint(
        "##directory",
        (m_session_programs != nullptr) ? "Browser directory (empty: this session's programs)" : "Browser directory",
        &m_directory_input,
        ImGuiInputTextFlags_EnterReturnsTrue
    );
    ImGui::SameLine();
    if (ImGui::Button("Open") || open) {
        m_directory = std::filesystem::path{m_directory_input};
        m_selected  = c_none;
        reload();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        reload();
    }
    if ((m_report != nullptr) && (m_directory != m_report->get_directory())) {
        ImGui::SameLine();
        if (ImGui::Button("Session")) {
            m_directory       = m_report->get_directory();
            m_directory_input = m_directory.generic_string();
            m_selected        = c_none;
            reload();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show the directory this session records into");
        }
    } else if ((m_report == nullptr) && (m_session_programs != nullptr) && !m_directory.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Session")) {
            m_directory.clear();
            m_directory_input.clear();
            m_selected = c_none;
            reload();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show the programs this session built, from their SPIR-V cache records");
        }
    }
    if (is_session()) {
        // Analysis status: change-driven reloads refresh it as results land.
        if (ImGui::Button("Analyze all")) {
            m_session_programs->request_analysis_all();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Run the offline compilers on every listed program, before and after optimization, in background tasks");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_session_analysis_info.c_str());
    }

    if (!m_load_error.empty()) {
        ImGui::TextWrapped("%s", m_load_error.c_str());
        return;
    }
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - 260.0f, 120.0f));
    if (ImGui::InputTextWithHint("##filter", "Filter by program or define (all words must match)", &m_filter)) {
        update_shown();
    }
    ImGui::SameLine();
    int panes = static_cast<int>(m_panes);
    ImGui::RadioButton("Both", &panes, static_cast<int>(Panes::both));
    ImGui::SameLine();
    ImGui::RadioButton("Table", &panes, static_cast<int>(Panes::table));
    ImGui::SameLine();
    ImGui::RadioButton("Program", &panes, static_cast<int>(Panes::details));
    m_panes = static_cast<Panes>(panes);
    ImGui::TextDisabled("%s", m_summary.c_str());
    if (!m_analytics_info.empty()) {
        ImGui::TextDisabled("%s", m_analytics_info.c_str());
    }
    if (!m_fragments_info.empty()) {
        ImGui::TextDisabled("%s", m_fragments_info.c_str());
    }
}

void Shader_variants_window::table_imgui()
{
    const ImGuiTableFlags flags =
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##variants", static_cast<int>(m_columns.size()), flags)) {
        return;
    }
    ImGui::TableSetupScrollFreeze(3, 2); // #, Program, Variant key; the group row and the column header row
    for (std::size_t c = 0; c < m_columns.size(); ++c) {
        ImGuiTableColumnFlags column_flags = ImGuiTableColumnFlags_None;
        if (c == 0) {
            column_flags |= ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_NoHide;
        }
        ImGui::TableSetupColumn(m_columns[c].label.c_str(), column_flags, 0.0f, static_cast<ImGuiID>(c));
    }
    // Change-driven: a vendor toggled in the group row menu, or a reload built new columns.
    if (m_vendor_columns_dirty) {
        m_vendor_columns_dirty = false;
        for (std::size_t c = 0; c < m_columns.size(); ++c) {
            if (m_columns[c].vendor != Column_vendor::none) {
                ImGui::TableSetColumnEnabled(static_cast<int>(c), m_vendor_shown[static_cast<std::size_t>(m_columns[c].vendor)]);
            }
        }
    }

    if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs(); (sort_specs != nullptr) && sort_specs->SpecsDirty) {
        if (sort_specs->SpecsCount > 0) {
            m_sort_column    = static_cast<int>(sort_specs->Specs[0].ColumnUserID);
            m_sort_ascending = (sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
        }
        sort_specs->SpecsDirty = false;
        update_shown();
    }

    bool open_vendor_menu = false;
    group_row_imgui(open_vendor_menu);

    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    for (std::size_t c = 0; c < m_columns.size(); ++c) {
        if (!ImGui::TableSetColumnIndex(static_cast<int>(c))) {
            continue;
        }
        ImGui::TableHeader(m_columns[c].label.c_str());
        if (!m_columns[c].tooltip.empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", m_columns[c].tooltip.c_str());
        }
    }

    ImFont* const mono_font = m_imgui_renderer.mono_font();
    std::size_t clicked = c_none;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(m_shown.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::size_t index = m_shown[static_cast<std::size_t>(row)];
            const Program& program = m_programs[index];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(index));
            for (std::size_t c = 0; c < m_columns.size(); ++c) {
                if (!ImGui::TableSetColumnIndex(static_cast<int>(c))) {
                    continue;
                }
                const Cell& cell = program.cells[c];
                if (c == 0) {
                    if (ImGui::Selectable(cell.text.c_str(), index == m_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                        clicked = index;
                    }
                    continue;
                }
                if (c == 2) {
                    if (mono_font != nullptr) {
                        ImGui::PushFont(mono_font, 0.0f);
                    }
                    for (std::size_t s = 0; s < program.key_slots.size(); ++s) {
                        const Key_slot& slot = program.key_slots[s];
                        if (s > 0) {
                            ImGui::SameLine(0.0f, ImGui::CalcTextSize(" ").x);
                        }
                        if (slot.set) {
                            ImGui::TextUnformatted(slot.label.c_str());
                        } else {
                            ImGui::TextDisabled("%s", slot.label.c_str());
                        }
                    }
                    if (mono_font != nullptr) {
                        ImGui::PopFont();
                    }
                    continue;
                }
                if (cell.bad) {
                    ImGui::PushStyleColor(ImGuiCol_Text, c_bad_color);
                }
                if (m_columns[c].numeric) {
                    const float width = ImGui::CalcTextSize(cell.text.c_str()).x;
                    const float avail = ImGui::GetContentRegionAvail().x;
                    if (avail > width) {
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - width);
                    }
                }
                ImGui::TextUnformatted(cell.text.c_str());
                if (cell.bad) {
                    ImGui::PopStyleColor();
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();

    // Opened here, outside the table, so BeginPopup() below sees the same id.
    if (open_vendor_menu) {
        ImGui::OpenPopup("##vendor_columns");
    }
    vendor_menu_imgui();

    if (clicked != c_none) {
        if (m_panes == Panes::table) {
            m_panes = Panes::both;
        }
        select(clicked);
    }
}

void Shader_variants_window::group_row_imgui(bool& open_vendor_menu)
{
    const float row_height = ImGui::GetFrameHeight();
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, row_height);
    ImGuiTable* const table = ImGui::GetCurrentTable();
    if (table == nullptr) {
        return;
    }

    // Runs of adjacent enabled columns with one group label, in display order
    // (the user may reorder and hide columns); the layout is valid after TableNextRow().
    m_group_runs.clear();
    m_column_runs.assign(m_columns.size(), -1);
    int previous = -1;
    for (int order = 0; order < table->ColumnsCount; ++order) {
        const int               c      = table->DisplayOrderToIndex[order];
        const ImGuiTableColumn& column = table->Columns[c];
        if (!column.IsEnabled || (static_cast<std::size_t>(c) >= m_columns.size())) {
            continue;
        }
        const std::string& label = m_columns[static_cast<std::size_t>(c)].group;
        if (label.empty()) {
            previous = -1;
            continue;
        }
        if ((previous < 0) || (*m_group_runs[static_cast<std::size_t>(previous)].label != label)) {
            m_group_runs.push_back(Group_run{.label = &label, .min_x = column.MinX, .visible_min_x = FLT_MAX});
            previous = static_cast<int>(m_group_runs.size()) - 1;
        }
        m_column_runs[static_cast<std::size_t>(c)] = previous;
        if (column.IsVisibleX) {
            Group_run& run = m_group_runs[static_cast<std::size_t>(previous)];
            run.visible_min_x = std::min(run.visible_min_x, column.ClipRect.Min.x);
        }
    }

    // Each visible cell of a run draws the run's label at the same position,
    // clipped to itself, so the label reads across the run. The label sticks
    // to the run's visible left edge while the run scrolls under the frozen columns.
    const ImU32 text_color   = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 run_colors[] = { ImGui::GetColorU32(ImGuiCol_Header), ImGui::GetColorU32(ImGuiCol_Header, 0.55f) };
    const float padding      = ImGui::GetStyle().CellPadding.x;
    for (std::size_t c = 0; c < m_columns.size(); ++c) {
        if (!ImGui::TableSetColumnIndex(static_cast<int>(c))) {
            continue;
        }
        ImGui::PushID(static_cast<int>(c));
        const int    run_index = m_column_runs[c];
        const ImVec2 cursor    = ImGui::GetCursorScreenPos();
        if (run_index >= 0) {
            const Group_run& run = m_group_runs[static_cast<std::size_t>(run_index)];
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, run_colors[static_cast<std::size_t>(run_index) % 2]);
            ImGui::GetWindowDrawList()->AddText(ImVec2{std::max(run.min_x, run.visible_min_x) + padding, cursor.y}, text_color, run.label->c_str());
        }
        ImGui::InvisibleButton("##group", ImVec2{std::max(ImGui::GetContentRegionAvail().x, 1.0f), ImGui::GetTextLineHeight()}, ImGuiButtonFlags_MouseButtonRight);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            open_vendor_menu = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Right-click to choose the vendors whose columns are shown");
        }
        ImGui::PopID();
    }
}

void Shader_variants_window::vendor_menu_imgui()
{
    if (!ImGui::BeginPopup("##vendor_columns")) {
        return;
    }
    ImGui::TextDisabled("Vendor columns");
    for (std::size_t v = 0; v < c_vendor_count; ++v) {
        const Column_vendor vendor  = static_cast<Column_vendor>(v);
        const bool          present = std::any_of(m_columns.begin(), m_columns.end(), [vendor](const Column& column) { return column.vendor == vendor; });
        bool shown = m_vendor_shown[v];
        ImGui::BeginDisabled(!present);
        if (ImGui::Checkbox(c_vendor_names[v], &shown)) {
            m_vendor_shown[v]      = shown;
            m_vendor_columns_dirty = true;
        }
        ImGui::EndDisabled();
        if (!present) {
            ImGui::SetItemTooltip("analytics.js has no %s core", c_vendor_names[v]);
        }
    }
    ImGui::EndPopup();
}

void Shader_variants_window::key_tab_imgui(const Program& program)
{
    ImGui::TextDisabled("%zu defines, in the order the program declares them; the table shows the ones that vary among this program's variants as its key slots.", program.defines.size());
    if (!ImGui::BeginTable("##defines", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)) {
        return;
    }
    ImGui::TableSetupColumn("Define");
    ImGui::TableSetupColumn("Value");
    ImGui::TableSetupColumn("Name");
    ImGui::TableHeadersRow();
    ImFont* const mono_font = m_imgui_renderer.mono_font();
    if (mono_font != nullptr) {
        ImGui::PushFont(mono_font, 0.0f);
    }
    for (const Define& define : program.defines) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(define.name.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(define.value.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(define.value_name.c_str());
    }
    if (mono_font != nullptr) {
        ImGui::PopFont();
    }
    ImGui::EndTable();
}

void Shader_variants_window::sources_tab_imgui(const Program& program)
{
    ImFont* const mono_font = m_imgui_renderer.mono_font();
    char label[512];
    for (std::size_t s = 0; (s < program.stages.size()) && (s < m_stage_details.size()); ++s) {
        const Stage& stage = program.stages[s];
        ImGui::PushID(static_cast<int>(s));
        std::snprintf(label, sizeof(label), "%s - %s###source", stage_title(stage.type), stage.main_name.c_str());
        if (mono_font != nullptr) ImGui::PushFont(mono_font, 0.0f);
        m_stage_details[s].source.imgui(label, true);
        std::snprintf(label, sizeof(label), "%s - synthesized erhe_preamble.glsl###preamble", stage_title(stage.type));
        m_stage_details[s].preamble.imgui(label, false);
        if (mono_font != nullptr) ImGui::PopFont();
        ImGui::PopID();
    }
    for (std::size_t i = 0; i < m_include_details.size(); ++i) {
        ImGui::PushID(static_cast<int>(1000 + i));
        std::snprintf(label, sizeof(label), "include - %s###include", m_include_details[i].path.c_str());
        if (mono_font != nullptr) ImGui::PushFont(mono_font, 0.0f);
        m_include_details[i].text.imgui(label, false);
        if (mono_font != nullptr) ImGui::PopFont();
        ImGui::PopID();
    }
}

void Shader_variants_window::spirv_tab_imgui(const Program& program, const bool optimized)
{
    ImFont* const mono_font = m_imgui_renderer.mono_font();
    char label[256];
    for (std::size_t s = 0; (s < program.stages.size()) && (s < m_stage_details.size()); ++s) {
        const Stage& stage = program.stages[s];
        ImGui::PushID(static_cast<int>(s));
        std::snprintf(
            label, sizeof(label), "%s - %zu bytes, %zu instructions###spirv",
            stage_title(stage.type),
            optimized ? stage.optimized_spirv_bytes : stage.spirv_bytes,
            optimized ? stage.optimized_instruction_count : stage.instruction_count
        );
        if (mono_font != nullptr) ImGui::PushFont(mono_font, 0.0f);
        (optimized ? m_stage_details[s].after : m_stage_details[s].before).imgui(label, true);
        if (mono_font != nullptr) ImGui::PopFont();
        ImGui::PopID();
    }
}

void Shader_variants_window::mali_core_imgui(const ordered_json& core, const ordered_json& program)
{
    const auto text = [&core](const char* key) -> const char* {
        const ordered_json* v = member(&core, key);
        return ((v != nullptr) && v->is_string()) ? v->get_ref<const std::string&>().c_str() : "";
    };
    ImGui::Text("%s %s", text("core"), text("revision"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s, driver %s, Mali Offline Compiler %s, %s", text("architecture"), text("driver"), text("malioc_version"), text("generated"));
    const ordered_json* pipelines = member(&core, "pipelines");
    for (const auto& [stage, s] : program.items()) {
        ImGui::PushID(stage.c_str());
        ImGui::SeparatorText(stage_title(stage));
        const ordered_json* b  = member(&s, "before");
        const ordered_json* af = member(&s, "after");
        error_lines(b, af);
        const ordered_json* bv = member(b, "variants");
        const ordered_json* av = member(af, "variants");
        for (const ordered_json* list : {bv, av}) {
            if ((list == nullptr) || !list->is_array()) {
                continue;
            }
            for (const ordered_json& variant : *list) {
                const std::string name = variant.value("name", std::string{});
                if ((list == av) && (find_variant(bv, name) != nullptr)) {
                    continue; // shown with the before list
                }
                const ordered_json* x = find_variant(bv, name);
                const ordered_json* y = find_variant(av, name);
                ImGui::PushID(name.c_str());
                if (ImGui::TreeNodeEx("##variant", ImGuiTreeNodeFlags_DefaultOpen, "%s variant", name.c_str())) {
                    if (begin_compare_table("##compare")) {
                        for (const auto& [label, key] : {
                            std::pair{"Work registers", "work_registers"}, std::pair{"Uniform registers", "uniform_registers"},
                            std::pair{"Thread occupancy %", "occupancy"}, std::pair{"Stack bytes", "stack_size"},
                            std::pair{"Spill bytes", "spill_bytes"}, std::pair{"Alloca bytes", "alloca_bytes"},
                            std::pair{"16-bit arithmetic %", "fp16"}
                        }) {
                            compare_row(label, member(x, key), member(y, key), 1);
                        }
                        for (const auto& [label, key, bound_key] : {
                            std::tuple{"Total cycles", "total", "bound_total"},
                            std::tuple{"Shortest path", "shortest", "bound_shortest"},
                            std::tuple{"Longest path", "longest", "bound_longest"}
                        }) {
                            if ((pipelines != nullptr) && pipelines->is_array()) {
                                for (std::size_t i = 0; i < pipelines->size(); ++i) {
                                    char row_label[128];
                                    char pipeline[64];
                                    format_value(&(*pipelines)[i], pipeline, sizeof(pipeline));
                                    std::snprintf(row_label, sizeof(row_label), "%s: %s", label, pipeline);
                                    compare_row(row_label, array_item(member(x, key), i), array_item(member(y, key), i), 1);
                                }
                            }
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%s: bound", label);
                            for (const auto& [column, v] : {std::pair{1, x}, std::pair{2, y}}) {
                                ImGui::TableSetColumnIndex(column);
                                const ordered_json* bound = member(v, bound_key);
                                if ((bound != nullptr) && bound->is_array()) {
                                    for (std::size_t i = 0; i < bound->size(); ++i) {
                                        char unit[64];
                                        format_value(&(*bound)[i], unit, sizeof(unit));
                                        if (i > 0) {
                                            ImGui::SameLine();
                                        }
                                        ImGui::TextUnformatted(unit);
                                    }
                                }
                            }
                        }
                        ImGui::EndTable();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
        const ordered_json* properties = member(af, "properties");
        if ((properties == nullptr) || properties->empty()) {
            properties = member(b, "properties");
        }
        if ((properties != nullptr) && properties->is_object() && !properties->empty()) {
            if (ImGui::TreeNodeEx("Shader properties", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginTable("##properties", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                    for (const auto& [key, value] : properties->items()) {
                        char buffer[128];
                        format_value(&value, buffer, sizeof(buffer));
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(key.c_str());
                        ImGui::TableSetColumnIndex(1);
                        if (value.is_boolean() && value.get<bool>()) {
                            ImGui::TextColored(c_bad_color, "%s", buffer);
                        } else {
                            ImGui::TextUnformatted(buffer);
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }
        }
        const ordered_json* streams = member(af, "attribute_streams");
        if (streams == nullptr) {
            streams = member(b, "attribute_streams");
        }
        if ((streams != nullptr) && ImGui::TreeNode("Attribute streams")) {
            const std::string dump = streams->dump(1);
            ImGui::TextUnformatted(dump.c_str());
            ImGui::TreePop();
        }
        for (const auto& [label, summary] : {std::pair{"before", b}, std::pair{"after", af}}) {
            if (summary == nullptr) {
                continue;
            }
            char node_label[64];
            std::snprintf(node_label, sizeof(node_label), "%s: warnings and notes", label);
            string_lines_node(node_label, member(summary, "warnings"), member(summary, "notes"));
            command_line(label, summary);
        }
        ImGui::PopID();
    }
}

void Shader_variants_window::powervr_core_imgui(const ordered_json& core, const ordered_json& program)
{
    const auto text = [&core](const char* key) -> const char* {
        const ordered_json* v = member(&core, key);
        return ((v != nullptr) && v->is_string()) ? v->get_ref<const std::string&>().c_str() : "";
    };
    ImGui::Text("%s", text("core"));
    ImGui::SameLine();
    ImGui::TextDisabled("BVNC %s, %s (%s), PowerVR profiling compiler %s, %s", text("bvnc"), text("architecture"), text("soc"), text("powervr_version"), text("generated"));
    // [[name, value], ...] lookup.
    const auto pair_value = [](const ordered_json* list, const std::string& name) -> const ordered_json* {
        if ((list == nullptr) || !list->is_array()) {
            return nullptr;
        }
        for (const ordered_json& entry : *list) {
            if (entry.is_array() && (entry.size() >= 2) && entry[0].is_string() && (entry[0].get_ref<const std::string&>() == name)) {
                return entry[1].is_null() ? nullptr : &entry[1];
            }
        }
        return nullptr;
    };
    // sense: 1 lower is better, -1 higher is better, 0 neither
    const auto sense_of = [](const std::string& counter) -> int {
        if ((counter == "USC Utilisation") || (counter == "Max Parallel Instances")) {
            return -1;
        }
        return (counter == "Memory Used HWM") ? 0 : 1;
    };
    for (const auto& [stage, s] : program.items()) {
        ImGui::PushID(stage.c_str());
        ImGui::SeparatorText(stage_title(stage));
        const ordered_json* b  = member(&s, "before");
        const ordered_json* af = member(&s, "after");
        error_lines(b, af);
        const ordered_json* bv = member(b, "variants");
        const ordered_json* av = member(af, "variants");
        for (const ordered_json* list : {bv, av}) {
            if ((list == nullptr) || !list->is_array()) {
                continue;
            }
            for (const ordered_json& variant : *list) {
                const std::string name = variant.value("name", std::string{});
                if ((list == av) && (find_variant(bv, name) != nullptr)) {
                    continue; // shown with the before list
                }
                const ordered_json* xs = member(find_variant(bv, name), "counters");
                const ordered_json* ys = member(find_variant(av, name), "counters");
                ImGui::PushID(name.c_str());
                if (ImGui::TreeNodeEx("##variant", ImGuiTreeNodeFlags_DefaultOpen, "%s", name.c_str())) {
                    if (begin_compare_table("##compare")) {
                        for (const ordered_json* counters : {xs, ys}) {
                            if ((counters == nullptr) || !counters->is_array()) {
                                continue;
                            }
                            for (const ordered_json& entry : *counters) {
                                if (!entry.is_array() || entry.empty() || !entry[0].is_string()) {
                                    continue;
                                }
                                const std::string& counter = entry[0].get_ref<const std::string&>();
                                if ((counters == ys) && (pair_value(xs, counter) != nullptr)) {
                                    continue;
                                }
                                compare_row(counter.c_str(), pair_value(xs, counter), pair_value(ys, counter), sense_of(counter));
                            }
                        }
                        ImGui::EndTable();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
        for (const auto& [label, summary] : {std::pair{"before", b}, std::pair{"after", af}}) {
            if (summary != nullptr) {
                command_line(label, summary);
            }
        }
        ImGui::PopID();
    }
}

void Shader_variants_window::detail_imgui()
{
    if (m_selected >= m_programs.size()) {
        ImGui::TextDisabled("Select a program in the table");
        return;
    }
    const Program& program = m_programs[m_selected];
    ImGui::Text("%s", program.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("#%zu (%s)", program.index, program.id.c_str());

    if (const ordered_json* fragment_programs = member(&m_fragments, "programs"); fragment_programs != nullptr) {
        char frame_id[32];
        format_value(member(&m_fragments, "frame_id"), frame_id, sizeof(frame_id));
        const ordered_json* entry = member(fragment_programs, program.fragments_key.c_str());
        const std::optional<double> invocations = get_number(member(entry, "invocations"));
        if (invocations.has_value()) {
            const double total = get_number(member(&m_fragments, "total_invocations")).value_or(0.0);
            ImGui::TextDisabled(
                "Fragments in captured frame %s: %.0f (%.2f %% of the frame's %.0f) in %.0f pipeline bind range(s)",
                frame_id, invocations.value(), 100.0 * invocations.value() / std::max(total, 1.0), total,
                get_number(member(entry, "ranges")).value_or(0.0)
            );
        } else {
            ImGui::TextDisabled("Not drawn in captured frame %s (fragments.js)", frame_id);
        }
    }

    if (ImGui::BeginTable("##stages", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Stage");
        ImGui::TableSetupColumn("Bytes before");
        ImGui::TableSetupColumn("Bytes after");
        ImGui::TableSetupColumn("Instr. before");
        ImGui::TableSetupColumn("Instr. after");
        ImGui::TableSetupColumn(".spv");
        ImGui::TableHeadersRow();
        for (const Stage& stage : program.stages) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(stage_title(stage.type));
            ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", stage.spirv_bytes);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%zu", stage.optimized_spirv_bytes);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%zu", stage.instruction_count);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%zu", stage.optimized_instruction_count);
            ImGui::TableSetColumnIndex(5); ImGui::TextDisabled("%s %s", stage.spirv.c_str(), stage.optimized_spirv.c_str());
        }
        ImGui::EndTable();
    }

    // One tab bar id for every program, so the active tab stays across selections.
    if (!ImGui::BeginTabBar("##program_tabs")) {
        return;
    }
    const ordered_json* cores = member(&m_analytics, "cores");
    const auto tab_child = [](const char* id) -> bool {
        return ImGui::BeginChild(id, ImVec2{0.0f, 0.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    };
    if (ImGui::BeginTabItem("Shader key")) {
        if (tab_child("##key")) {
            key_tab_imgui(program);
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Sources")) {
        if (tab_child("##sources")) {
            sources_tab_imgui(program);
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("SPIR-V before optimization")) {
        if (tab_child("##before")) {
            spirv_tab_imgui(program, false);
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("SPIR-V after optimization")) {
        if (tab_child("##after")) {
            spirv_tab_imgui(program, true);
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    bool any_core = false;
    if ((cores != nullptr) && cores->is_object()) {
        for (const auto& [core_name, core] : cores->items()) {
            const ordered_json* prog = member(member(&core, "programs"), program.id.c_str());
            if ((prog == nullptr) || !prog->is_object()) {
                continue;
            }
            any_core = true;
            char tab_label[128];
            std::snprintf(tab_label, sizeof(tab_label), "%s", core_name.c_str());
            if (char* dash = std::strchr(tab_label, '-'); dash != nullptr) {
                *dash = ' ';
            }
            if (ImGui::BeginTabItem(tab_label)) {
                if (tab_child("##core")) {
                    const ordered_json* compiler = member(&core, "compiler");
                    const std::string   compiler_name = ((compiler != nullptr) && compiler->is_string()) ? compiler->get<std::string>() : std::string{};
                    if (compiler_name == "powervr") {
                        powervr_core_imgui(core, *prog);
                    } else {
                        mali_core_imgui(core, *prog);
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
    }
    if (!any_core && ImGui::BeginTabItem("GPU analytics")) {
        if (is_session()) {
            ImGui::TextWrapped("%s", m_session_analysis_info.empty() ? "Not analyzed yet." : m_session_analysis_info.c_str());
        } else {
            ImGui::TextWrapped("No analytics recorded: run py -3 scripts/shader_analytics.py <browser directory> to write analytics.js (doc/erhe/graphics.md \"Mali analytics\", \"PowerVR analytics\"), then Reload.");
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void Shader_variants_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    bool reload_now = false;
    {
        std::lock_guard<std::mutex> lock{m_reload_mutex};
        reload_now = m_reload_requested;
        m_reload_requested = false;
    }
    if (reload_now) {
        reload();
    }

    bar_imgui();
    if (!m_load_error.empty()) {
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (m_panes == Panes::both) {
        if (m_table_width <= 0.0f) {
            m_table_width = avail.x * 0.52f;
        }
        if (ImGui::BeginChild("##table_pane", ImVec2{m_table_width, 0.0f}, ImGuiChildFlags_ResizeX)) {
            table_imgui();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("##detail_pane", ImVec2{0.0f, 0.0f})) {
            detail_imgui();
        }
        ImGui::EndChild();
    } else if (m_panes == Panes::table) {
        table_imgui();
    } else {
        if (ImGui::BeginChild("##detail_pane", ImVec2{0.0f, 0.0f})) {
            detail_imgui();
        }
        ImGui::EndChild();
    }
}

} // namespace editor
