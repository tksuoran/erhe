// Present calibration cache (doc/erhe/graphics.md "Present calibration
// cache"): the key, the entry file and the store Device owns.

#include "erhe_graphics/present_calibration_cache.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_hash/hash.hpp"
#include "erhe_version/version.hpp"
#include "erhe_window/window.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#else
#   include <unistd.h>
#endif

namespace erhe::graphics {

using json = nlohmann::json;

auto c_str(const Present_path value) -> const char*
{
    switch (value) {
        case Present_path::metal:                   return "metal";
        case Present_path::metal4:                  return "metal4";
        case Present_path::moltenvk_display_timing: return "moltenvk_display_timing";
        case Present_path::vulkan_display_timing:   return "vulkan_display_timing";
        default:                                    return "?";
    }
}

auto c_str(const Present_window_mode value) -> const char*
{
    switch (value) {
        case Present_window_mode::windowed:   return "windowed";
        case Present_window_mode::fullscreen: return "fullscreen";
        default:                              return "?";
    }
}

auto c_str(const Present_calibration_source value) -> const char*
{
    switch (value) {
        case Present_calibration_source::none:     return "none";
        case Present_calibration_source::measured: return "measured";
        case Present_calibration_source::cached:   return "cached";
        default:                                   return "?";
    }
}

namespace {

auto process_suffix() -> std::string
{
#if defined(_WIN32)
    return std::to_string(static_cast<unsigned long>(GetCurrentProcessId()));
#else
    return std::to_string(static_cast<long>(getpid()));
#endif
}

// Hexfloat text (AGENTS.md "Diagnostic float serialization"): bit-exact.
auto hexfloat(const double value) -> std::string
{
    return fmt::format("{:a}", value);
}

// Parses fmt's "{:a}" output ("0x1.8p+1", "-0x0p+0") locale-free.
auto parse_hexfloat(const json& value, double& out) -> bool
{
    if (!value.is_string()) {
        return false;
    }
    const std::string& text = value.get_ref<const std::string&>();
    const char*       first = text.data();
    const char* const last  = text.data() + text.size();
    const bool negative = (first != last) && (*first == '-');
    if (negative) {
        ++first;
    }
    if (((last - first) < 2) || (first[0] != '0') || ((first[1] != 'x') && (first[1] != 'X'))) {
        return false;
    }
    first += 2;
    double magnitude = 0.0;
    const std::from_chars_result result = std::from_chars(first, last, magnitude, std::chars_format::hex);
    if ((result.ec != std::errc{}) || (result.ptr != last)) {
        return false;
    }
    out = negative ? -magnitude : magnitude;
    return true;
}

auto parse_present_path(const std::string& text, Present_path& out) -> bool
{
    for (const Present_path path : {Present_path::metal, Present_path::metal4, Present_path::moltenvk_display_timing, Present_path::vulkan_display_timing}) {
        if (text == c_str(path)) {
            out = path;
            return true;
        }
    }
    return false;
}

auto parse_window_mode(const std::string& text, Present_window_mode& out) -> bool
{
    for (const Present_window_mode mode : {Present_window_mode::windowed, Present_window_mode::fullscreen}) {
        if (text == c_str(mode)) {
            out = mode;
            return true;
        }
    }
    return false;
}

auto parse_phase(const std::string& text, Present_calibration_phase& out) -> bool
{
    for (const Present_calibration_phase phase : {Present_calibration_phase::grid, Present_calibration_phase::request_window, Present_calibration_phase::deadline}) {
        if (text == c_str(phase)) {
            out = phase;
            return true;
        }
    }
    return false;
}

auto key_to_json(const Present_calibration_key& key) -> json
{
    return json{
        {"display_vendor",            key.display_vendor},
        {"display_model",             key.display_model},
        {"display_serial",            key.display_serial},
        {"mode_pixel_width",          key.mode_pixel_width},
        {"mode_pixel_height",         key.mode_pixel_height},
        {"mode_refresh_rate",         hexfloat(key.mode_refresh_rate)},
        {"mode_io_flags",             key.mode_io_flags},
        {"backing_scale_factor",      hexfloat(key.backing_scale_factor)},
        {"minimum_refresh_interval",  hexfloat(key.minimum_refresh_interval)},
        {"maximum_refresh_interval",  hexfloat(key.maximum_refresh_interval)},
        {"update_granularity",        hexfloat(key.update_granularity)},
        {"maximum_frames_per_second", key.maximum_frames_per_second},
        {"window_mode",               c_str(key.window_mode)},
        {"os_version",                key.os_version},
        {"os_build",                  key.os_build},
        {"gpu_name",                  key.gpu_name},
        {"present_path",              c_str(key.present_path)},
        {"calibration_version",       key.calibration_version}
    };
}

// The first key field that is missing or differs, empty when all match.
auto compare_key_json(const json& stored, const Present_calibration_key& key) -> std::string
{
    if (!stored.is_object()) {
        return "key";
    }
    const json expected = key_to_json(key);
    for (auto it = expected.begin(); it != expected.end(); ++it) {
        const auto stored_it = stored.find(it.key());
        if ((stored_it == stored.end()) || (*stored_it != it.value())) {
            return it.key();
        }
    }
    return {};
}

auto calibration_to_json(const Present_calibration& calibration) -> json
{
    json samples = json::array();
    for (const Present_calibration_sample& sample : calibration.samples) {
        samples.push_back(json{
            {"phase",             c_str(sample.phase)},
            {"target_time",       hexfloat(sample.target_time)},
            {"parameter",         hexfloat(sample.parameter)},
            {"completion_margin", hexfloat(sample.completion_margin)},
            {"presented_time",    hexfloat(sample.presented_time)},
            {"slot_offset",       sample.slot_offset}
        });
    }
    return json{
        {"valid",           calibration.valid},
        {"status",          calibration.status},
        {"period",          hexfloat(calibration.period)},
        {"request_offset",  hexfloat(calibration.request_offset)},
        {"request_min",     hexfloat(calibration.request_min)},
        {"request_max",     hexfloat(calibration.request_max)},
        {"deadline_offset", hexfloat(calibration.deadline_offset)},
        {"samples",         std::move(samples)}
    };
}

auto calibration_from_json(const json& value, Present_calibration& out) -> bool
{
    if (!value.is_object()) {
        return false;
    }
    const auto valid  = value.find("valid");
    const auto status = value.find("status");
    if ((valid == value.end()) || !valid->is_boolean() || (status == value.end()) || !status->is_string()) {
        return false;
    }
    out.done   = true;
    out.valid  = valid->get<bool>();
    out.status = status->get<std::string>();
    if (
        !parse_hexfloat(value.value("period",          json{}), out.period)          ||
        !parse_hexfloat(value.value("request_offset",  json{}), out.request_offset)  ||
        !parse_hexfloat(value.value("request_min",     json{}), out.request_min)     ||
        !parse_hexfloat(value.value("request_max",     json{}), out.request_max)     ||
        !parse_hexfloat(value.value("deadline_offset", json{}), out.deadline_offset)
    ) {
        return false;
    }
    out.samples.clear();
    const auto samples = value.find("samples");
    if ((samples != value.end()) && samples->is_array()) {
        for (const json& sample_json : *samples) {
            Present_calibration_sample sample{};
            if (
                !sample_json.is_object() ||
                !parse_phase(sample_json.value("phase", std::string{}), sample.phase) ||
                !parse_hexfloat(sample_json.value("target_time",       json{}), sample.target_time) ||
                !parse_hexfloat(sample_json.value("parameter",         json{}), sample.parameter) ||
                !parse_hexfloat(sample_json.value("completion_margin", json{}), sample.completion_margin) ||
                !parse_hexfloat(sample_json.value("presented_time",    json{}), sample.presented_time)
            ) {
                return false;
            }
            sample.slot_offset = sample_json.value("slot_offset", 0);
            out.samples.push_back(sample);
        }
    }
    return true;
}

auto now_unix_seconds() -> std::int64_t
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

auto format_utc(const std::int64_t unix_seconds) -> std::string
{
    const std::time_t time = static_cast<std::time_t>(unix_seconds);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    return fmt::format(
        "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z",
        utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec
    );
}

} // anonymous namespace

#if !defined(__APPLE__)
auto query_present_calibration_key(
    erhe::window::Context_window* const window,
    const std::string_view              gpu_name,
    const Present_path                  present_path
) -> std::optional<Present_calibration_key>
{
    static_cast<void>(window);
    static_cast<void>(gpu_name);
    static_cast<void>(present_path);
    return std::nullopt;
}
#endif

auto get_present_calibration_key_hash(const Present_calibration_key& key) -> std::uint64_t
{
    // nlohmann::json objects are ordered by key: the dump is canonical.
    const std::string text = key_to_json(key).dump();
    return erhe::hash::hash(text.data(), text.size());
}

auto make_present_calibration_cache_path(
    const std::filesystem::path&   directory,
    const Present_calibration_key& key
) -> std::filesystem::path
{
    return directory / fmt::format("{:016x}.json", get_present_calibration_key_hash(key));
}

auto read_present_calibration_cache_file(
    const std::filesystem::path&   path,
    const Present_calibration_key& key,
    std::string&                   out_reason
) -> std::optional<Present_calibration_cache_entry>
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        out_reason = "no entry";
        return std::nullopt;
    }
    std::ifstream file{path};
    if (!file.is_open()) {
        out_reason = "entry cannot be opened";
        return std::nullopt;
    }
    const json root = json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        out_reason = "entry is not valid JSON";
        return std::nullopt;
    }
    const std::string differing = compare_key_json(root.value("key", json{}), key);
    if (!differing.empty()) {
        out_reason = fmt::format("entry key field {} differs", differing);
        return std::nullopt;
    }
    Present_calibration_cache_entry entry{};
    entry.key = key;
    // A field of the wrong type in otherwise valid JSON (a hand edit, a
    // future format) is a malformed entry, so a miss: nlohmann's value() and
    // get() report it as json::type_error.
    try {
        if (!calibration_from_json(root.value("calibration", json{}), entry.calibration)) {
            out_reason = "entry calibration is malformed";
            return std::nullopt;
        }
        entry.display_label         = root.value("display_label", std::string{});
        entry.measured_unix_seconds = root.value("measured_unix_seconds", std::int64_t{0});
        entry.measured_utc          = root.value("measured_utc", std::string{});
        entry.build                 = root.value("build", std::string{});
    } catch (const json::type_error& error) {
        out_reason = fmt::format("entry is malformed ({})", error.what());
        return std::nullopt;
    }
    return entry;
}

auto write_present_calibration_cache_file(
    const std::filesystem::path&           path,
    const Present_calibration_cache_entry& entry
) -> bool
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        log_display_refresh->warn("present calibration cache: cannot create {}: {}", path.parent_path().string(), ec.message());
        return false;
    }
    const json root{
        {"display_label",         entry.display_label},
        {"key",                   key_to_json(entry.key)},
        {"calibration",           calibration_to_json(entry.calibration)},
        {"measured_unix_seconds", entry.measured_unix_seconds},
        {"measured_utc",          entry.measured_utc},
        {"build",                 entry.build}
    };
    const std::filesystem::path temporary_path = path.parent_path() / (path.filename().string() + ".tmp." + process_suffix());
    {
        std::ofstream file{temporary_path, std::ios::trunc};
        if (!file.is_open()) {
            log_display_refresh->warn("present calibration cache: cannot write {}", temporary_path.string());
            return false;
        }
        file << root.dump(2) << '\n';
        if (!file.good()) {
            log_display_refresh->warn("present calibration cache: cannot write {}", temporary_path.string());
            file.close();
            std::filesystem::remove(temporary_path, ec);
            return false;
        }
    }
    std::filesystem::rename(temporary_path, path, ec);
    if (ec) {
        log_display_refresh->warn("present calibration cache: cannot store {}: {}", path.string(), ec.message());
        std::error_code remove_ec;
        std::filesystem::remove(temporary_path, remove_ec);
        return false;
    }
    return true;
}

// Present_calibration_store

Present_calibration_store::Present_calibration_store(const Present_calibration_cache_reads cache_reads, std::filesystem::path directory)
    : m_cache_reads{cache_reads}
    , m_directory  {std::move(directory)}
{
}

void Present_calibration_store::set_key_source(
    erhe::window::Context_window* const window,
    const std::string_view              gpu_name,
    const Present_path                  present_path
)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_window       = window;
    m_gpu_name     = std::string{gpu_name};
    m_present_path = present_path;
    m_keyed        = true;
}

auto Present_calibration_store::is_keyed() const -> bool
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_keyed;
}

auto Present_calibration_store::query_key() const -> Present_calibration_key_query
{
    erhe::window::Context_window* window       = nullptr;
    std::string                   gpu_name;
    Present_path                  present_path = Present_path::metal;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        window       = m_window;
        gpu_name     = m_gpu_name;
        present_path = m_present_path;
    }
    Present_calibration_key_query query{};
    query.key           = query_present_calibration_key(window, gpu_name, present_path);
    query.display_label = (window != nullptr) ? window->get_display_name() : std::string{};
    return query;
}

void Present_calibration_store::set_key_locked(const Present_calibration_key_query& query)
{
    m_key           = query.key;
    m_display_label = query.display_label;
    m_cache_file    = query.key.has_value() ? make_present_calibration_cache_path(m_directory, query.key.value()).filename().string() : std::string{};
}

auto Present_calibration_store::find_session_entry_locked(const Present_calibration_key& key) const -> const Session_entry*
{
    for (const Session_entry& entry : m_session_entries) {
        if (entry.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

void Present_calibration_store::remember_session_entry_locked()
{
    if (!m_key.has_value() || !m_calibration.valid) {
        return;
    }
    for (Session_entry& entry : m_session_entries) {
        if (entry.key == m_key.value()) {
            entry.calibration           = m_calibration;
            entry.source                = m_source;
            entry.measured_unix_seconds = m_measured_unix_seconds;
            entry.measured_utc          = m_measured_utc;
            return;
        }
    }
    m_session_entries.push_back(Session_entry{m_key.value(), m_calibration, m_source, m_measured_unix_seconds, m_measured_utc});
}

auto Present_calibration_store::read_file_entry(
    const Present_calibration_key& key,
    const Present_calibration&     live,
    Present_calibration&           out
) const -> std::optional<Present_calibration_cache_entry>
{
    const std::filesystem::path path = make_present_calibration_cache_path(m_directory, key);
    if (m_cache_reads == Present_calibration_cache_reads::disabled) {
        log_display_refresh->info("present calibration: cache reads disabled, not reading {}", path.filename().string());
        return std::nullopt;
    }
    std::string reason;
    std::optional<Present_calibration_cache_entry> entry = read_present_calibration_cache_file(path, key, reason);
    if (!entry.has_value()) {
        log_display_refresh->info("present calibration: cache miss {} ({})", path.filename().string(), reason);
        return std::nullopt;
    }
    if (!entry->calibration.valid) {
        log_display_refresh->info("present calibration: cache miss {} (entry not valid)", path.filename().string());
        return std::nullopt;
    }
    out = entry->calibration;
    out.prewarm_seconds_per_iteration = live.prewarm_seconds_per_iteration;
    const std::int64_t age_seconds = std::max<std::int64_t>(0, now_unix_seconds() - entry->measured_unix_seconds);
    log_display_refresh->info(
        "present calibration: cached {}, measured {}, {} days old (period {:.3f} ms, request offset {:.3f} ms, deadline offset {:.3f} ms)",
        path.filename().string(),
        entry->measured_utc,
        age_seconds / 86400,
        out.period * 1000.0,
        out.request_offset * 1000.0,
        out.deadline_offset * 1000.0
    );
    return entry;
}

auto Present_calibration_store::begin(
    const Present_calibration_key_query& query,
    Present_calibration&                 in_out
) -> bool
{
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        set_key_locked(query);
    }
    if (!query.key.has_value()) {
        log_display_refresh->info("present calibration: no cache key for this window (no screen, display mode or display identity), measuring");
        return false;
    }
    Present_calibration adopted{};
    const std::optional<Present_calibration_cache_entry> entry = read_file_entry(query.key.value(), in_out, adopted);
    if (!entry.has_value()) {
        return false;
    }
    in_out = adopted;
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_calibration           = adopted;
    m_source                = Present_calibration_source::cached;
    m_measured_unix_seconds = entry->measured_unix_seconds;
    m_measured_utc          = entry->measured_utc;
    remember_session_entry_locked();
    return true;
}

void Present_calibration_store::finish_measured(const Present_calibration& result)
{
    Present_calibration_cache_entry entry{};
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_calibration           = result;
        m_source                = Present_calibration_source::measured;
        m_measured_unix_seconds = now_unix_seconds();
        m_measured_utc          = format_utc(m_measured_unix_seconds);
        remember_session_entry_locked();
        if (!m_key.has_value() || !m_calibration.valid) {
            return;
        }
        entry.key                   = m_key.value();
        entry.display_label         = m_display_label;
        entry.calibration           = m_calibration;
        entry.measured_unix_seconds = m_measured_unix_seconds;
        entry.measured_utc          = m_measured_utc;
    }
    entry.calibration.prewarm_seconds_per_iteration = 0.0;
    entry.build = erhe::version::get_description();
    const std::filesystem::path path = make_present_calibration_cache_path(m_directory, entry.key);
    if (write_present_calibration_cache_file(path, entry)) {
        log_display_refresh->info("present calibration: stored {}", path.string());
    }
}

auto Present_calibration_store::refresh_for_display(const Present_calibration_key_query& query) -> bool
{
    Present_calibration live{};
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (!m_keyed || (query.key == m_key)) {
            return false;
        }
        set_key_locked(query);
        live = m_calibration;
        // This session's own result for the key first, whatever the read
        // setting: a display round trip restores what was measured here.
        if (query.key.has_value()) {
            const Session_entry* const session_entry = find_session_entry_locked(query.key.value());
            if (session_entry != nullptr) {
                m_calibration                               = session_entry->calibration;
                m_calibration.prewarm_seconds_per_iteration = live.prewarm_seconds_per_iteration;
                m_source                                    = session_entry->source;
                m_measured_unix_seconds                     = session_entry->measured_unix_seconds;
                m_measured_utc                              = session_entry->measured_utc;
                log_display_refresh->info("present calibration: display changed to '{}'; re-seeded from this session's calibration of it", query.display_label);
                return true;
            }
        }
    }
    // The file read runs outside the mutex (the presenting thread reads the
    // request offset under it); display changes arrive on the main thread
    // only, so nothing else changes the key meanwhile.
    Present_calibration adopted{};
    std::optional<Present_calibration_cache_entry> entry{};
    if (query.key.has_value()) {
        entry = read_file_entry(query.key.value(), live, adopted);
    }
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (entry.has_value()) {
        m_calibration           = adopted;
        m_source                = Present_calibration_source::cached;
        m_measured_unix_seconds = entry->measured_unix_seconds;
        m_measured_utc          = entry->measured_utc;
        remember_session_entry_locked();
        log_display_refresh->info("present calibration: display changed to '{}'; re-seeded from the cache", query.display_label);
        return true;
    }
    Present_calibration cleared{};
    cleared.done                          = true;
    cleared.valid                         = false;
    cleared.status                        = "display changed; no calibration for it";
    cleared.prewarm_seconds_per_iteration = live.prewarm_seconds_per_iteration;
    m_calibration           = cleared;
    m_source                = Present_calibration_source::none;
    m_measured_unix_seconds = 0;
    m_measured_utc.clear();
    log_display_refresh->info(
        "present calibration: display changed to '{}'; no calibration for it, cleared (pacer defaults); the next start calibrates for this display",
        query.display_label
    );
    return true;
}

auto Present_calibration_store::remove_entry(const std::string_view reason) -> std::string
{
    std::optional<Present_calibration_key> key{};
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        key = m_key;
    }
    if (!key.has_value()) {
        log_display_refresh->info("present calibration: {}: no cache key, nothing to delete", reason);
        return {};
    }
    const std::filesystem::path path = make_present_calibration_cache_path(m_directory, key.value());
    std::error_code ec;
    const bool removed = std::filesystem::remove(path, ec);
    if (!removed) {
        log_display_refresh->info("present calibration: {}: no entry {} to delete", reason, path.filename().string());
        return {};
    }
    log_display_refresh->info("present calibration: {}: deleted {}; the next start measures", reason, path.string());
    return path.filename().string();
}

auto Present_calibration_store::get_calibration() const -> Present_calibration
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_calibration;
}

auto Present_calibration_store::get_state() const -> Present_calibration_state
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    Present_calibration_state state{};
    state.calibration           = m_calibration;
    state.source                = m_source;
    state.key                   = m_key;
    state.display_label         = m_display_label;
    state.cache_file            = m_cache_file;
    state.measured_unix_seconds = m_measured_unix_seconds;
    state.measured_utc          = m_measured_utc;
    state.cache_reads_enabled   = (m_cache_reads == Present_calibration_cache_reads::enabled);
    return state;
}

void Present_calibration_store::get_summary(Present_calibration_summary& out) const
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    out.done   = m_calibration.done;
    out.valid  = m_calibration.valid;
    out.source = m_source;
    const auto copy_text = [](const std::string& text, auto& buffer) {
        const std::size_t length = std::min(text.size(), buffer.size() - 1);
        std::copy_n(text.data(), length, buffer.data());
        buffer[length] = '\0';
    };
    copy_text(m_measured_utc, out.measured_utc);
    copy_text(m_cache_file,   out.cache_file);
}

auto Present_calibration_store::get_request_offset() const -> std::optional<double>
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_calibration.valid ? std::optional<double>{m_calibration.request_offset} : std::nullopt;
}

} // namespace erhe::graphics
