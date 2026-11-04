#include "renderers/offline_compiler.hpp"

#include "editor_log.hpp"

#include "erhe_file/file.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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
}

namespace {

void analyze_program_reports(
    const Offline_compiler_tools&                 tools,
    const Offline_compiler_stage&                 fragment_stage,
    const std::filesystem::path&                  cache_directory,
    Shader_cost_metrics&                          metrics,
    std::string&                                  error
)
{
    const Offline_compiler_stage* const fragment = &fragment_stage;

    if (!tools.malioc.empty()) {
        uint64_t hash = 0xcbf29ce484222325ull;
        hash_text(hash, "malioc");
        hash_text(hash, tools.malioc_version);
        hash_text(hash, tools.mali_core);
        hash_bytes(hash, fragment->spirv.data(), fragment->spirv.size() * sizeof(unsigned int));
        const std::filesystem::path path = cache_directory / fmt::format("{:016x}.{}.malioc.json", hash, tools.mali_core);
        std::optional<json> report = read_cached_report(path);
        if (!report.has_value()) {
            report = run_malioc(tools, fragment->spirv);
            write_cached_report(path, report.value());
        }
        extract_mali_metrics(report.value(), metrics, error);
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

} // namespace editor
