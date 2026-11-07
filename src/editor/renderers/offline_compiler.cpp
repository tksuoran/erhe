#include "renderers/offline_compiler.hpp"

#include "editor_log.hpp"

#include "erhe_file/file.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#elif !defined(ERHE_OS_ANDROID)
#   include <cerrno>
#   include <fcntl.h>
#   include <spawn.h>
#   include <sys/wait.h>
#   include <unistd.h>
extern char** environ;
#else
#   include <unistd.h>
#endif

namespace editor {

using json = nlohmann::json;

namespace {

constexpr double c_missing = std::numeric_limits<double>::quiet_NaN();

class Process_result
{
public:
    bool        started  {false};
    int         exit_code{0};  // negative: terminated by that signal
    std::string out;
    std::string err;
};

#if defined(_WIN32)
// One argument quoted for CommandLineToArgvW / the MSVC runtime.
[[nodiscard]] auto quote_windows_argument(const std::wstring& argument) -> std::wstring
{
    if (!argument.empty() && (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)) {
        return argument;
    }
    std::wstring result{L"\""};
    std::size_t backslashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            result.append((backslashes * 2) + 1, L'\\');
        } else {
            result.append(backslashes, L'\\');
        }
        backslashes = 0;
        result += c;
    }
    result.append(backslashes * 2, L'\\');
    result += L'"';
    return result;
}
#endif

// Runs a program with stdin empty and stdout / stderr captured through files
// in working_directory (no pipes: no deadlock between the two streams).
[[nodiscard]] auto run_process(const std::vector<std::string>& arguments, const std::filesystem::path& working_directory) -> Process_result
{
    Process_result result;
    const std::filesystem::path out_path = working_directory / "stdout.txt";
    const std::filesystem::path err_path = working_directory / "stderr.txt";
#if defined(_WIN32)
    SECURITY_ATTRIBUTES security{};
    security.nLength        = sizeof(security);
    security.bInheritHandle = TRUE;
    const HANDLE out_file = CreateFileW(out_path.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const HANDLE err_file = CreateFileW(err_path.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const HANDLE in_file  = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if ((out_file != INVALID_HANDLE_VALUE) && (err_file != INVALID_HANDLE_VALUE) && (in_file != INVALID_HANDLE_VALUE)) {
        std::wstring command_line;
        for (const std::string& argument : arguments) {
            if (!command_line.empty()) {
                command_line += L' ';
            }
            command_line += quote_windows_argument(std::filesystem::path{argument}.wstring());
        }
        STARTUPINFOW startup{};
        startup.cb         = sizeof(startup);
        startup.dwFlags    = STARTF_USESTDHANDLES;
        startup.hStdInput  = in_file;
        startup.hStdOutput = out_file;
        startup.hStdError  = err_file;
        PROCESS_INFORMATION process{};
        const std::wstring directory = working_directory.wstring();
        if (CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process) != 0) {
            WaitForSingleObject(process.hProcess, INFINITE);
            DWORD exit_code = 0;
            GetExitCodeProcess(process.hProcess, &exit_code);
            result.started   = true;
            result.exit_code = static_cast<int>(exit_code);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
    }
    for (const HANDLE handle : {out_file, err_file, in_file}) {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
#elif defined(ERHE_OS_ANDROID)
    // No offline compilers on device (Shader_analytics_service never runs there).
    static_cast<void>(arguments);
#else
    std::vector<char*> argv;
    for (const std::string& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, out_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_addopen(&actions, 2, err_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_addchdir_np(&actions, working_directory.c_str());
    pid_t pid = 0;
    const int spawn_error = posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawn_error == 0) {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0) {
            if (errno != EINTR) {
                break;
            }
        }
        result.started   = true;
        result.exit_code = WIFSIGNALED(status) ? -WTERMSIG(status) : WEXITSTATUS(status);
    }
#endif
    std::error_code error_code;
    if (std::filesystem::is_regular_file(out_path, error_code)) {
        result.out = erhe::file::read("Offline compiler stdout", out_path).value_or(std::string{});
    }
    if (std::filesystem::is_regular_file(err_path, error_code)) {
        result.err = erhe::file::read("Offline compiler stderr", err_path).value_or(std::string{});
    }
    return result;
}

// A scratch directory per run.
class Scratch_directory
{
public:
    Scratch_directory()
    {
        static std::atomic<uint64_t> s_counter{0};
        std::error_code error_code;
#if defined(_WIN32)
        const unsigned long process_id = GetCurrentProcessId();
#else
        const long process_id = static_cast<long>(getpid());
#endif
        path = std::filesystem::temp_directory_path(error_code) / fmt::format("erhe_shader_analytics_{}_{}", process_id, s_counter.fetch_add(1));
        std::filesystem::create_directories(path, error_code);
    }
    ~Scratch_directory()
    {
        std::error_code error_code;
        std::filesystem::remove_all(path, error_code);
    }
    Scratch_directory(const Scratch_directory&) = delete;
    auto operator=(const Scratch_directory&) -> Scratch_directory& = delete;

    std::filesystem::path path;
};

void hash_bytes(uint64_t& hash, const void* data, const std::size_t size)
{
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ull;
    }
}

void hash_text(uint64_t& hash, const std::string_view text)
{
    hash_bytes(hash, text.data(), text.size());
    hash_bytes(hash, "|", 1);
}

[[nodiscard]] auto stage_short_name(const erhe::graphics::Shader_type type) -> const char*
{
    switch (type) {
        case erhe::graphics::Shader_type::vertex_shader:          return "vert";
        case erhe::graphics::Shader_type::fragment_shader:        return "frag";
        case erhe::graphics::Shader_type::compute_shader:         return "comp";
        case erhe::graphics::Shader_type::geometry_shader:        return "geom";
        case erhe::graphics::Shader_type::tess_control_shader:    return "tesc";
        case erhe::graphics::Shader_type::tess_evaluation_shader: return "tese";
        default:                                                  return nullptr;
    }
}

auto write_spirv(const std::filesystem::path& path, const std::vector<unsigned int>& spirv) -> bool
{
    const std::string_view bytes{reinterpret_cast<const char*>(spirv.data()), spirv.size() * sizeof(unsigned int)};
    return erhe::file::write_file_via_temp(path, bytes);
}

[[nodiscard]] auto join_command(const std::vector<std::string>& arguments) -> std::string
{
    std::string command;
    for (const std::string& argument : arguments) {
        command += (command.empty() ? "" : " ") + argument;
    }
    return command;
}

// Non-empty lines of a compiler's text output, trailing blanks trimmed.
[[nodiscard]] auto split_lines(const std::string& text) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    std::size_t line_start = 0;
    while (line_start < text.size()) {
        std::size_t line_end = text.find('\n', line_start);
        if (line_end == std::string::npos) {
            line_end = text.size();
        }
        std::string line = text.substr(line_start, line_end - line_start);
        while (!line.empty() && ((line.back() == '\r') || (line.back() == ' '))) {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
        line_start = line_end + 1;
    }
    return lines;
}

[[nodiscard]] auto read_cached_report(const std::filesystem::path& path) -> std::optional<json>
{
    std::error_code error_code;
    if (!std::filesystem::is_regular_file(path, error_code)) {
        return std::nullopt;
    }
    const std::optional<std::string> text = erhe::file::read("Shader analytics report cache", path);
    if (!text.has_value()) {
        return std::nullopt;
    }
    json report = json::parse(text.value(), nullptr, false);
    if (report.is_discarded()) {
        return std::nullopt;
    }
    return report;
}

void write_cached_report(const std::filesystem::path& path, const json& report)
{
    std::error_code error_code;
    std::filesystem::create_directories(path.parent_path(), error_code);
    if (!erhe::file::write_file_via_temp(path, report.dump(1))) {
        log_programs->warn("Shader analytics: cannot write {}", path.generic_string());
    }
}

// The shape of malioc's error report (schema "error" 0).
[[nodiscard]] auto make_malioc_error_report(const std::string& message) -> json
{
    json errors = json::array();
    errors.push_back(message);
    json shader = json::object();
    shader["errors"] = std::move(errors);
    json shaders = json::array();
    shaders.push_back(std::move(shader));
    json report = json::object();
    report["schema"]  = json{{"name", "error"}, {"version", 0}};
    report["shaders"] = std::move(shaders);
    return report;
}

// malioc on the fragment module; the raw report plus an "erhe" block.
[[nodiscard]] auto run_malioc(const Offline_compiler_tools& tools, const std::vector<unsigned int>& spirv) -> json
{
    Scratch_directory scratch;
    const std::filesystem::path module_path = scratch.path / "module.spv";
    std::vector<std::string> arguments{
        tools.malioc.string(), "--vulkan", "--spirv", "-c", tools.mali_core, "--format", "json", module_path.string()
    };
    json report;
    if (!write_spirv(module_path, spirv)) {
        report = make_malioc_error_report("cannot write the module");
    } else {
        const Process_result result = run_process(arguments, scratch.path);
        const std::string&   output = result.out.find('{') != std::string::npos ? result.out : result.err;
        const std::size_t    start  = output.find('{');
        if (result.started && (start != std::string::npos)) {
            report = json::parse(std::string_view{output}.substr(start), nullptr, false);
        }
        if (!result.started || report.is_discarded() || !report.is_object()) {
            const std::string message = !result.started
                ? std::string{"cannot start malioc"}
                : (result.exit_code < 0)
                    ? fmt::format("compiler crashed with signal {}", -result.exit_code)
                    : fmt::format("compiler exited {} without a JSON report: {}", result.exit_code, output.substr(0, 300));
            report = make_malioc_error_report(message);
        }
        report["erhe"] = json{{"exit_code", result.exit_code}};
    }
    report["erhe"]["core"]           = tools.mali_core;
    report["erhe"]["malioc_version"] = tools.malioc_version;
    report["erhe"]["command"]        = join_command(arguments);
    return report;
}

// ProfilingCompilerCLIWrapper on the fragment module; the raw report plus an
// "erhe" block. The report lands in the working directory as <module>.json.
[[nodiscard]] auto run_powervr(const Offline_compiler_tools& tools, const std::vector<unsigned int>& spirv) -> json
{
    Scratch_directory scratch;
    const std::filesystem::path module_path = scratch.path / "frag.spv";
    std::vector<std::string> arguments{
        tools.powervr.string(), "-a", "spv", "-b", tools.powervr_bvnc, "-p", tools.powervr.parent_path().string(), "-i", module_path.string()
    };
    json report;
    std::vector<std::string> messages;
    int exit_code = 0;
    if (!write_spirv(module_path, spirv)) {
        messages.push_back("cannot write the module");
    } else {
        const Process_result result = run_process(arguments, scratch.path);
        exit_code = result.exit_code;
        const std::filesystem::path report_path = scratch.path / "frag.spv.json";
        std::error_code error_code;
        if (result.started && std::filesystem::is_regular_file(report_path, error_code)) {
            const std::optional<std::string> text = erhe::file::read("PowerVR report", report_path);
            if (text.has_value()) {
                report = json::parse(text.value(), nullptr, false);
            }
        }
        if (!result.started || report.is_discarded() || !report.is_object() || !report.contains("shaders") || !report["shaders"].is_object()) {
            // stdout carries the disassembly on success, the error line on failure.
            messages = split_lines(result.out);
            for (std::string& line : split_lines(result.err)) {
                messages.push_back(std::move(line));
            }
            if (!result.started) {
                messages.insert(messages.begin(), fmt::format("cannot start {}", c_powervr_executable));
            } else if (result.exit_code < 0) {
                messages.insert(messages.begin(), fmt::format("compiler crashed with signal {}", -result.exit_code));
            } else if (messages.empty()) {
                messages.push_back(fmt::format("compiler exited {} without a report", result.exit_code));
            }
            report = json{{"output", "failed"}, {"shaders", json::object()}};
        }
    }
    if (!report.is_object()) {
        report = json{{"output", "failed"}, {"shaders", json::object()}};
    }
    report["erhe"] = json{
        {"core",            tools.powervr_core},
        {"bvnc",            tools.powervr_bvnc},
        {"powervr_version", tools.powervr_version},
        {"command",         join_command(arguments)},
        {"exit_code",       exit_code},
        {"messages",        messages}
    };
    return report;
}

[[nodiscard]] auto number_of(const json& value) -> double
{
    if (value.is_number()) {
        return value.get<double>();
    }
    if (value.is_string()) {
        // "28", "17.9 %"
        const std::string& text = value.get_ref<const std::string&>();
        char* end = nullptr;
        const double number = std::strtod(text.c_str(), &end);
        if (end != text.c_str()) {
            return number;
        }
    }
    return c_missing;
}

// The fragment Main variant's cycles of a raw malioc report.
void extract_mali_metrics(const json& report, Shader_cost_metrics& metrics, std::string& error)
{
    const json& shaders = report.contains("shaders") ? report["shaders"] : json::array();
    if ((report.value("schema", json::object()).value("name", std::string{}) != "performance") || !shaders.is_array() || shaders.empty()) {
        std::string messages;
        if (shaders.is_array()) {
            for (const json& shader : shaders) {
                for (const json& message : shader.value("errors", json::array())) {
                    messages += (messages.empty() ? "" : "; ") + (message.is_string() ? message.get<std::string>() : message.dump());
                }
            }
        }
        error = fmt::format("malioc: {}", messages.empty() ? std::string{"no performance report"} : messages);
        return;
    }
    const json& shader = shaders[0];
    const auto variants = shader.find("variants");
    const json* main = nullptr;
    if ((variants != shader.end()) && variants->is_array()) {
        for (const json& variant : *variants) {
            if (variant.is_object() && (variant.value("name", std::string{}) == "Main")) {
                main = &variant;
            }
        }
    }
    if ((main == nullptr) || !main->contains("performance")) {
        error = "malioc: no Main variant";
        return;
    }
    const json& performance = (*main)["performance"];
    const json& names       = performance.value("pipelines", json::array());
    const json& counts      = performance.value("total_cycles", json::object()).value("cycle_count", json::array());
    double max_cycles = c_missing;
    const auto cycles_of = [&](const std::string_view pipeline) -> double {
        for (std::size_t i = 0; (i < names.size()) && (i < counts.size()); ++i) {
            if (names[i].is_string() && (names[i].get_ref<const std::string&>() == pipeline)) {
                return number_of(counts[i]);
            }
        }
        return c_missing;
    };
    for (const json& count : counts) {
        if (count.is_number()) {
            max_cycles = std::isnan(max_cycles) ? count.get<double>() : std::max(max_cycles, count.get<double>());
        }
    }
    metrics[static_cast<std::size_t>(Shader_cost_metric::mali_cycles)]            = max_cycles;
    metrics[static_cast<std::size_t>(Shader_cost_metric::mali_arithmetic_cycles)] = cycles_of("arith_total");
    metrics[static_cast<std::size_t>(Shader_cost_metric::mali_texture_cycles)]    = cycles_of("texture");
    metrics[static_cast<std::size_t>(Shader_cost_metric::mali_load_store_cycles)] = cycles_of("load_store");
}

// The fragment Main counters of a raw PowerVR report.
void extract_powervr_metrics(const json& report, Shader_cost_metrics& metrics, std::string& error)
{
    const json& shaders = report.contains("shaders") ? report["shaders"] : json::object();
    const auto  main    = shaders.is_object() ? shaders.find("Main") : shaders.end();
    if (!shaders.is_object() || (main == shaders.end()) || !main->is_object() || !main->contains("counters")) {
        std::string messages;
        for (const json& message : report.value("erhe", json::object()).value("messages", json::array())) {
            messages += (messages.empty() ? "" : "; ") + (message.is_string() ? message.get<std::string>() : message.dump());
        }
        error += fmt::format("{}PowerVR: {}", error.empty() ? "" : "; ", messages.empty() ? std::string{"no Main counters"} : messages);
        return;
    }
    const json& counters = (*main)["counters"];
    const auto counter = [&counters](const char* key) -> double {
        const auto entry = counters.find(key);
        return ((entry != counters.end()) && entry->is_object() && entry->contains("value")) ? number_of((*entry)["value"]) : c_missing;
    };
    metrics[static_cast<std::size_t>(Shader_cost_metric::powervr_cycles)]       = counter("uNaiveCycleCount");
    metrics[static_cast<std::size_t>(Shader_cost_metric::powervr_instructions)] = counter("uInstructionCount");
    metrics[static_cast<std::size_t>(Shader_cost_metric::powervr_samples)]      = counter("uSamples");
    metrics[static_cast<std::size_t>(Shader_cost_metric::powervr_registers)]    = counter("uTempRegisterCount");
}

[[nodiscard]] auto query_version_output(const std::filesystem::path& tool, const char* argument) -> std::string
{
    Scratch_directory scratch;
    const Process_result result = run_process({tool.string(), argument}, scratch.path);
    return result.started ? (result.out + result.err) : std::string{};
}

// "<label>: <value>" / "<label> v<value>" -> value
[[nodiscard]] auto find_after(const std::string& text, const std::string_view label) -> std::string
{
    std::size_t i = text.find(label);
    if (i == std::string::npos) {
        return {};
    }
    i += label.size();
    while ((i < text.size()) && ((text[i] == ' ') || (text[i] == ':') || (text[i] == '\t'))) {
        ++i;
    }
    std::size_t end = i;
    while ((end < text.size()) && !std::isspace(static_cast<unsigned char>(text[end]))) {
        ++end;
    }
    return text.substr(i, end - i);
}

} // anonymous namespace

namespace {

#if defined(_WIN32)
constexpr char c_path_list_separator = ';';
#else
constexpr char c_path_list_separator = ':';
#endif

// "$NAME", "${NAME}" and a leading "~" expand from the environment; an unset
// variable makes the entry unusable (empty result).
[[nodiscard]] auto expand_path_entry(const std::string_view entry) -> std::string
{
    std::string result;
    std::size_t i = 0;
    if (!entry.empty() && (entry.front() == '~') && ((entry.size() == 1) || (entry[1] == '/') || (entry[1] == '\\'))) {
        const char* home = std::getenv("HOME");
#if defined(_WIN32)
        if (home == nullptr) {
            home = std::getenv("USERPROFILE");
        }
#endif
        if (home == nullptr) {
            return {};
        }
        result = home;
        i = 1;
    }
    while (i < entry.size()) {
        if (entry[i] != '$') {
            result.push_back(entry[i++]);
            continue;
        }
        std::size_t name_begin = i + 1;
        std::size_t name_end   = name_begin;
        std::size_t next       = name_begin;
        if ((name_begin < entry.size()) && (entry[name_begin] == '{')) {
            ++name_begin;
            name_end = entry.find('}', name_begin);
            if (name_end == std::string_view::npos) {
                return {};
            }
            next = name_end + 1;
        } else {
            while ((name_end < entry.size()) && ((std::isalnum(static_cast<unsigned char>(entry[name_end])) != 0) || (entry[name_end] == '_'))) {
                ++name_end;
            }
            next = name_end;
        }
        const std::string name{entry.substr(name_begin, name_end - name_begin)};
        const char* const value = name.empty() ? nullptr : std::getenv(name.c_str());
        if (value == nullptr) {
            return {};
        }
        result += value;
        i = next;
    }
    return result;
}

// An entry is the executable itself or a directory holding it.
void add_candidate(
    const std::string_view                source,
    const std::string_view                entry,
    const std::string&                    file_name,
    Offline_compiler_candidates&          candidates
)
{
    if (entry.empty()) {
        return;
    }
    const std::string expanded = expand_path_entry(entry);
    candidates.searched.push_back(fmt::format("{} '{}'", source, entry));
    if (expanded.empty()) {
        return;
    }
    std::error_code error_code;
    std::filesystem::path candidate{expanded};
    if (std::filesystem::is_directory(candidate, error_code)) {
        candidate /= file_name;
    }
    if (!std::filesystem::is_regular_file(candidate, error_code)) {
        return;
    }
    const auto canonical_or_self = [](const std::filesystem::path& path) -> std::filesystem::path {
        std::error_code canonical_error;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, canonical_error);
        return canonical_error ? path : canonical;
    };
    const std::filesystem::path key = canonical_or_self(candidate);
    for (const std::filesystem::path& existing : candidates.paths) {
        if (canonical_or_self(existing) == key) {
            return; // the same executable through another entry
        }
    }
    candidates.paths.push_back(candidate);
}

void add_path_list(const std::string_view source, const std::string_view list, const std::string& file_name, Offline_compiler_candidates& candidates)
{
    std::size_t start = 0;
    while (start <= list.size()) {
        std::size_t end = list.find(c_path_list_separator, start);
        if (end == std::string_view::npos) {
            end = list.size();
        }
        add_candidate(source, list.substr(start, end - start), file_name, candidates);
        start = end + 1;
    }
}

// Dotted numeric components compared in order; a missing component is 0.
[[nodiscard]] auto is_newer_version(const std::string_view lhs, const std::string_view rhs) -> bool
{
    std::size_t i = 0;
    std::size_t j = 0;
    while ((i < lhs.size()) || (j < rhs.size())) {
        unsigned long long a = 0;
        unsigned long long b = 0;
        while ((i < lhs.size()) && (std::isdigit(static_cast<unsigned char>(lhs[i])) != 0)) { a = (a * 10) + static_cast<unsigned long long>(lhs[i++] - '0'); }
        while ((j < rhs.size()) && (std::isdigit(static_cast<unsigned char>(rhs[j])) != 0)) { b = (b * 10) + static_cast<unsigned long long>(rhs[j++] - '0'); }
        if (a != b) {
            return a > b;
        }
        while ((i < lhs.size()) && (std::isdigit(static_cast<unsigned char>(lhs[i])) == 0)) { ++i; }
        while ((j < rhs.size()) && (std::isdigit(static_cast<unsigned char>(rhs[j])) == 0)) { ++j; }
    }
    return false;
}

[[nodiscard]] auto query_malioc_version(const std::filesystem::path& malioc) -> std::string
{
    // "Mali Offline Compiler v2026.5.0 (Build 922df8)"
    std::string version = find_after(query_version_output(malioc, "--version"), "Mali Offline Compiler");
    if (!version.empty() && (version.front() == 'v')) {
        version.erase(0, 1);
    }
    return version;
}

[[nodiscard]] auto is_bvnc(const std::string_view text) -> bool
{
    int dots = 0;
    bool digit_before = false;
    for (const char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            digit_before = true;
        } else if ((c == '.') && digit_before) {
            ++dots;
            digit_before = false;
        } else {
            return false;
        }
    }
    return (dots == 3) && digit_before;
}

// "PowerVR-BXM-8-256" or "PowerVR-35.3.408.101" -> the BVNC, through the
// wrapper's -h list ("35.3.408.101: Series B BXM-8-256, MediaTek Dimensity 930").
// Empty, with the reason in error, when the core names no single BVNC.
[[nodiscard]] auto resolve_powervr_bvnc(const std::string& help_text, const std::string& core, std::string& error) -> std::string
{
    const std::string_view name = std::string_view{core}.substr(std::string_view{"PowerVR-"}.size());
    if (is_bvnc(name)) {
        return std::string{name};
    }
    std::vector<std::string> bvncs;
    for (const std::string& line : split_lines(help_text)) {
        const std::size_t first = line.find_first_not_of(" \t");
        const std::size_t colon = line.find(':');
        if ((first == std::string::npos) || (colon == std::string::npos) || (colon < first)) {
            continue;
        }
        const std::string bvnc = line.substr(first, colon - first);
        if (!is_bvnc(bvnc)) {
            continue;
        }
        // The GPU is the last word before the first comma: " Series B BXM-8-256, MediaTek ..."
        const std::size_t comma = line.find(',', colon);
        std::string_view  words{std::string_view{line}.substr(colon + 1, (comma == std::string::npos) ? std::string_view::npos : (comma - colon - 1))};
        while (!words.empty() && (std::isspace(static_cast<unsigned char>(words.back())) != 0)) {
            words.remove_suffix(1);
        }
        const std::size_t last_space = words.find_last_of(" \t");
        const std::string_view gpu = (last_space == std::string_view::npos) ? words : words.substr(last_space + 1);
        if (!gpu.empty() && (gpu == name)) {
            bvncs.push_back(bvnc);
        }
    }
    if (bvncs.size() == 1) {
        return bvncs.front();
    }
    error = bvncs.empty()
        ? fmt::format("-h names no GPU {}", name)
        : fmt::format("{} names {} BVNCs; use PowerVR-<BVNC>", name, bvncs.size());
    return {};
}

// The SPIR-V compiler library of a BVNC beside the wrapper ("libspvcompiler_sdk.35.3.408.101.so").
[[nodiscard]] auto find_powervr_library(const std::filesystem::path& wrapper, const std::string& bvnc) -> std::filesystem::path
{
    const std::string pattern = fmt::format("spvcompiler_sdk.{}.", bvnc);
    std::error_code error_code;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{wrapper.parent_path(), error_code}) {
        if (entry.path().filename().string().find(pattern) != std::string::npos) {
            return entry.path();
        }
    }
    return {};
}

// The wrapper has no version option; the library embeds its DDK build ("rogueddk MAIN@6451761").
[[nodiscard]] auto read_powervr_library_version(const std::filesystem::path& library) -> std::string
{
    const std::optional<std::string> bytes = erhe::file::read("PowerVR compiler library", library);
    if (!bytes.has_value()) {
        return {};
    }
    constexpr std::string_view marker{"rogueddk "};
    const std::size_t start = bytes.value().find(marker);
    if (start == std::string::npos) {
        return {};
    }
    std::size_t end = start + marker.size();
    while ((end < bytes.value().size()) && (std::isgraph(static_cast<unsigned char>(bytes.value()[end])) != 0)) {
        ++end;
    }
    return bytes.value().substr(start + marker.size(), end - start - marker.size());
}

} // anonymous namespace

auto find_offline_compiler_candidates(
    const std::span<const std::string> configured_paths,
    const char* const                  env_name,
    const char* const                  exe_name
) -> Offline_compiler_candidates
{
    Offline_compiler_candidates candidates;
#if defined(_WIN32)
    const std::string file_name = std::string{exe_name} + ".exe";
#else
    const std::string file_name{exe_name};
#endif
    for (const std::string& entry : configured_paths) {
        add_candidate("settings", entry, file_name, candidates);
    }
    if (const char* env = std::getenv(env_name); env != nullptr) {
        add_path_list(env_name, env, file_name, candidates);
    }
    if (const char* path_variable = std::getenv("PATH"); path_variable != nullptr) {
        const std::size_t searched_before = candidates.searched.size();
        add_path_list("PATH", path_variable, file_name, candidates);
        // One summary entry for PATH, not every directory of it.
        candidates.searched.resize(searched_before);
        candidates.searched.push_back("PATH");
    }
    return candidates;
}

void query_offline_compiler_versions(Offline_compiler_tools& tools)
{
    tools.malioc.clear();
    for (const std::filesystem::path& candidate : tools.malioc_candidates) {
        const std::string version = query_malioc_version(candidate);
        if (version.empty()) {
            log_programs->warn("Shader analytics: malioc candidate {}: --version reported no version; skipped", candidate.generic_string());
            continue;
        }
        log_programs->info("Shader analytics: malioc candidate {} version {}", candidate.generic_string(), version);
        if (tools.malioc.empty() || is_newer_version(version, tools.malioc_version)) {
            tools.malioc         = candidate;
            tools.malioc_version = version;
        }
    }
    if (!tools.malioc.empty()) {
        log_programs->info("Shader analytics: using malioc {} (version {})", tools.malioc.generic_string(), tools.malioc_version);
    } else if (!tools.malioc_candidates.empty()) {
        log_programs->warn("Shader analytics: no malioc candidate reported a version; Mali analysis off");
    }

    tools.powervr.clear();
    const bool powervr_core_valid = std::string_view{tools.powervr_core}.starts_with("PowerVR-");
    if (!tools.powervr_candidates.empty() && !powervr_core_valid) {
        log_programs->warn("Shader analytics: the core {} is not PowerVR-<GPU> or PowerVR-<BVNC>; PowerVR analysis off", tools.powervr_core);
    }
    for (const std::filesystem::path& candidate : powervr_core_valid ? std::span<const std::filesystem::path>{tools.powervr_candidates} : std::span<const std::filesystem::path>{}) {
        std::string error;
        const std::string bvnc = resolve_powervr_bvnc(query_version_output(candidate, "-h"), tools.powervr_core, error);
        if (bvnc.empty()) {
            log_programs->warn("Shader analytics: {} candidate {}: {}; skipped", c_powervr_executable, candidate.generic_string(), error);
            continue;
        }
        const std::filesystem::path library = find_powervr_library(candidate, bvnc);
        if (library.empty()) {
            log_programs->warn("Shader analytics: {} candidate {}: no SPIR-V compiler library for BVNC {} beside it; skipped", c_powervr_executable, candidate.generic_string(), bvnc);
            continue;
        }
        const std::string version = read_powervr_library_version(library);
        if (version.empty()) {
            log_programs->warn("Shader analytics: {} candidate {}: {} names no rogueddk build; skipped", c_powervr_executable, candidate.generic_string(), library.generic_string());
            continue;
        }
        log_programs->info("Shader analytics: {} candidate {} BVNC {} version {}", c_powervr_executable, candidate.generic_string(), bvnc, version);
        if (tools.powervr.empty() || is_newer_version(version, tools.powervr_version)) {
            tools.powervr         = candidate;
            tools.powervr_version = version;
            tools.powervr_bvnc    = bvnc;
        }
    }
    if (!tools.powervr.empty()) {
        log_programs->info("Shader analytics: using {} {} (BVNC {}, version {})", c_powervr_executable, tools.powervr.generic_string(), tools.powervr_bvnc, tools.powervr_version);
    } else if (!tools.powervr_candidates.empty() && powervr_core_valid) {
        log_programs->warn("Shader analytics: no {} candidate compiles for {}; PowerVR analysis off", c_powervr_executable, tools.powervr_core);
    }
}

namespace {

// The raw report of each compiler from the cache, else from a run that is
// then cached; keyed by the compiler, its versions, the core and the module
// bytes, so a module is compiled once per compiler version.
[[nodiscard]] auto get_malioc_report(const Offline_compiler_tools& tools, const std::vector<unsigned int>& spirv, const std::filesystem::path& cache_directory) -> json
{
    uint64_t hash = 0xcbf29ce484222325ull;
    hash_text(hash, "malioc");
    hash_text(hash, tools.malioc_version);
    hash_text(hash, tools.mali_core);
    hash_bytes(hash, spirv.data(), spirv.size() * sizeof(unsigned int));
    const std::filesystem::path path = cache_directory / fmt::format("{:016x}.{}.malioc.json", hash, tools.mali_core);
    std::optional<json> report = read_cached_report(path);
    if (!report.has_value()) {
        report = run_malioc(tools, spirv);
        write_cached_report(path, report.value());
    }
    return std::move(report.value());
}

[[nodiscard]] auto get_powervr_report(const Offline_compiler_tools& tools, const std::vector<unsigned int>& spirv, const std::filesystem::path& cache_directory) -> json
{
    uint64_t hash = 0xcbf29ce484222325ull;
    hash_text(hash, "powervr");
    hash_text(hash, tools.powervr_version);
    hash_text(hash, tools.powervr_bvnc);
    hash_bytes(hash, spirv.data(), spirv.size() * sizeof(unsigned int));
    const std::filesystem::path path = cache_directory / fmt::format("{:016x}.{}.powervr.json", hash, tools.powervr_core);
    std::optional<json> report = read_cached_report(path);
    if (!report.has_value()) {
        report = run_powervr(tools, spirv);
        write_cached_report(path, report.value());
    }
    return std::move(report.value());
}

void analyze_program_reports(
    const Offline_compiler_tools&                 tools,
    const Offline_compiler_stage&                 fragment_stage,
    const std::filesystem::path&                  cache_directory,
    Shader_cost_metrics&                          metrics,
    std::string&                                  error
)
{
    if (!tools.malioc.empty()) {
        extract_mali_metrics(get_malioc_report(tools, fragment_stage.spirv, cache_directory), metrics, error);
    }
    if (!tools.powervr.empty()) {
        extract_powervr_metrics(get_powervr_report(tools, fragment_stage.spirv, cache_directory), metrics, error);
    }
}

} // anonymous namespace

auto analyze_program(
    const Offline_compiler_tools&           tools,
    const std::span<const Offline_compiler_stage> stages,
    const std::filesystem::path&            cache_directory,
    std::string&                            error
) -> Shader_cost_metrics
{
    Shader_cost_metrics metrics;
    metrics.fill(c_missing);

    const Offline_compiler_stage* fragment = nullptr;
    for (const Offline_compiler_stage& stage : stages) {
        if (stage.type == erhe::graphics::Shader_type::fragment_shader) {
            fragment = &stage;
        }
    }
    if (fragment == nullptr) {
        return metrics;
    }
    // The reports are the compilers' output: a shape this parser does not
    // expect is an analysis failure of this program, not of the editor.
    try {
        analyze_program_reports(tools, *fragment, cache_directory, metrics, error);
    } catch (const json::exception& exception) {
        error += fmt::format("{}unexpected report: {}", error.empty() ? "" : "; ", exception.what());
    }
    return metrics;
}


// ---------------------------------------------------------------------------
// Full program analytics: the summaries of scripts/shader_analytics.py
// (summarize_malioc_report(), summarize_powervr_report()), field for field, so the Shader Variants window
// reads in-editor results and analytics.js alike.

namespace {

using ojson = nlohmann::ordered_json;

[[nodiscard]] auto value_or_null(const json& object, const char* key) -> ojson
{
    if (!object.is_object()) {
        return nullptr;
    }
    const auto i = object.find(key);
    return (i == object.end()) ? ojson(nullptr) : ojson::parse(i->dump());
}

[[nodiscard]] auto to_ojson(const json& value) -> ojson
{
    return ojson::parse(value.dump());
}

[[nodiscard]] auto as_message_text(const json& value) -> std::string
{
    return value.is_string() ? value.get<std::string>() : value.dump();
}

// SPIR-V capability names the malioc error reports name by number.
[[nodiscard]] auto capability_name(const int number) -> const char*
{
    switch (number) {
        case 1:    return "Shader";
        case 11:   return "Float16";
        case 22:   return "Int64";
        case 23:   return "Int16";
        case 32:   return "ClipDistance";
        case 33:   return "CullDistance";
        case 45:   return "SampledCubeArray";
        case 49:   return "StorageImageExtendedFormats";
        case 50:   return "ImageQuery";
        case 51:   return "DerivativeControl";
        case 61:   return "GroupNonUniform";
        case 4423: return "SubgroupBallotKHR";
        case 4427: return "DrawParameters";
        case 4433: return "StorageBuffer16BitAccess";
        case 4434: return "UniformAndStorageBuffer16BitAccess";
        case 4437: return "DeviceGroup";
        case 4439: return "MultiView";
        case 4441: return "VariablePointersStorageBuffer";
        case 4472: return "RayQueryKHR";
        case 5301: return "ShaderNonUniform";
        case 5302: return "RuntimeDescriptorArray";
        case 5345: return "VulkanMemoryModel";
        case 5347: return "PhysicalStorageBufferAddresses";
        case 5379: return "DemoteToHelperInvocation";
        case 5391: return "RayQueryPositionFetchKHR";
        default:   return nullptr;
    }
}

// "Unsupported capability (5302)" -> "Unsupported capability (5302 RuntimeDescriptorArray)"
[[nodiscard]] auto resolve_capability_names(std::string message) -> std::string
{
    constexpr std::string_view marker{"Unsupported capability ("};
    std::size_t start = 0;
    for (;;) {
        const std::size_t i = message.find(marker, start);
        if (i == std::string::npos) {
            return message;
        }
        const std::size_t j = message.find(')', i);
        if (j == std::string::npos) {
            return message;
        }
        const std::string number = message.substr(i + marker.size(), j - i - marker.size());
        const bool digits = !number.empty() && std::all_of(number.begin(), number.end(), [](const char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
        if (digits && (number.size() < 9)) {
            if (const char* name = capability_name(std::stoi(number)); name != nullptr) {
                message.insert(j, std::string{" "} + name);
            }
        }
        start = j + 1;
    }
}

// Returns (summary, core_info or null).
[[nodiscard]] auto summarize_malioc_report(const json& report) -> std::pair<ojson, ojson>
{
    const json erhe = report.value("erhe", json::object());
    ojson summary{
        {"command",           erhe.value("command", std::string{})},
        {"error",             nullptr},
        {"variants",          ojson::array()},
        {"properties",        ojson::object()},
        {"attribute_streams", nullptr},
        {"warnings",          ojson::array()},
        {"notes",             ojson::array()}
    };
    const json shaders = report.value("shaders", json::array());
    const bool performance = report.value("schema", json::object()).value("name", std::string{}) == "performance";
    if (!performance || !shaders.is_array() || shaders.empty()) {
        std::string messages;
        if (shaders.is_array()) {
            for (const json& shader : shaders) {
                for (const json& error : shader.value("errors", json::array())) {
                    messages += (messages.empty() ? "" : "; ") + resolve_capability_names(as_message_text(error));
                }
            }
        }
        summary["error"] = messages.empty() ? std::string{"no performance report"} : messages;
        return {std::move(summary), nullptr};
    }
    const json& shader   = shaders[0];
    const json  hardware = shader.value("hardware", json::object());
    std::vector<std::string> pipelines;
    for (const json& pipeline : hardware.value("pipelines", json::array())) {
        pipelines.push_back(pipeline.value("name", std::string{}));
    }
    ojson core_info{
        {"revision",     value_or_null(hardware, "revision")},
        {"architecture", value_or_null(hardware, "architecture")},
        {"driver",       value_or_null(shader, "driver")},
        {"pipelines",    pipelines}
    };
    for (const json& property : shader.value("properties", json::array())) {
        summary["properties"][property.value("name", std::string{})] = value_or_null(property, "value");
    }
    for (const json& variant : shader.value("variants", json::array())) {
        json properties = json::object();
        for (const json& property : variant.value("properties", json::array())) {
            properties[property.value("name", std::string{})] = property.contains("value") ? property["value"] : json(nullptr);
        }
        const json performance_block = variant.value("performance", json::object());
        // A variant's cycle_count arrays follow the variant's own pipelines
        // order; the summary stores them in the hardware block's order.
        std::vector<std::string> variant_pipelines = pipelines;
        if (performance_block.contains("pipelines") && performance_block["pipelines"].is_array()) {
            variant_pipelines.clear();
            for (const json& name : performance_block["pipelines"]) {
                variant_pipelines.push_back(name.is_string() ? name.get<std::string>() : name.dump());
            }
        }
        const auto cycles = [&](const char* key) -> std::pair<ojson, ojson> {
            const json entry = performance_block.value(key, json::object());
            ojson counts = value_or_null(entry, "cycle_count");
            if (!counts.is_null() && (variant_pipelines != pipelines)) {
                ojson reordered = ojson::array();
                for (const std::string& name : pipelines) {
                    ojson value = nullptr;
                    for (std::size_t i = 0; (i < variant_pipelines.size()) && (i < counts.size()); ++i) {
                        if (variant_pipelines[i] == name) {
                            value = counts[i];
                        }
                    }
                    reordered.push_back(std::move(value));
                }
                counts = std::move(reordered);
            }
            ojson bound = ojson::array();
            for (const json& unit : entry.value("bound_pipelines", json::array())) {
                if (!unit.is_null() && !(unit.is_string() && unit.get_ref<const std::string&>().empty())) {
                    bound.push_back(to_ojson(unit));
                }
            }
            return {std::move(counts), std::move(bound)};
        };
        auto [total,    bound_total]    = cycles("total_cycles");
        auto [shortest, bound_shortest] = cycles("shortest_path_cycles");
        auto [longest,  bound_longest]  = cycles("longest_path_cycles");
        summary["variants"].push_back(ojson{
            {"name",              value_or_null(variant, "name")},
            {"work_registers",    value_or_null(properties, "work_registers_used")},
            {"uniform_registers", value_or_null(properties, "uniform_registers_used")},
            {"occupancy",         value_or_null(properties, "thread_occupancy")},
            {"stack_size",        value_or_null(properties, "stack_size")},
            {"has_spilling",      value_or_null(properties, "has_stack_spilling")},
            {"spill_bytes",       value_or_null(properties, "stack_spill_bytes")},
            {"alloca_bytes",      value_or_null(properties, "stack_alloca_bytes")},
            {"fp16",              value_or_null(properties, "fp16_arithmetic")},
            {"total",             std::move(total)},
            {"bound_total",       std::move(bound_total)},
            {"shortest",          std::move(shortest)},
            {"bound_shortest",    std::move(bound_shortest)},
            {"longest",           std::move(longest)},
            {"bound_longest",     std::move(bound_longest)}
        });
    }
    summary["attribute_streams"] = value_or_null(shader, "attribute_streams");
    for (const json& warning : shader.value("warnings", json::array())) {
        summary["warnings"].push_back(as_message_text(warning));
    }
    for (const json& note : shader.value("notes", json::array())) {
        summary["notes"].push_back(as_message_text(note));
    }
    return {std::move(summary), std::move(core_info)};
}

[[nodiscard]] auto powervr_counter_label(const std::string& key, const json& entry) -> std::string
{
    const std::string long_name = entry.value("long_name", std::string{});
    if (!long_name.empty()) {
        return long_name;
    }
    return (key == "uNaiveCycleCount") ? std::string{"Naive Cycle Count"} : key;
}

// Returns (summary, core_info or null).
[[nodiscard]] auto summarize_powervr_report(const json& report) -> std::pair<ojson, ojson>
{
    const json erhe = report.value("erhe", json::object());
    ojson summary{
        {"command",  erhe.value("command", std::string{})},
        {"error",    nullptr},
        {"variants", ojson::array()},
        {"messages", to_ojson(erhe.value("messages", json::array()))}
    };
    const json shaders = report.value("shaders", json::object());
    if (!shaders.is_object() || shaders.empty()) {
        std::string text;
        for (const json& message : erhe.value("messages", json::array())) {
            text += (text.empty() ? "" : "; ") + as_message_text(message);
        }
        summary["error"] = text.empty() ? std::string{"no report"} : text;
        return {std::move(summary), nullptr};
    }
    const json information = report.value("information", json::object());
    ojson core_info{
        {"gpu",          value_or_null(information, "gpu")},
        {"architecture", value_or_null(information, "architecture")},
        {"soc",          value_or_null(information, "soc")}
    };
    for (const auto& [name, shader] : shaders.items()) {
        const json counters = shader.is_object() ? shader.value("counters", json::object()) : json::object();
        ojson counter_pairs = ojson::array();
        for (const auto& [key, entry] : counters.items()) {
            if (entry.is_object()) {
                counter_pairs.push_back(ojson::array({powervr_counter_label(key, entry), value_or_null(entry, "value")}));
            }
        }
        const auto counter = [&counters](const char* key) -> ojson {
            const auto i = counters.find(key);
            return ((i != counters.end()) && i->is_object()) ? value_or_null(*i, "value") : ojson(nullptr);
        };
        summary["variants"].push_back(ojson{
            {"name",           name},
            {"counters",       std::move(counter_pairs)},
            {"cycles",         counter("uNaiveCycleCount")},
            {"instructions",   counter("uInstructionCount")},
            {"temp_registers", counter("uTempRegisterCount")},
            {"samples",        counter("uSamples")},
            {"utilisation",    counter("fUSCUtilisation")}
        });
    }
    return {std::move(summary), std::move(core_info)};
}

[[nodiscard]] auto generated_now() -> std::string
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local);
    return buffer;
}

} // anonymous namespace

auto analyze_program_stages(
    const Offline_compiler_tools&                         tools,
    const std::span<const Offline_compiler_program_stage> stages,
    const std::filesystem::path&                          cache_directory
) -> nlohmann::ordered_json
{
    ojson result = ojson::object();
    const std::string generated = generated_now();
    const auto stage_key = [](const erhe::graphics::Shader_type type) -> std::string {
        const char* const name = stage_short_name(type);
        return (name != nullptr) ? std::string{name} : std::string{"?"};
    };
    // The reports are the compilers' output: a shape this code does not
    // expect is a failed analysis of this program, not of the editor.
    try {
        // Mali and PowerVR: one compile per stage module.
        const auto per_module = [&](
            const char*        compiler,
            const std::string& core,
            ojson              block,
            const std::function<std::pair<ojson, ojson>(const std::vector<unsigned int>&)>& summarize
        ) {
            ojson program = ojson::object();
            bool  core_info_set = false;
            for (const Offline_compiler_program_stage& stage : stages) {
                ojson stage_block{{"before", nullptr}, {"after", nullptr}};
                for (const auto& [slot, module] : {std::pair{"before", &stage.before}, std::pair{"after", &stage.after}}) {
                    if (module->empty()) {
                        continue;
                    }
                    auto [summary, core_info] = summarize(*module);
                    stage_block[slot] = std::move(summary);
                    if (!core_info.is_null() && !core_info_set) {
                        for (auto& [key, value] : core_info.items()) {
                            block[key] = value;
                        }
                        core_info_set = true;
                    }
                }
                program[stage_key(stage.type)] = std::move(stage_block);
            }
            block["compiler"] = compiler;
            result[core] = ojson{{"block", std::move(block)}, {"program", std::move(program)}};
        };
        if (!tools.malioc.empty()) {
            per_module(
                "malioc", tools.mali_core,
                ojson{{"core", tools.mali_core}, {"compiler", "malioc"}, {"revision", nullptr}, {"architecture", nullptr}, {"driver", nullptr}, {"pipelines", ojson::array()}, {"malioc_version", tools.malioc_version}, {"generated", generated}},
                [&](const std::vector<unsigned int>& module) { return summarize_malioc_report(get_malioc_report(tools, module, cache_directory)); }
            );
        }
        if (!tools.powervr.empty()) {
            per_module(
                "powervr", tools.powervr_core,
                ojson{{"core", tools.powervr_core}, {"compiler", "powervr"}, {"bvnc", tools.powervr_bvnc}, {"gpu", nullptr}, {"architecture", nullptr}, {"soc", nullptr}, {"powervr_version", tools.powervr_version}, {"generated", generated}},
                [&](const std::vector<unsigned int>& module) { return summarize_powervr_report(get_powervr_report(tools, module, cache_directory)); }
            );
        }
    } catch (const json::exception& exception) { // json and ordered_json share their exception types
        log_programs->warn("Shader analytics: unexpected compiler report: {}", exception.what());
    }
    return result;
}

} // namespace editor
