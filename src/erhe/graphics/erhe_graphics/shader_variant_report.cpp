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

[[nodiscard]] auto find_value_name(
    const std::vector<std::pair<std::string, std::string>>& value_names,
    const std::string_view                                  define_name
) -> std::string_view
{
    for (const auto& [name, value_name] : value_names) {
        if (name == define_name) {
            return value_name;
        }
    }
    return {};
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
    return count_spirv_instructions(spirv);
}

[[nodiscard]] auto disassemble(const std::vector<unsigned int>& spirv) -> std::string
{
    return disassemble_spirv(spirv);
}

void write_text_file(const std::filesystem::path& path, const std::string_view text)
{
    if (!erhe::file::write_file_via_temp(path, text)) {
        log_program->warn("Shader variant browser: failed to write {}", path.string());
    }
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

// FNV-1a 64 of the program key, as 16 hex digits: the same program gets the
// same id in every session, whatever order the programs compile in.
[[nodiscard]] auto make_program_id(const std::string_view program_key) -> std::string
{
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (const char c : program_key) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 0x100000001b3ull;
    }
    return fmt::format("{:016x}", hash);
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

auto count_spirv_instructions(const std::span<const unsigned int> spirv) -> std::size_t
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

auto disassemble_spirv(const std::span<const unsigned int> spirv) -> std::string
{
    if (spirv.empty()) {
        return "(none)";
    }
    spvtools::SpirvTools tools{SPV_ENV_UNIVERSAL_1_6};
    std::string text;
    const bool ok = tools.Disassemble(
        spirv.data(),
        spirv.size(),
        &text,
        SPV_BINARY_TO_TEXT_OPTION_INDENT | SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES
    );
    return ok ? text : std::string{"(disassembly failed)"};
}

auto get_shader_variant_stage_short_name(const Shader_type type) -> const char*
{
    return stage_short_name(type);
}

void Shader_variant_report::set_add_callback(std::function<void()> callback)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_add_callback = std::move(callback);
}

auto Shader_variant_report::get_directory() const -> const std::filesystem::path&
{
    return m_directory;
}

Shader_variant_report::Shader_variant_report(const std::filesystem::path& directory)
    : m_directory{directory}
{
    // A recording starts fresh: the results of an earlier recording here
    // (modules, pages, analytics, fragments) describe other programs.
    std::error_code error_code;
    std::filesystem::remove_all(m_directory / "variants", error_code);
    for (const char* file_name : {"index.html", "analytics.js", "fragments.js"}) {
        std::filesystem::remove(m_directory / file_name, error_code);
    }
    error_code.clear();
    std::filesystem::create_directories(m_directory / "variants", error_code);
    if (error_code) {
        log_program->error("Shader variant browser: cannot create {}: {}", (m_directory / "variants").string(), error_code.message());
    }
    log_program->info(
        "Shader variant browser: recording compiled programs into {} (earlier results there removed; the SPIR-V cache is not read while recording)",
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

    // The program's identity: its defines and those of its vertex format.
    const std::vector<std::pair<std::string, std::string>> variant_defines = create_info.get_variant_defines();

    // The key fragments.js matches programs by: JSON.stringify([name, defines]) of index.html's rows.
    nlohmann::json key_defines = nlohmann::json::array();
    for (const auto& [define_name, value] : variant_defines) {
        key_defines.push_back(nlohmann::json::array({define_name, value}));
    }
    const std::string program_key = nlohmann::json::array({create_info.name, std::move(key_defines)}).dump();

    // A program compiled again (same key) replaces its earlier record.
    std::size_t index = m_rows.size();
    for (const Row& existing : m_rows) {
        if ((existing.name == create_info.name) && (existing.defines == variant_defines)) {
            index = existing.index;
        }
    }
    std::string id = make_program_id(program_key);
    for (const Row& existing : m_rows) {
        if ((existing.id == id) && (existing.index != index)) {
            log_program->error("Shader variant browser: program id {} of {} collides with #{}; using a suffixed id", id, create_info.name, existing.index);
            id += fmt::format("_{}", index);
        }
    }
    Row row{
        .id                 = id,
        .index              = index,
        .name               = create_info.name,
        .defines            = variant_defines,
        .define_value_names = {},
        .stages             = {},
        .includes           = {}
    };
    for (const auto& [define_name, value] : variant_defines) {
        const Shader_define_constants* const constants = create_info.find_define_constants(define_name);
        if (constants != nullptr) {
            std::string expression = get_define_value_expression(*constants, value);
            if (!expression.empty() && (expression != value)) { // an empty mask's "0" adds nothing
                row.define_value_names.emplace_back(define_name, std::move(expression));
            }
        }
    }

    // Detail page: the program's sizes, then tabs (key, sources, SPIR-V before, after, one per analytics core).
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
            .optimized_instruction_count = count_instructions(stage.optimized_spirv),
            .main_name                   = stage.main_name
        };
        row.stages.push_back(stage_row);

        const std::string spv_name           = fmt::format("{}_{}.spv",           id, short_name);
        const std::string optimized_spv_name = fmt::format("{}_{}_optimized.spv", id, short_name);
        write_spirv_file(m_directory / "variants" / spv_name,           stage.spirv);
        write_spirv_file(m_directory / "variants" / optimized_spv_name, stage.optimized_spirv);
        write_text_file (m_directory / "variants" / fmt::format("{}_{}.glsl",          id, short_name), stage.main_source);
        write_text_file (m_directory / "variants" / fmt::format("{}_{}_preamble.glsl", id, short_name), stage.preamble);

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
    // paths during the parse), once each in first-include order (the parse
    // lists a file once per #include of it); the top-level files are shown per stage.
    for (const std::filesystem::path& path : include_paths) {
        const bool is_main = std::any_of(stages.begin(), stages.end(), [&path](const Shader_variant_report_stage& stage) {
            return stage.main_name == path.generic_string();
        });
        if (is_main || (std::find(row.includes.begin(), row.includes.end(), path.generic_string()) != row.includes.end())) {
            continue;
        }
        row.includes.push_back(path.generic_string());
        const std::optional<std::string> text = erhe::file::read("Shader_variant_report::add", path);
        sources_tab += fmt::format(
            "<details><summary>include - <code>{}</code></summary><pre>{}</pre></details>\n",
            html_escape(path.generic_string()),
            text.has_value() ? html_escape(text.value()) : std::string{"(unreadable)"}
        );
    }

    std::string define_rows;
    for (const auto& [define_name, value] : variant_defines) {
        define_rows += fmt::format(
            "<tr><td class=\"mono\">{}</td><td class=\"mono\">{}</td><td class=\"mono\">{}</td></tr>\n",
            html_escape(define_name), html_escape(value), html_escape(find_value_name(row.define_value_names, define_name))
        );
    }

    const std::string page = fmt::format(
        R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>{0} #{10}</title>
<style>{2}
body {{ padding: 12px 16px; }}
.tabs button {{ font: inherit; padding: 4px 12px; border: 1px solid var(--line); background: var(--head); color: var(--fg); cursor: pointer; }}
.tabs button.active {{ background: var(--select); }}
.tab {{ display: none; }} .tab.active {{ display: block; }}
summary {{ cursor: pointer; padding: 2px 0; }}
.columns {{ display: flex; gap: 24px; flex-wrap: wrap; align-items: flex-start; }}
</style></head><body>
<h2>{0} <span class="muted">#{10} ({1})</span></h2>
<p id="fragments" class="muted"></p>
<div class="columns">
<table><tr><th>Stage</th><th class="num">Bytes before</th><th class="num">Bytes after</th><th class="num">Instr. before</th><th class="num">Instr. after</th><th>.spv</th></tr>
{3}</table>
</div>
<p class="tabs"><button class="active" data-tab="key">Shader key</button><button data-tab="sources">Sources</button><button data-tab="before">SPIR-V before optimization</button><button data-tab="after">SPIR-V after optimization</button><button data-tab="analytics">GPU analytics</button></p>
<div class="tab active" id="key"><p class="muted">{4} defines, in the order the program declares them; the index shows the ones that vary among this program's variants as its key slots.</p><table><tr><th>Define</th><th>Value</th><th>Name</th></tr>{5}</table></div>
<div class="tab" id="sources">{6}</div>
<div class="tab" id="before">{7}</div>
<div class="tab" id="after">{8}</div>
<div class="tab" id="analytics"><p class="muted">No analytics recorded: run <code>py -3 scripts/shader_analytics.py &lt;browser directory&gt;</code> to write <code>analytics.js</code> (doc/erhe/graphics.md "Mali analytics", "PowerVR analytics").</p></div>
<script>
// The analytics script below replaces the analytics tab with one tab per core,
// so switching is delegated; the active tab is remembered across program pages.
const showTab = id => {{
  if (!document.getElementById(id)) return false;
  for (const b of document.querySelectorAll('.tabs button')) b.classList.toggle('active', b.dataset.tab === id);
  for (const t of document.querySelectorAll('.tab')) t.classList.toggle('active', t.id === id);
  return true;
}};
document.querySelector('.tabs').addEventListener('click', e => {{
  const button = e.target.closest('button'); if (!button) return;
  showTab(button.dataset.tab);
  try {{ localStorage.setItem('erhe_shader_variant_tab', button.dataset.tab); }} catch (error) {{}}
}});
</script>
<script src="../analytics.js"></script>
<script src="../fragments.js"></script>
<script>
// The program's fragments in a captured frame (fragments.js; doc/plans/shader_cost.md A3).
(function() {{
  const f = window.erhe_shader_fragments; if (!f || !f.programs) return;
  const entry = f.programs[{9}];
  const total = f.total_invocations || 0;
  document.getElementById('fragments').textContent = entry
    ? `Fragments in captured frame ${{f.frame_id}}: ${{entry.invocations}} (${{(100 * entry.invocations / Math.max(total, 1)).toFixed(2)}} % of the frame's ${{total}}) in ${{entry.ranges}} pipeline bind range(s)`
    : `Not drawn in captured frame ${{f.frame_id}} (fragments.js)`;
}})();
</script>
<script>
// The analytics tabs, one per core, rendered from analytics.js
// (scripts/shader_analytics.py) when it is present: per stage, per compiler variant a before /
// after / delta table (Mali: Main, or Position and Varying for vertex shaders;
// PowerVR: Main).
(function() {{
  const a = window.erhe_shader_analytics; if (!a || !a.cores) return;
  const esc = s => String(s).replace(/[&<>]/g, c => ({{'&':'&amp;','<':'&lt;','>':'&gt;'}})[c]);
  const fmt = v => (v === null || v === undefined) ? '-' : (typeof v === 'number' ? (Number.isInteger(v) ? String(v) : v.toFixed(2)) : String(v));
  const stageNames = {{vert: 'Vertex', frag: 'Fragment', comp: 'Compute', geom: 'Geometry', tesc: 'Tessellation control', tese: 'Tessellation evaluation'}};
  // sense: 1 lower is better, -1 higher is better, 0 neither
  const row = (label, x, y, sense = 1) => {{
    const d = (typeof x === 'number' && typeof y === 'number') ? (y - x) : null;
    const cls = (d === null || d === 0 || sense === 0) ? '' : ((d * sense) < 0 ? 'good' : 'bad');
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
  // PowerVR: per variant (Main) the compiler's counters, by their long names.
  const powervrSense = n => (n === 'USC Utilisation' || n === 'Max Parallel Instances') ? -1 : (n === 'Memory Used HWM' ? 0 : 1);
  const renderPowervrStage = s => {{
    const b = s.before, af = s.after;
    let out = '';
    for (const [label, x] of [['before', b], ['after', af]]) if (x && x.error) out += `<p class="bad">${{label}}: ${{esc(x.error)}}</p>`;
    const bv = (b && b.variants) || [], av = (af && af.variants) || [];
    const names = [...new Set([...bv.map(v => v.name), ...av.map(v => v.name)])];
    for (const name of names) {{
      const x = bv.find(v => v.name === name), y = av.find(v => v.name === name);
      const xs = new Map((x && x.counters) || []), ys = new Map((y && y.counters) || []);
      const counterNames = [...new Set([...xs.keys(), ...ys.keys()])];
      out += `<details open><summary>${{esc(name)}}</summary><table><tr><th></th><th class="num">Before</th><th class="num">After</th><th class="num">Delta</th></tr>`;
      out += counterNames.map(n => row(n, xs.get(n), ys.get(n), powervrSense(n))).join('') + '</table></details>';
    }}
    for (const [label, x] of [['before', b], ['after', af]]) if (x && x.command) out += `<p class="muted mono">${{label}}: ${{esc(x.command)}}</p>`;
    return out;
  }};
  const tabs = [];
  for (const coreName of Object.keys(a.cores)) {{
    const core = a.cores[coreName];
    const prog = core.programs && core.programs[{11}];
    if (!prog) continue;
    let html = '';
    tabs.push([coreName, () => html]);
    if (core.compiler === 'powervr') {{
      html += `<h3>${{esc(coreName)}} <span class="muted">BVNC ${{esc(core.bvnc || '')}}, ${{esc(core.architecture || '')}} (${{esc(core.soc || '')}}), PowerVR profiling compiler ${{esc(core.powervr_version || '')}}, ${{esc(core.generated || '')}}</span></h3>`;
      for (const stage of Object.keys(prog)) html += `<h4>${{stageNames[stage] || stage}}</h4>` + renderPowervrStage(prog[stage]);
      continue;
    }}
    html += `<h3>${{esc(coreName)}} ${{esc(core.revision || '')}} <span class="muted">${{esc(core.architecture || '')}}, driver ${{esc(core.driver || '')}}, Mali Offline Compiler ${{esc(core.malioc_version || '')}}, ${{esc(core.generated || '')}}</span></h3>`;
    for (const stage of Object.keys(prog)) html += `<h4>${{stageNames[stage] || stage}}</h4>` + renderStage(core, prog[stage]);
  }}
  if (!tabs.length) return;
  // One tab per core ("Mali G310", "PowerVR BXM-8-256") in place of the analytics placeholder.
  const placeholder = document.getElementById('analytics');
  document.querySelector('.tabs button[data-tab="analytics"]').remove();
  tabs.forEach(([coreName, html], i) => {{
    const id = 'analytics-' + coreName;
    document.querySelector('.tabs').insertAdjacentHTML('beforeend', `<button data-tab="${{esc(id)}}">${{esc(coreName.replace('-', ' '))}}</button>`);
    const div = document.createElement('div');
    div.className = 'tab'; div.id = id; div.innerHTML = html();
    placeholder.before(div);
  }});
  placeholder.remove();
}})();
try {{ const tab = localStorage.getItem('erhe_shader_variant_tab'); if (tab) showTab(tab); }} catch (error) {{}}
</script>
</body></html>
)HTML",
        html_escape(create_info.name), id, c_style,
        size_rows, variant_defines.size(), define_rows,
        sources_tab, before_tab, after_tab,
        json_string(program_key),
        index, json_string(id)
    );
    const std::filesystem::path page_path = m_directory / "variants" / fmt::format("{}.html", id);
    if (!erhe::file::write_file_via_temp(page_path, page)) {
        log_program->warn("Shader variant browser: failed to write {}", page_path.string());
    }

    if (index < m_rows.size()) {
        m_rows[index] = std::move(row);
    } else {
        m_rows.push_back(std::move(row));
    }
    write_index();
    write_manifest();
    if (m_add_callback) {
        m_add_callback();
    }
}

auto Shader_variant_report::find_program_id(
    const std::string_view                                  name,
    const std::vector<std::pair<std::string, std::string>>& defines
) -> std::optional<std::string>
{
    std::lock_guard<std::mutex> lock{m_mutex};
    for (const Row& row : m_rows) {
        if ((row.name == name) && (row.defines == defines)) {
            return row.id;
        }
    }
    return std::nullopt;
}

void Shader_variant_report::write_manifest() const
{
    nlohmann::json programs = nlohmann::json::array();
    for (const Row& row : m_rows) {
        nlohmann::json defines = nlohmann::json::array();
        for (const auto& [define_name, value] : row.defines) {
            defines.push_back(nlohmann::json::array({define_name, value}));
        }
        nlohmann::json define_value_names = nlohmann::json::object();
        for (const auto& [define_name, value_name] : row.define_value_names) {
            define_value_names[define_name] = value_name;
        }
        nlohmann::json stages = nlohmann::json::array();
        for (const Stage_row& stage : row.stages) {
            const char* const short_name = stage_short_name(stage.type);
            nlohmann::json stage_json{
                {"type",                        short_name},
                {"entry_point",                 "main"},
                {"main_name",                   stage.main_name},
                {"source",                      fmt::format("{}_{}.glsl", row.id, short_name)},
                {"preamble",                    fmt::format("{}_{}_preamble.glsl", row.id, short_name)},
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
            {"index",   row.index},
            {"name",    row.name},
            {"defines", std::move(defines)},
            {"define_value_names", std::move(define_value_names)},
            {"includes", row.includes},
            {"stages",  std::move(stages)}
        });
    }
    const nlohmann::json manifest{
        {"schema", {{"name", "erhe_shader_variants"}, {"version", 2}}},
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
    std::string ids{"["};
    for (const Row& row : m_rows) {
        ids += fmt::format("{}{}", (ids.size() > 1) ? "," : "", json_string(row.id));
        std::string defines{"["};
        for (const auto& [define_name, value] : row.defines) {
            if (defines.size() > 1) {
                defines += ',';
            }
            const std::string_view value_name = find_value_name(row.define_value_names, define_name);
            if (value_name.empty()) {
                defines += fmt::format("[{},{}]", json_string(define_name), json_string(value));
            } else {
                defines += fmt::format("[{},{},{}]", json_string(define_name), json_string(value), json_string(value_name));
            }
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
            row.index, json_string(row.name), defines, json_string(stages),
            bytes, optimized_bytes, instructions, optimized_instructions
        );
    }
    data += "]";
    ids  += "]";

    const std::string page = fmt::format(
        R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Shader variants</title>
<style>{0}
body {{ display: flex; height: 100vh; overflow: hidden; }}
#left {{ flex: 0 0 52%; display: flex; flex-direction: column; min-width: 0; }}
#divider {{ flex: 0 0 auto; display: flex; flex-direction: column; gap: 4px; padding: 6px 2px; border-left: 1px solid var(--line); border-right: 1px solid var(--line); background: var(--head); }}
#divider button {{ font: inherit; padding: 2px 5px; cursor: pointer; background: var(--bg); color: var(--fg); border: 1px solid var(--line); }}
body.table-collapsed #left {{ display: none; }}
body.details-collapsed #detail {{ display: none; }}
body.details-collapsed #left {{ flex: 1 1 auto; }}
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
<div id="divider"><button id="toggle-table"></button><button id="toggle-details"></button></div>
<iframe id="detail" title="Selected variant"></iframe>
<script src="analytics.js"></script>
<script src="fragments.js"></script>
<script>
const rows = {1};
// Row ids (variants/<id>.html, analytics.js keys): stable across sessions; column 0 is the recording order.
const ids = {2};
rows.forEach((r, i) => {{ r.id = ids[i]; }});
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
  r.valueNames = new Map(r[2].filter(d => d[2]).map(d => [d[0], d[2]]));
  r.keyFull = r[2].map(([n, v, vn]) => (v === '' ? n : `${{n}}=${{v}}`) + (vn ? ` (${{vn}})` : '')).join('\n');
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
  const valueName = r.valueNames.get(a.name);
  const full = value === undefined ? `${{a.name}} (absent)` : ((value === '' ? a.name : `${{a.name}}=${{value}}`) + (valueName ? ` (${{valueName}})` : ''));
  return `<span class="${{set ? '' : 'off'}}" style="width:${{a.width}}ch" title="${{esc(full)}}">${{set ? esc(compactLabel(a.name, value)) : '.'}}</span>`;
}}).join('');
// Analytics columns (analytics.js from scripts/shader_analytics.py): one
// group per compiler, from its default core, appended after column 8; each
// group records its first column index. Mali, per program: the fragment (or
// compute) Main total cycles before and after optimization with the bound
// pipelines, the vertex Position and Varying cycles after, the maximum work
// registers and minimum occupancy over the stages, the spill bytes summed,
// and flags. PowerVR: the fragment (or compute) Main cycles before and after, its
// instructions and samples after, the vertex Main cycles after, the maximum
// temporary registers and minimum USC utilisation over the stages, flags.
const analytics = window.erhe_shader_analytics;
const analyticsCores = (analytics && analytics.cores) || {{}};
const maliCoreName = analytics ? (analytics.default_cores ? analytics.default_cores.malioc : analytics.default_core) : null;
const powervrCoreName = (analytics && analytics.default_cores) ? analytics.default_cores.powervr : null;
const maxCycles = v => (v && v.total) ? Math.max(...v.total.filter(x => typeof x === 'number'), 0) : null;
function summarizeMali(prog) {{
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
function summarizePowervr(prog) {{
  const out = [null, null, null, null, null, null, null, ''];
  if (!prog) return out;
  let error = false, temps = null, util = null;
  const main = summary => (summary && !summary.error && summary.variants) ? summary.variants.find(x => x.name === 'Main') : null;
  for (const stage of Object.keys(prog)) {{
    const s = prog[stage], b = s.before, af = s.after || s.before;
    if ((b && b.error) || (af && af.error)) error = true;
    if (!af || af.error) continue;
    for (const v of af.variants || []) {{
      if (typeof v.temp_registers === 'number') temps = (temps === null) ? v.temp_registers : Math.max(temps, v.temp_registers);
      if (typeof v.utilisation === 'number') util = (util === null) ? v.utilisation : Math.min(util, v.utilisation);
    }}
    const mb = main(b), ma = main(af);
    if (stage === 'frag' || stage === 'comp') {{ out[0] = mb ? mb.cycles : null; out[1] = ma ? ma.cycles : null; out[2] = ma ? ma.instructions : null; out[3] = ma ? ma.samples : null; }}
    if (stage === 'vert') out[4] = ma ? ma.cycles : null;
  }}
  out[5] = temps; out[6] = util; out[7] = error ? 'error' : '';
  return out;
}}
const shortCore = name => name.replace(/^Mali-/, '').replace(/^PowerVR-/, '');
const columnGroups = [];
// columns: [label, numeric, bad(value)]; sum: the two columns the summary line totals;
// unit: of the fragment cost column (offset + 1) the fragments estimate multiplies
if (maliCoreName && analyticsCores[maliCoreName]) columnGroups.push({{
  core: maliCoreName, summarize: summarizeMali, flags: 8, sum: [0, 1], sumLabel: 'Main cycles', unit: 'cyc.',
  columns: [['Main cyc. before', true], ['Main cyc. after', true], ['Bound', false], ['Pos. cyc.', true], ['Var. cyc.', true], ['Regs', true],
    ['Occ. %', true, v => v !== null && v < 100], ['Spill B', true, v => v > 0], ['Flags', false, v => (v || '').includes('error')]]
}});
if (powervrCoreName && analyticsCores[powervrCoreName]) columnGroups.push({{
  core: powervrCoreName, summarize: summarizePowervr, flags: 7, sum: [0, 1], sumLabel: 'FS/CS cycles', unit: 'cyc.',
  columns: [['Cyc. before', true], ['Cyc. after', true], ['Instr.', true], ['Samples', true], ['VS cyc.', true], ['Temps', true],
    ['Util. %', true], ['Flags', false, v => (v || '').includes('error')]]
}});
// Fragments of a captured frame (fragments.js, MCP capture_fragment_invocations;
// doc/plans/shader_cost.md A3), matched by the key JSON.stringify([name, defines]):
// the program's invocations, their share of the frame, and per analytics
// group the frame's estimated work of the variant - fragments times its
// fragment Main cost after optimization - and that estimate's share.
const fragments = window.erhe_shader_fragments;
const fragmentPrograms = (fragments && fragments.programs) ? fragments.programs : null;
const rowKey = r => JSON.stringify([r[1], r[2].map(d => [d[0], d[1]])]);
{{
  let offset = 9;
  const coreGroups = columnGroups.slice();
  for (const g of coreGroups) {{
    g.offset = offset;
    g.tag = shortCore(g.core);
    const programs = analyticsCores[g.core].programs || {{}};
    for (const r of rows) r.push(...g.summarize(programs[r.id]));
    offset += g.columns.length;
  }}
  if (fragmentPrograms) {{
    const estimates = coreGroups.map(g => ({{ tag: g.tag, unit: g.unit, column: g.offset + 1 }}));
    const columns = [['Fragments', true], ['Frame %', true]];
    for (const e of estimates) columns.push([`${{e.tag}} est. ${{e.unit}}`, true], [`${{e.tag}} est. %`, true]);
    // A program recorded twice (same name and defines) shows its fragments on both rows, counted once.
    const matchedKeys = new Set();
    let total = 0;
    for (const r of rows) {{
      const key = rowKey(r);
      const entry = fragmentPrograms[key];
      const fragmentCount = entry ? entry.invocations : null;
      if (entry && !matchedKeys.has(key)) {{ total += fragmentCount; matchedKeys.add(key); }}
      const values = [fragmentCount, null];
      for (const e of estimates) values.push(((fragmentCount !== null) && (typeof r[e.column] === 'number')) ? fragmentCount * r[e.column] : null, null);
      r.push(...values);
    }}
    const base = offset;
    const g = {{ core: null, tag: '', title: `fragments.js: frame ${{fragments.frame_id}}`, offset: base, columns, sum: [0, 2] }};
    const frameTotal = fragments.total_invocations || total;
    const uniqueRows = rows.filter((r, i) => rows.findIndex(o => rowKey(o) === rowKey(r)) === i);
    const estimateTotals = estimates.map((e, i) => uniqueRows.reduce((sum, r) => sum + (r[base + 2 + (2 * i)] || 0), 0));
    for (const r of rows) {{
      if (r[base] !== null) r[base + 1] = 100 * r[base] / Math.max(frameTotal, 1);
      estimates.forEach((e, i) => {{ const v = r[base + 2 + (2 * i)]; if (v !== null) r[base + 3 + (2 * i)] = 100 * v / Math.max(estimateTotals[i], 1); }});
    }}
    g.summary = shownRows => {{
      const shownKeys = new Set();
      let shownFragments = 0;
      for (const r of shownRows) {{ const key = rowKey(r); if ((r[base] !== null) && !shownKeys.has(key)) {{ shownFragments += r[base]; shownKeys.add(key); }} }}
      return `, fragments ${{shownFragments}} of the frame's ${{frameTotal}}`;
    }};
    columnGroups.push(g);
    document.getElementById('bar').insertAdjacentHTML('beforeend',
      `<span class="muted">Fragments: frame ${{fragments.frame_id}}, ${{matchedKeys.size}} of ${{Object.keys(fragmentPrograms).length}} captured programs recorded here (${{(100 * total / Math.max(frameTotal, 1)).toFixed(1)}} % of the frame's invocations)</span>`);
    offset += columns.length;
  }}
  for (const g of columnGroups) {{
    document.querySelector('#table thead tr').insertAdjacentHTML('beforeend', g.columns.map(([label, numeric], i) =>
      `<th class="${{numeric ? 'num' : ''}}" data-col="${{g.offset + i}}" title="${{g.title || g.core}}">${{g.tag ? g.tag + ' ' : ''}}${{label}}</th>`).join(''));
  }}
  if (coreGroups.length) document.getElementById('bar').insertAdjacentHTML('beforeend',
    `<span class="muted">Analytics: ${{coreGroups.map(g => g.core).join(', ')}} (${{Object.keys(analyticsCores).length}} core(s) in analytics.js)</span>`);
}}
let sortCol = 0, sortAsc = true, selected = '';
const tbody = document.querySelector('#table tbody');
const filter = document.getElementById('filter');
const esc = s => String(s).replace(/[&<>]/g, c => ({{'&':'&amp;','<':'&lt;','>':'&gt;'}})[c]);
const num = v => (v === null || v === undefined) ? '-' : (Number.isInteger(v) ? String(v) : v.toFixed(2));
function render() {{
  const words = filter.value.toLowerCase().split(/\s+/).filter(w => w);
  const flagText = r => columnGroups.map(g => (g.flags === undefined) ? '' : (r[g.offset + g.flags] || '')).join(' ');
  const shown = rows.filter(r => words.every(w => (r[1] + ' ' + r.keyText + ' ' + r.keyFull + ' ' + flagText(r)).toLowerCase().includes(w)));
  shown.sort((a, b) => {{
    const x = (sortCol === 2) ? a.keyText : a[sortCol], y = (sortCol === 2) ? b.keyText : b[sortCol];
    const nx = (x === null || x === undefined) ? -1 : x, ny = (y === null || y === undefined) ? -1 : y;
    const c = (typeof nx === 'number' && typeof ny === 'number') ? (nx - ny) : String(nx).localeCompare(String(ny));
    return sortAsc ? c : -c;
  }});
  let before = 0, after = 0;
  const sums = columnGroups.map(() => [0, 0]);
  tbody.innerHTML = shown.map(r => {{
    before += r[4]; after += r[5];
    let cells = `<tr data-id="${{r.id}}" class="${{r.id === selected ? 'selected' : ''}}"><td class="num">${{r[0]}}</td><td>${{esc(r[1])}}</td><td class="key" title="${{esc(r.keyFull)}}">${{renderKey(r)}}</td><td>${{r[3]}}</td>` +
      `<td class="num">${{r[4]}}</td><td class="num">${{r[5]}}</td><td class="num">${{r[8].toFixed(1)}}</td><td class="num">${{r[6]}}</td><td class="num">${{r[7]}}</td>`;
    columnGroups.forEach((g, gi) => {{
      sums[gi][0] += r[g.offset + g.sum[0]] || 0; sums[gi][1] += r[g.offset + g.sum[1]] || 0;
      cells += g.columns.map(([label, numeric, bad], i) => {{
        const v = r[g.offset + i];
        const cls = [numeric ? 'num' : '', (bad && bad(v)) ? 'bad' : ''].join(' ');
        return `<td class="${{cls}}">${{numeric ? num(v) : esc(v)}}</td>`;
      }}).join('');
    }});
    return cells + '</tr>';
  }}).join('');
  document.getElementById('summary').textContent = `${{shown.length}} / ${{rows.length}} programs, SPIR-V ${{before}} -> ${{after}} bytes` +
    columnGroups.map((g, gi) => g.summary ? g.summary(shown) : `, ${{g.tag}} ${{g.sumLabel}} ${{num(sums[gi][0])}} -> ${{num(sums[gi][1])}}`).join('');
}}
document.querySelector('#table thead').addEventListener('click', e => {{
  const th = e.target.closest('th'); if (!th) return;
  const col = Number(th.dataset.col);
  if (col === sortCol) sortAsc = !sortAsc; else {{ sortCol = col; sortAsc = true; }}
  render();
}});
// Collapsible panes: collapsing one gives the other the window; never both.
const setPanes = state => {{
  document.body.classList.toggle('table-collapsed', state === 'details');
  document.body.classList.toggle('details-collapsed', state === 'table');
  const tableButton = document.getElementById('toggle-table'), detailsButton = document.getElementById('toggle-details');
  tableButton.textContent = state === 'details' ? '>' : '<';
  tableButton.title = state === 'details' ? 'Show the variant table' : 'Collapse the variant table';
  detailsButton.textContent = state === 'table' ? '<' : '>';
  detailsButton.title = state === 'table' ? 'Show the selected variant' : 'Collapse the selected variant';
  try {{ localStorage.setItem('erhe_shader_variant_panes', state); }} catch (error) {{}}
}};
const paneState = () => document.body.classList.contains('table-collapsed') ? 'details' : (document.body.classList.contains('details-collapsed') ? 'table' : 'both');
document.getElementById('toggle-table').addEventListener('click', () => setPanes(paneState() === 'details' ? 'both' : 'details'));
document.getElementById('toggle-details').addEventListener('click', () => setPanes(paneState() === 'table' ? 'both' : 'table'));
{{
  let state = 'both';
  try {{ state = localStorage.getItem('erhe_shader_variant_panes') || 'both'; }} catch (error) {{}}
  setPanes(['both', 'table', 'details'].includes(state) ? state : 'both');
}}
tbody.addEventListener('click', e => {{
  const tr = e.target.closest('tr'); if (!tr) return;
  if (paneState() === 'table') setPanes('both');
  selected = tr.dataset.id;
  document.getElementById('detail').src = `variants/${{selected}}.html`;
  render();
}});
filter.addEventListener('input', render);
render();
</script>
</body></html>
)HTML",
        c_style, data, ids
    );
    const std::filesystem::path index_path = m_directory / "index.html";
    if (!erhe::file::write_file_via_temp(index_path, page)) {
        log_program->warn("Shader variant browser: failed to write {}", index_path.string());
    }
}

} // namespace erhe::graphics
