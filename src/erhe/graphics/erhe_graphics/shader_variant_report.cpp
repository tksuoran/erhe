#include "erhe_graphics/shader_variant_report.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_file/file.hpp"

#include <fmt/format.h>
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
    const std::span<const std::filesystem::path> include_paths
)
{
    std::lock_guard<std::mutex> lock{m_mutex};

    const std::size_t id = m_rows.size();
    Row row{
        .id      = id,
        .name    = create_info.name,
        .defines = create_info.defines,
        .stages  = {}
    };

    // Detail page: the program's defines and sizes, then three tabs.
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
<details><summary>Defines ({4})</summary><table>{5}</table></details>
</div>
<p class="tabs"><button class="active" data-tab="sources">Sources</button><button data-tab="before">SPIR-V before optimization</button><button data-tab="after">SPIR-V after optimization</button></p>
<div class="tab active" id="sources">{6}</div>
<div class="tab" id="before">{7}</div>
<div class="tab" id="after">{8}</div>
<script>
for (const button of document.querySelectorAll('.tabs button')) {{
  button.addEventListener('click', () => {{
    for (const b of document.querySelectorAll('.tabs button')) b.classList.toggle('active', b === button);
    for (const t of document.querySelectorAll('.tab')) t.classList.toggle('active', t.id === button.dataset.tab);
  }});
}}
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
}

void Shader_variant_report::write_index() const
{
    // The table data as a JSON array; the page script renders, sorts and
    // filters it, and shows the selected program's page beside the table.
    std::string data{"[\n"};
    for (const Row& row : m_rows) {
        std::string defines;
        for (const auto& [define_name, value] : row.defines) {
            if (value == "0") {
                continue; // zero-valued axes clutter the key column
            }
            if (!defines.empty()) {
                defines += ' ';
            }
            defines += value.empty() ? define_name : fmt::format("{}={}", define_name, value);
        }
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
            row.id, json_string(row.name), json_string(defines), json_string(stages),
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
td.key {{ font-family: ui-monospace, monospace; font-size: 11px; word-break: break-all; }}
iframe {{ flex: 1; border: 0; height: 100%; background: var(--bg); }}
</style></head><body>
<div id="left">
<div id="bar"><strong>Shader variants</strong><span id="summary" class="muted"></span><input id="filter" placeholder="Filter by program or define (all words must match)"></div>
<div id="scroll"><table id="table"><thead><tr>
<th class="num" data-col="0">#</th><th data-col="1">Program</th><th data-col="2">Variant key (non-zero defines)</th><th data-col="3">Stages</th>
<th class="num" data-col="4">Bytes before</th><th class="num" data-col="5">Bytes after</th><th class="num" data-col="8">Bytes %</th>
<th class="num" data-col="6">Instr. before</th><th class="num" data-col="7">Instr. after</th>
</tr></thead><tbody></tbody></table></div>
</div>
<iframe id="detail" title="Selected variant"></iframe>
<script>
const rows = {1};
for (const r of rows) r.push(r[4] > 0 ? (100 * r[5] / r[4]) : 0);
let sortCol = 0, sortAsc = true, selected = -1;
const tbody = document.querySelector('#table tbody');
const filter = document.getElementById('filter');
const esc = s => s.replace(/[&<>]/g, c => ({{'&':'&amp;','<':'&lt;','>':'&gt;'}})[c]);
function render() {{
  const words = filter.value.toLowerCase().split(/\s+/).filter(w => w);
  const shown = rows.filter(r => words.every(w => (r[1] + ' ' + r[2]).toLowerCase().includes(w)));
  shown.sort((a, b) => {{ const x = a[sortCol], y = b[sortCol]; const c = (typeof x === 'number') ? (x - y) : String(x).localeCompare(String(y)); return sortAsc ? c : -c; }});
  let before = 0, after = 0;
  tbody.innerHTML = shown.map(r => {{
    before += r[4]; after += r[5];
    return `<tr data-id="${{r[0]}}" class="${{r[0] === selected ? 'selected' : ''}}"><td class="num">${{r[0]}}</td><td>${{esc(r[1])}}</td><td class="key">${{esc(r[2])}}</td><td>${{r[3]}}</td>` +
      `<td class="num">${{r[4]}}</td><td class="num">${{r[5]}}</td><td class="num">${{r[8].toFixed(1)}}</td><td class="num">${{r[6]}}</td><td class="num">${{r[7]}}</td></tr>`;
  }}).join('');
  document.getElementById('summary').textContent = `${{shown.length}} / ${{rows.length}} programs, SPIR-V ${{before}} -> ${{after}} bytes`;
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
