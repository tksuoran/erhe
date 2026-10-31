#include "erhe_graphics/shader_variant_report.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_file/file.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spirv-tools/libspirv.hpp>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string_view>

namespace erhe::graphics {

namespace {

[[nodiscard]] auto html_escape(const std::string_view text) -> std::string
{
    std::string result;
    result.reserve(text.size() + (text.size() / 16));
    for (const char c : text) {
        switch (c) {
            case '&': result += "&amp;";  break;
            case '<': result += "&lt;";   break;
            case '>': result += "&gt;";   break;
            case '"': result += "&quot;"; break;
            default:  result += c;        break;
        }
    }
    return result;
}

// JSON string literal (for the table data embedded in index.html); '<' is
// escaped so no "</script>" can end the script element.
[[nodiscard]] auto json_string(const std::string_view text) -> std::string
{
    std::string result{"\""};
    for (const char c : text) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            case '<':  result += "\\u003c"; break;
            default: {
                if (static_cast<unsigned char>(c) < 0x20) {
                    result += fmt::format("\\u{:04x}", static_cast<unsigned int>(static_cast<unsigned char>(c)));
                } else {
                    result += c;
                }
                break;
            }
        }
    }
    result += '"';
    return result;
}

[[nodiscard]] auto count_instructions(const std::vector<unsigned int>& spirv) -> std::size_t
{
    constexpr std::size_t header_word_count = 5;
    std::size_t count = 0;
    std::size_t i     = header_word_count;
    while (i < spirv.size()) {
        const unsigned int word_count = spirv[i] >> 16u;
        if (word_count == 0u) {
            break;
        }
        ++count;
        i += word_count;
    }
    return count;
}

[[nodiscard]] auto disassemble(const std::vector<unsigned int>& spirv) -> std::string
{
    if (spirv.empty()) {
        return "(none)";
    }
    spvtools::SpirvTools tools{SPV_ENV_UNIVERSAL_1_6};
    std::string text;
    const bool ok = tools.Disassemble(
        spirv,
        &text,
        SPV_BINARY_TO_TEXT_OPTION_INDENT | SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES
    );
    return ok ? text : std::string{"(disassembly failed)"};
}

[[nodiscard]] auto stage_short_name(const Shader_type type) -> const char*
{
    switch (type) {
        case Shader_type::vertex_shader:          return "vert";
        case Shader_type::fragment_shader:        return "frag";
        case Shader_type::compute_shader:         return "comp";
        case Shader_type::geometry_shader:        return "geom";
        case Shader_type::tess_control_shader:    return "tesc";
        case Shader_type::tess_evaluation_shader: return "tese";
        default:                                  return "stage";
    }
}

void write_spirv_file(const std::filesystem::path& path, const std::vector<unsigned int>& spirv)
{
    if (spirv.empty()) {
        return;
    }
    const std::string_view bytes{reinterpret_cast<const char*>(spirv.data()), spirv.size() * sizeof(unsigned int)};
    if (!erhe::file::write_file_via_temp(path, bytes)) {
        log_program->warn("Shader variant browser: failed to write {}", path.string());
    }
}

constexpr const char* c_style = R"CSS(
:root { --bg: #ffffff; --fg: #1c1c1c; --muted: #6b6b6b; --line: #d8d8d8; --head: #f2f2f2; --hover: #eaf1fb; --select: #cfe0f7; --code: #f7f7f7; --good: #1e7b34; --bad: #b42318; }
@media (prefers-color-scheme: dark) {
  :root { --bg: #161616; --fg: #e6e6e6; --muted: #9a9a9a; --line: #333333; --head: #222222; --hover: #1f2a38; --select: #26405f; --code: #1e1e1e; --good: #5cc275; --bad: #f07167; }
}
html, body { margin: 0; background: var(--bg); color: var(--fg); font: 13px/1.4 system-ui, sans-serif; }
code, pre, .mono { font-family: ui-monospace, "DejaVu Sans Mono", monospace; font-size: 12px; }
pre { background: var(--code); border: 1px solid var(--line); padding: 8px; margin: 4px 0 12px 0; overflow: auto; white-space: pre; }
table { border-collapse: collapse; }
th, td { border-bottom: 1px solid var(--line); padding: 3px 6px; text-align: left; vertical-align: top; }
th { background: var(--head); position: sticky; top: 0; cursor: pointer; user-select: none; }
td.num, th.num { text-align: right; font-variant-numeric: tabular-nums; }
.muted { color: var(--muted); }
.good { color: var(--good); }
.bad { color: var(--bad); }
)CSS";

} // anonymous namespace

Shader_variant_report::Shader_variant_report(const std::filesystem::path& directory)
    : m_directory{directory}
{
    std::error_code error_code;
    std::filesystem::create_directories(m_directory / "variants", error_code);
    if (error_code) {
        log_program->error("Shader variant browser: cannot create {}: {}", (m_directory / "variants").string(), error_code.message());
    }
    log_program->info(
        "Shader variant browser: recording compiled programs into {} (programs served from the SPIR-V cache are not recorded; delete spirv_cache/ to record every program)",
        m_directory.string()
    );
    write_index();
}

void Shader_variant_report::add(
    const Shader_stages_create_info&             create_info,
    const std::span<const Shader_variant_report_stage> stages,
    const std::span<const std::filesystem::path> include_paths,
    const Shader_variant_report_tools&           tools
)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_tools = tools;

    const std::size_t id = m_rows.size();
    Row row{
        .id      = id,
        .name    = create_info.name,
        .defines = create_info.defines,
        .stages  = {}
    };

    // Detail page: the program's sizes, then five tabs (key, sources, SPIR-V before, after, Mali analytics).
    std::string sources_tab;
    std::string before_tab;
    std::string after_tab;
    std::string size_rows;
    for (const Shader_variant_report_stage& stage : stages) {
        const char* const short_name = stage_short_name(stage.type);
        const Stage_row stage_row{
            .type                        = stage.type,
            .spirv_bytes                 = stage.spirv.size() * sizeof(unsigned int),
            .optimized_spirv_bytes       = stage.optimized_spirv.size() * sizeof(unsigned int),
            .instruction_count           = count_instructions(stage.spirv),
            .optimized_instruction_count = count_instructions(stage.optimized_spirv)
        };
        row.stages.push_back(stage_row);

        const std::string spv_name           = fmt::format("{}_{}.spv",           id, short_name);
        const std::string optimized_spv_name = fmt::format("{}_{}_optimized.spv", id, short_name);
        write_spirv_file(m_directory / "variants" / spv_name,           stage.spirv);
        write_spirv_file(m_directory / "variants" / optimized_spv_name, stage.optimized_spirv);

        size_rows += fmt::format(
            "<tr><td>{}</td><td class=\"num\">{}</td><td class=\"num\">{}</td><td class=\"num\">{}</td><td class=\"num\">{}</td><td><a href=\"{}\">before</a> <a href=\"{}\">after</a></td></tr>\n",
            c_str(stage.type),
            stage_row.spirv_bytes, stage_row.optimized_spirv_bytes,
            stage_row.instruction_count, stage_row.optimized_instruction_count,
            spv_name, optimized_spv_name
        );
        sources_tab += fmt::format(
            "<details open><summary>{} - <code>{}</code></summary><pre>{}</pre></details>\n"
            "<details><summary>{} - synthesized <code>erhe_preamble.glsl</code></summary><pre>{}</pre></details>\n",
            c_str(stage.type), html_escape(stage.main_name), html_escape(stage.main_source),
            c_str(stage.type), html_escape(stage.preamble)
        );
        before_tab += fmt::format(
            "<details open><summary>{} - {} bytes, {} instructions</summary><pre>{}</pre></details>\n",
            c_str(stage.type), stage_row.spirv_bytes, stage_row.instruction_count, html_escape(disassemble(stage.spirv))
        );
        after_tab += fmt::format(
            "<details open><summary>{} - {} bytes, {} instructions</summary><pre>{}</pre></details>\n",
            c_str(stage.type), stage_row.optimized_spirv_bytes, stage_row.optimized_instruction_count,
            stage.optimized_spirv.empty()
                ? std::string{"(the optimizer failed or its output did not validate; see logs/log.txt)"}
                : html_escape(disassemble(stage.optimized_spirv))
        );
    }

    // The included files, read now (the includer resolved them from these
    // paths during the parse); the top-level files are shown per stage.
    for (const std::filesystem::path& path : include_paths) {
        const bool is_main = std::any_of(stages.begin(), stages.end(), [&path](const Shader_variant_report_stage& stage) {
            return stage.main_name == path.generic_string();
        });
        if (is_main) {
            continue;
        }
        const std::optional<std::string> text = erhe::file::read("Shader_variant_report::add", path);
        sources_tab += fmt::format(
            "<details><summary>include - <code>{}</code></summary><pre>{}</pre></details>\n",
            html_escape(path.generic_string()),
            text.has_value() ? html_escape(text.value()) : std::string{"(unreadable)"}
        );
    }

    std::string define_rows;
    for (const auto& [define_name, value] : create_info.defines) {
        define_rows += fmt::format("<tr><td class=\"mono\">{}</td><td class=\"mono\">{}</td></tr>\n", html_escape(define_name), html_escape(value));
    }

    const std::string page = fmt::format(
        R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>{0} #{1}</title>
<style>{2}
body {{ padding: 12px 16px; }}
.tabs button {{ font: inherit; padding: 4px 12px; border: 1px solid var(--line); background: var(--head); color: var(--fg); cursor: pointer; }}
.tabs button.active {{ background: var(--select); }}
.tab {{ display: none; }} .tab.active {{ display: block; }}
summary {{ cursor: pointer; padding: 2px 0; }}
.columns {{ display: flex; gap: 24px; flex-wrap: wrap; align-items: flex-start; }}
</style></head><body>
<h2>{0} <span class="muted">#{1}</span></h2>
<div class="columns">
<table><tr><th>Stage</th><th class="num">Bytes before</th><th class="num">Bytes after</th><th class="num">Instr. before</th><th class="num">Instr. after</th><th>.spv</th></tr>
{3}</table>
</div>
<p class="tabs"><button class="active" data-tab="key">Shader key</button><button data-tab="sources">Sources</button><button data-tab="before">SPIR-V before optimization</button><button data-tab="after">SPIR-V after optimization</button><button data-tab="analytics">Mali analytics</button></p>
<div class="tab active" id="key"><p class="muted">{4} defines, in the order the program declares them; the index shows the ones that vary among this program's variants as its key slots.</p><table><tr><th>Define</th><th>Value</th></tr>{5}</table></div>
<div class="tab" id="sources">{6}</div>
<div class="tab" id="before">{7}</div>
<div class="tab" id="after">{8}</div>
<div class="tab" id="analytics"><p class="muted">No analytics recorded: run <code>py -3 scripts/shader_analytics.py &lt;browser directory&gt;</code> to write <code>analytics.js</code> (doc/erhe/graphics.md "Shader variant browser").</p></div>
<script>
for (const button of document.querySelectorAll('.tabs button')) {{
  button.addEventListener('click', () => {{
    for (const b of document.querySelectorAll('.tabs button')) b.classList.toggle('active', b === button);
    for (const t of document.querySelectorAll('.tab')) t.classList.toggle('active', t.id === button.dataset.tab);
  }});
}}
</script>
<script src="../analytics.js"></script>
<script>
// The Mali analytics tab, rendered from analytics.js (scripts/shader_analytics.py)
// when it is present: per core, per stage, per compiler variant (Main, or
// Position and Varying for vertex shaders) a before / after / delta table.
(function() {{
  const a = window.erhe_shader_analytics; if (!a || !a.cores) return;
  const esc = s => String(s).replace(/[&<>]/g, c => ({{'&':'&amp;','<':'&lt;','>':'&gt;'}})[c]);
  const fmt = v => (v === null || v === undefined) ? '-' : (typeof v === 'number' ? (Number.isInteger(v) ? String(v) : v.toFixed(2)) : String(v));
  const stageNames = {{vert: 'Vertex', frag: 'Fragment', comp: 'Compute', geom: 'Geometry', tesc: 'Tessellation control', tese: 'Tessellation evaluation'}};
  const row = (label, x, y) => {{
    const d = (typeof x === 'number' && typeof y === 'number') ? (y - x) : null;
    const cls = (d === null || d === 0) ? '' : (d < 0 ? 'good' : 'bad');
    return `<tr><td>${{esc(label)}}</td><td class="num">${{fmt(x)}}</td><td class="num">${{fmt(y)}}</td><td class="num ${{cls}}">${{d === null ? '' : (d > 0 ? '+' : '') + fmt(d)}}</td></tr>`;
  }};
  const renderStage = (core, s) => {{
    const b = s.before, af = s.after;
    let out = '';
    for (const [label, x] of [['before', b], ['after', af]]) if (x && x.error) out += `<p class="bad">${{label}}: ${{esc(x.error)}}</p>`;
    const bv = (b && b.variants) || [], av = (af && af.variants) || [];
    const names = [...new Set([...bv.map(v => v.name), ...av.map(v => v.name)])];
    for (const name of names) {{
      const x = bv.find(v => v.name === name), y = av.find(v => v.name === name);
      out += `<details open><summary>${{esc(name)}} variant</summary><table><tr><th></th><th class="num">Before</th><th class="num">After</th><th class="num">Delta</th></tr>`;
      for (const [label, key] of [['Work registers', 'work_registers'], ['Uniform registers', 'uniform_registers'], ['Thread occupancy %', 'occupancy'], ['Stack bytes', 'stack_size'], ['Spill bytes', 'spill_bytes'], ['Alloca bytes', 'alloca_bytes'], ['16-bit arithmetic %', 'fp16']]) {{
        out += row(label, x ? x[key] : null, y ? y[key] : null);
      }}
      for (const [label, key, bkey] of [['Total cycles', 'total', 'bound_total'], ['Shortest path', 'shortest', 'bound_shortest'], ['Longest path', 'longest', 'bound_longest']]) {{
        (core.pipelines || []).forEach((p, i) => {{ out += row(`${{label}}: ${{p}}`, (x && x[key]) ? x[key][i] : null, (y && y[key]) ? y[key][i] : null); }});
        out += `<tr><td>${{label}}: bound</td><td class="num">${{esc(((x && x[bkey]) || []).join(' '))}}</td><td class="num">${{esc(((y && y[bkey]) || []).join(' '))}}</td><td></td></tr>`;
      }}
      out += '</table></details>';
    }}
    const props = (af && af.properties) || (b && b.properties) || {{}};
    if (Object.keys(props).length) out += '<details open><summary>Shader properties</summary><table>' + Object.keys(props).map(k => `<tr><td>${{esc(k)}}</td><td class="${{props[k] === true ? 'bad' : ''}}">${{props[k]}}</td></tr>`).join('') + '</table></details>';
    const streams = (af && af.attribute_streams) || (b && b.attribute_streams);
    if (streams) out += `<details><summary>Attribute streams</summary><pre>${{esc(JSON.stringify(streams, null, 1))}}</pre></details>`;
    for (const [label, x] of [['before', b], ['after', af]]) {{
      if (!x) continue;
      const lines = [...(x.warnings || []), ...(x.notes || [])];
      if (lines.length) out += `<details><summary>${{label}}: warnings and notes (${{lines.length}})</summary><pre>${{esc(lines.join('\n'))}}</pre></details>`;
      if (x.command) out += `<p class="muted mono">${{label}}: ${{esc(x.command)}}</p>`;
    }}
    return out;
  }};
  let html = '';
  for (const coreName of Object.keys(a.cores)) {{
    const core = a.cores[coreName];
    const prog = core.programs && core.programs[String({1})];
    if (!prog) continue;
    html += `<h3>${{esc(coreName)}} ${{esc(core.revision || '')}} <span class="muted">${{esc(core.architecture || '')}}, driver ${{esc(core.driver || '')}}, Mali Offline Compiler ${{esc(core.malioc_version || '')}}, ${{esc(core.generated || '')}}</span></h3>`;
    for (const stage of Object.keys(prog)) html += `<h4>${{stageNames[stage] || stage}}</h4>` + renderStage(core, prog[stage]);
  }}
  if (html) document.getElementById('analytics').innerHTML = html;
}})();
</script>
</body></html>
)HTML",
        html_escape(create_info.name), id, c_style,
        size_rows, create_info.defines.size(), define_rows,
        sources_tab, before_tab, after_tab
    );
    const std::filesystem::path page_path = m_directory / "variants" / fmt::format("{}.html", id);
    if (!erhe::file::write_file_via_temp(page_path, page)) {
        log_program->warn("Shader variant browser: failed to write {}", page_path.string());
    }

    m_rows.push_back(std::move(row));
    write_index();
    write_manifest();
}

void Shader_variant_report::write_manifest() const
{
    nlohmann::json programs = nlohmann::json::array();
    for (const Row& row : m_rows) {
        nlohmann::json defines = nlohmann::json::array();
        for (const auto& [define_name, value] : row.defines) {
            defines.push_back(nlohmann::json::array({define_name, value}));
        }
        nlohmann::json stages = nlohmann::json::array();
        for (const Stage_row& stage : row.stages) {
            const char* const short_name = stage_short_name(stage.type);
            nlohmann::json stage_json{
                {"type",                        short_name},
                {"entry_point",                 "main"},
                {"specialization_constants",    nlohmann::json::array()},
                {"spirv",                       fmt::format("{}_{}.spv", row.id, short_name)},
                {"spirv_bytes",                 stage.spirv_bytes},
                {"instruction_count",           stage.instruction_count},
                {"optimized_spirv",             nullptr},
                {"optimized_spirv_bytes",       stage.optimized_spirv_bytes},
                {"optimized_instruction_count", stage.optimized_instruction_count}
            };
            if (stage.optimized_spirv_bytes > 0) {
                stage_json["optimized_spirv"] = fmt::format("{}_{}_optimized.spv", row.id, short_name);
            }
            stages.push_back(std::move(stage_json));
        }
        programs.push_back(nlohmann::json{
            {"id",      row.id},
            {"name",    row.name},
            {"defines", std::move(defines)},
            {"stages",  std::move(stages)}
        });
    }
    const nlohmann::json manifest{
        {"schema", {{"name", "erhe_shader_variants"}, {"version", 1}}},
        {"tools", {
            {"api",                 m_tools.api},
            {"glslang_version",     m_tools.glslang_version},
            {"spirv_tools_version", m_tools.spirv_tools_version},
            {"spirv_target_env",    m_tools.spirv_target_env},
            {"spirv_optimization",  m_tools.spirv_optimization},
            {"texture_heap_path",   m_tools.texture_heap_path},
            {"texture_heap_size",   m_tools.texture_heap_size}
        }},
        {"programs", std::move(programs)}
    };
    const std::filesystem::path path = m_directory / "variants" / "manifest.json";
    if (!erhe::file::write_file_via_temp(path, manifest.dump(1))) {
        log_program->warn("Shader variant browser: failed to write {}", path.string());
    }
}

void Shader_variant_report::write_index() const
{
    // The table data as a JSON array; the page script renders, sorts and
    // filters it, and shows the selected program's page beside the table.
    // The defines go in full (every axis, zero-valued ones included): the
    // page lays the key out as one slot per axis and shows the full define
    // in a tooltip.
    std::string data{"[\n"};
    for (const Row& row : m_rows) {
        std::string defines{"["};
        for (const auto& [define_name, value] : row.defines) {
            if (defines.size() > 1) {
                defines += ',';
            }
            defines += fmt::format("[{},{}]", json_string(define_name), json_string(value));
        }
        defines += ']';
        std::string stages;
        std::size_t bytes = 0, optimized_bytes = 0, instructions = 0, optimized_instructions = 0;
        for (const Stage_row& stage : row.stages) {
            if (!stages.empty()) {
                stages += ' ';
            }
            stages                 += stage_short_name(stage.type);
            bytes                  += stage.spirv_bytes;
            optimized_bytes        += stage.optimized_spirv_bytes;
            instructions           += stage.instruction_count;
            optimized_instructions += stage.optimized_instruction_count;
        }
        data += fmt::format(
            "[{},{},{},{},{},{},{},{}],\n",
            row.id, json_string(row.name), defines, json_string(stages),
            bytes, optimized_bytes, instructions, optimized_instructions
        );
    }
    data += "]";

    const std::string page = fmt::format(
        R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Shader variants</title>
<style>{0}
body {{ display: flex; height: 100vh; overflow: hidden; }}
#left {{ flex: 0 0 52%; display: flex; flex-direction: column; border-right: 1px solid var(--line); min-width: 0; }}
#bar {{ padding: 8px 12px; display: flex; gap: 12px; align-items: center; flex-wrap: wrap; }}
#bar input {{ font: inherit; padding: 3px 6px; flex: 1 1 200px; background: var(--bg); color: var(--fg); border: 1px solid var(--line); }}
#scroll {{ overflow: auto; flex: 1; }}
#table {{ width: 100%; }}
#table tbody tr {{ cursor: pointer; }}
#table tbody tr:hover {{ background: var(--hover); }}
#table tbody tr.selected {{ background: var(--select); }}
#table th, #table td {{ white-space: nowrap; }}
td.key {{ font-family: ui-monospace, monospace; font-size: 11px; }}
td.key span {{ display: inline-block; overflow: hidden; margin-right: 0.6ch; vertical-align: top; }}
td.key span.off {{ color: var(--line); }}
iframe {{ flex: 1; border: 0; height: 100%; background: var(--bg); }}
</style></head><body>
<div id="left">
<div id="bar"><strong>Shader variants</strong><span id="summary" class="muted"></span><input id="filter" placeholder="Filter by program or define (all words must match)"></div>
<div id="scroll"><table id="table"><thead><tr>
<th class="num" data-col="0">#</th><th data-col="1">Program</th><th data-col="2" id="keyhead">Variant key</th><th data-col="3">Stages</th>
<th class="num" data-col="4">Bytes before</th><th class="num" data-col="5">Bytes after</th><th class="num" data-col="8">Bytes %</th>
<th class="num" data-col="6">Instr. before</th><th class="num" data-col="7">Instr. after</th>
</tr></thead><tbody></tbody></table></div>
</div>
<iframe id="detail" title="Selected variant"></iframe>
<script src="analytics.js"></script>
<script>
const rows = {1};
for (const r of rows) r.push(r[4] > 0 ? (100 * r[5] / r[4]) : 0);
// The variant key column: one slot per define axis that varies among the
// programs of one name (an axis every variant of a program sets the same
// way is configuration, not key, and shows in the tooltip only), in the
// order the axes first appear, each slot as wide as the widest compact
// label it shows in any row, so the parts align down the table. A slot
// shows the axis's compact label (below), or a dot when the axis is
// zero or absent in that row. Each slot's tooltip is the full define; the
// cell's tooltip is the whole key, every define included.
// Compact axis codes: a table for the known axes, initials of the words for
// any other (ERHE_, USE_ and VARIANT_ dropped). A boolean axis (every value
// it takes is 0 or 1) shows its code, a numeric one its code followed by
// the value (B2; B1 when the axis also takes 2), a string-valued one its
// code, '=' and the value with a leading "s_" dropped.
const compactCodes = {{
  ERHE_USE_SKINNING: 'Sk',
  ERHE_USE_VERTEX_VARYING_NORMAL: 'vN', ERHE_USE_VERTEX_VARYING_COLOR: 'vC', ERHE_USE_VERTEX_VARYING_TANGENT: 'vT',
  ERHE_USE_VERTEX_VARYING_BITANGENT: 'vB', ERHE_USE_VERTEX_VARYING_TEXCOORD0: 'vU0', ERHE_USE_VERTEX_VARYING_TEXCOORD1: 'vU1',
  ERHE_USE_VERTEX_VARYING_TEXCOORD2: 'vU2', ERHE_USE_VERTEX_VARYING_ANISO_CONTROL: 'vA', ERHE_USE_VERTEX_VARYING_NODE_POSITION: 'vP',
  ERHE_LIGHT_COUNT_DIRECTIONAL_SHADOWMAPPED: 'Ld', ERHE_LIGHT_COUNT_DIRECTIONAL_NOT_SHADOWMAPPED: 'ld',
  ERHE_LIGHT_COUNT_SPOT_SHADOWMAPPED: 'Ls', ERHE_LIGHT_COUNT_SPOT_NOT_SHADOWMAPPED: 'ls',
  ERHE_LIGHT_COUNT_POINT_SHADOWMAPPED: 'Lp', ERHE_LIGHT_COUNT_POINT_NOT_SHADOWMAPPED: 'lp',
  ERHE_LIGHT_SHADING_CLUSTERED: 'LC', ERHE_USE_LIGHTMAP: 'Lm',
  ERHE_BXDF_MODEL: 'B', ERHE_MATERIAL_BLENDING_MODE: 'Bl',
  ERHE_SHADOW_FILTER: 'SF', ERHE_SHADOW_BIAS: 'SB', ERHE_SHADOW_DEPTH_BITS: 'SD',
  ERHE_USE_CIRCULAR_BRUSHED_METAL: 'Br', ERHE_CIRCULAR_BRUSHED_METAL_TEXGEN_MODE: 'BrT',
  ERHE_SPECULAR_ANTI_ALIASING: 'SAA', ERHE_FP16_PATHS: 'H',
  ERHE_USE_BASE_COLOR_TEXTURE: 'tB', ERHE_USE_METALLIC_ROUGHNESS_TEXTURE: 'tM', ERHE_USE_NORMAL_TEXTURE: 'tN',
  ERHE_USE_OCCLUSION_TEXTURE: 'tO', ERHE_USE_EMISSION_TEXTURE: 'tE',
  ERHE_SOLID_WIREFRAME: 'Wf', ERHE_SHADER_DEBUG: 'Dbg', ERHE_SHADER_MULTIVIEW_COUNT: 'MV', ERHE_OVERDRAW: 'OD',
  ERHE_VARIANT_DEPTH_ONLY: 'Dp', ERHE_VARIANT_POINTS: 'Pt', ERHE_VARIANT_BRUSH_PREVIEW: 'BP',
  ERHE_VARIANT_SHADOW_DISTANCE: 'SDi', ERHE_VARIANT_SHADOW_CUBE: 'SCu',
  SOURCE: 'src', FIRST_PASS: 'FP', LAST_PASS: 'LP'
}};
const compactName = name => compactCodes[name] || name.replace(/^ERHE_/, '').replace(/^USE_/, '').replace(/^VARIANT_/, '').split('_').map(w => w[0] || '').join('');
const booleanAxes = new Set();
{{
  const valuesByAxis = new Map();
  for (const r of rows) for (const [name, value] of r[2]) {{ if (!valuesByAxis.has(name)) valuesByAxis.set(name, new Set()); valuesByAxis.get(name).add(value); }}
  for (const [name, values] of valuesByAxis) if ([...values].every(v => v === '' || v === '0' || v === '1')) booleanAxes.add(name);
}}
const compactLabel = (name, value) => {{
  const code = compactName(name);
  if (booleanAxes.has(name)) return code;
  if (/^-?\d+$/.test(value)) return code + value;
  return `${{code}}=${{value.replace(/^s_/, '')}}`;
}};
const isSet = v => v !== '0';
const axes = [];
const axisIndex = new Map();
for (const r of rows) for (const [name, value] of r[2]) {{
  if (!axisIndex.has(name)) {{ axisIndex.set(name, axes.length); axes.push({{ name, width: 0, used: false }}); }}
}}
const valuesByProgram = new Map(); // program name -> axis name -> set of values ('0' when absent)
for (const r of rows) {{
  r.key = new Map(r[2]);
  r.keyText = r[2].filter(([n, v]) => isSet(v)).map(([n, v]) => compactLabel(n, v)).join(' ');
  r.keyFull = r[2].map(([n, v]) => v === '' ? n : `${{n}}=${{v}}`).join('\n');
  if (!valuesByProgram.has(r[1])) valuesByProgram.set(r[1], new Map());
  const byAxis = valuesByProgram.get(r[1]);
  for (const a of axes) {{
    const value = r.key.has(a.name) ? r.key.get(a.name) : '0';
    if (!byAxis.has(a.name)) byAxis.set(a.name, new Set());
    byAxis.get(a.name).add(value);
  }}
}}
for (const byAxis of valuesByProgram.values()) for (const [name, values] of byAxis) if (values.size > 1) axes[axisIndex.get(name)].used = true;
for (const r of rows) for (const [name, value] of r[2]) if (isSet(value)) {{
  const axis = axes[axisIndex.get(name)];
  if (axis.used) axis.width = Math.max(axis.width, compactLabel(name, value).length);
}}
const shownAxes = axes.filter(a => a.used);
document.getElementById('keyhead').title = 'One slot per define axis that varies among the variants of a program (the whole key is the cell tooltip), in order:\n' + shownAxes.map(a => `${{compactName(a.name)}} = ${{a.name}}`).join('\n');
const renderKey = r => shownAxes.map(a => {{
  const value = r.key.get(a.name);
  const set = value !== undefined && isSet(value);
  const full = value === undefined ? `${{a.name}} (absent)` : (value === '' ? a.name : `${{a.name}}=${{value}}`);
  return `<span class="${{set ? '' : 'off'}}" style="width:${{a.width}}ch" title="${{esc(full)}}">${{set ? esc(compactLabel(a.name, value)) : '.'}}</span>`;
}}).join('');
// Mali analytics columns (analytics.js from scripts/shader_analytics.py), per
// program: the fragment (or compute) Main variant's total cycles before and
// after optimization with the bound pipelines, the vertex Position and Varying
// cycles after, the maximum work registers and minimum occupancy over the
// stages, the spill bytes summed, and flags. Rows get indices 9..17.
const analytics = window.erhe_shader_analytics;
const analyticsCore = (analytics && analytics.cores) ? analytics.cores[analytics.default_core] : null;
const maxCycles = v => (v && v.total) ? Math.max(...v.total.filter(x => typeof x === 'number'), 0) : null;
function summarize(prog) {{
  const out = [null, null, '', null, null, null, null, null, ''];
  if (!prog) return out;
  const flags = new Set();
  let regs = null, occ = null, spill = 0;
  for (const stage of Object.keys(prog)) {{
    const s = prog[stage], b = s.before, af = s.after || s.before;
    if ((b && b.error) || (af && af.error)) flags.add('error');
    if (!af || af.error) continue;
    for (const v of af.variants || []) {{
      if (typeof v.work_registers === 'number') regs = (regs === null) ? v.work_registers : Math.max(regs, v.work_registers);
      if (typeof v.occupancy === 'number') occ = (occ === null) ? v.occupancy : Math.min(occ, v.occupancy);
      if (typeof v.spill_bytes === 'number') spill += v.spill_bytes;
      if (v.spill_bytes > 0) flags.add('spill');
    }}
    const p = af.properties || {{}};
    if (p.uses_late_zs_test || p.uses_late_zs_update) flags.add('late-zs');
    if (p.modifies_coverage) flags.add('coverage');
    if (p.has_side_effects) flags.add('side-effects');
    if (p.has_uniform_computation) flags.add('uniform-comp');
    if (stage === 'frag' || stage === 'comp') {{
      const mb = b && !b.error && b.variants && b.variants[0], ma = af.variants && af.variants[0];
      out[0] = mb ? maxCycles(mb) : null; out[1] = ma ? maxCycles(ma) : null; out[2] = (ma && ma.bound_total) ? ma.bound_total.join(' ') : '';
    }}
    if (stage === 'vert') for (const v of af.variants || []) {{ if (v.name === 'Varying') out[4] = maxCycles(v); else out[3] = maxCycles(v); }}
  }}
  out[5] = regs; out[6] = occ; out[7] = spill; out[8] = [...flags].join(' ');
  return out;
}}
if (analyticsCore) {{
  for (const r of rows) r.push(...summarize(analyticsCore.programs[String(r[0])]));
  document.querySelector('#table thead tr').insertAdjacentHTML('beforeend',
    '<th class="num" data-col="9">Main cyc. before</th><th class="num" data-col="10">Main cyc. after</th><th data-col="11">Bound</th>' +
    '<th class="num" data-col="12">Pos. cyc.</th><th class="num" data-col="13">Var. cyc.</th><th class="num" data-col="14">Regs</th><th class="num" data-col="15">Occ. %</th><th class="num" data-col="16">Spill B</th><th data-col="17">Flags</th>');
  document.getElementById('bar').insertAdjacentHTML('beforeend', `<span class="muted">Mali analytics: ${{analytics.default_core}} (${{Object.keys(analytics.cores).length}} core(s) in analytics.js)</span>`);
}}
let sortCol = 0, sortAsc = true, selected = -1;
const tbody = document.querySelector('#table tbody');
const filter = document.getElementById('filter');
const esc = s => String(s).replace(/[&<>]/g, c => ({{'&':'&amp;','<':'&lt;','>':'&gt;'}})[c]);
const num = v => (v === null || v === undefined) ? '-' : (Number.isInteger(v) ? String(v) : v.toFixed(2));
function render() {{
  const words = filter.value.toLowerCase().split(/\s+/).filter(w => w);
  const shown = rows.filter(r => words.every(w => (r[1] + ' ' + r.keyText + ' ' + r.keyFull + ' ' + (r[17] || '')).toLowerCase().includes(w)));
  shown.sort((a, b) => {{
    const x = (sortCol === 2) ? a.keyText : a[sortCol], y = (sortCol === 2) ? b.keyText : b[sortCol];
    const nx = (x === null || x === undefined) ? -1 : x, ny = (y === null || y === undefined) ? -1 : y;
    const c = (typeof nx === 'number' && typeof ny === 'number') ? (nx - ny) : String(nx).localeCompare(String(ny));
    return sortAsc ? c : -c;
  }});
  let before = 0, after = 0, cyclesBefore = 0, cyclesAfter = 0;
  tbody.innerHTML = shown.map(r => {{
    before += r[4]; after += r[5];
    let cells = `<tr data-id="${{r[0]}}" class="${{r[0] === selected ? 'selected' : ''}}"><td class="num">${{r[0]}}</td><td>${{esc(r[1])}}</td><td class="key" title="${{esc(r.keyFull)}}">${{renderKey(r)}}</td><td>${{r[3]}}</td>` +
      `<td class="num">${{r[4]}}</td><td class="num">${{r[5]}}</td><td class="num">${{r[8].toFixed(1)}}</td><td class="num">${{r[6]}}</td><td class="num">${{r[7]}}</td>`;
    if (analyticsCore) {{
      cyclesBefore += r[9] || 0; cyclesAfter += r[10] || 0;
      cells += `<td class="num">${{num(r[9])}}</td><td class="num">${{num(r[10])}}</td><td>${{esc(r[11])}}</td><td class="num">${{num(r[12])}}</td><td class="num">${{num(r[13])}}</td>` +
        `<td class="num">${{num(r[14])}}</td><td class="num ${{r[15] !== null && r[15] < 100 ? 'bad' : ''}}">${{num(r[15])}}</td><td class="num ${{r[16] > 0 ? 'bad' : ''}}">${{num(r[16])}}</td><td class="${{(r[17] || '').includes('error') ? 'bad' : ''}}">${{esc(r[17])}}</td>`;
    }}
    return cells + '</tr>';
  }}).join('');
  document.getElementById('summary').textContent = `${{shown.length}} / ${{rows.length}} programs, SPIR-V ${{before}} -> ${{after}} bytes` +
    (analyticsCore ? `, Main cycles ${{cyclesBefore.toFixed(1)}} -> ${{cyclesAfter.toFixed(1)}}` : '');
}}
document.querySelector('#table thead').addEventListener('click', e => {{
  const th = e.target.closest('th'); if (!th) return;
  const col = Number(th.dataset.col);
  if (col === sortCol) sortAsc = !sortAsc; else {{ sortCol = col; sortAsc = true; }}
  render();
}});
tbody.addEventListener('click', e => {{
  const tr = e.target.closest('tr'); if (!tr) return;
  selected = Number(tr.dataset.id);
  document.getElementById('detail').src = `variants/${{selected}}.html`;
  render();
}});
filter.addEventListener('input', render);
render();
</script>
</body></html>
)HTML",
        c_style, data
    );
    const std::filesystem::path index_path = m_directory / "index.html";
    if (!erhe::file::write_file_via_temp(index_path, page)) {
        log_program->warn("Shader variant browser: failed to write {}", index_path.string());
    }
}

} // namespace erhe::graphics
