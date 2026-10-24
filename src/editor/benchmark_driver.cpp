#include "benchmark_driver.hpp"
#include "editor_log.hpp"

#include "erhe_file/file.hpp"
#include "erhe_version/version.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

#if defined(ERHE_OS_ANDROID)
#   include <SDL3/SDL_system.h>
#   include <android/api-level.h>
#   include <android/thermal.h>
#   include <sys/system_properties.h>
#endif

namespace editor {

namespace {

constexpr uint64_t c_fnv_offset_basis = 0xcbf29ce484222325ull;
constexpr uint64_t c_fnv_prime        = 0x00000100000001b3ull;

auto fnv1a(const uint64_t hash_in, const void* data, const std::size_t size) -> uint64_t
{
    uint64_t       hash  = hash_in;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ static_cast<uint64_t>(bytes[i])) * c_fnv_prime;
    }
    return hash;
}

class Term_stats
{
public:
    double mean  {0.0};
    double median{0.0};
    double p99   {0.0};
    double min   {0.0};
    double max   {0.0};
};

// Cold path (after the measured window): sorts a copy.
auto compute_stats(const std::vector<double>& values) -> Term_stats
{
    Term_stats stats{};
    if (values.empty()) {
        return stats;
    }
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0.0;
    for (const double value : sorted) {
        sum += value;
    }
    const std::size_t count = sorted.size();
    stats.mean   = sum / static_cast<double>(count);
    stats.median = ((count % 2) == 1)
        ? sorted[count / 2]
        : (0.5 * (sorted[(count / 2) - 1] + sorted[count / 2]));
    // Nearest-rank 99th percentile.
    const std::size_t p99_rank = static_cast<std::size_t>(std::ceil(0.99 * static_cast<double>(count)));
    stats.p99 = sorted[std::clamp<std::size_t>(p99_rank, 1, count) - 1];
    stats.min = sorted.front();
    stats.max = sorted.back();
    return stats;
}

auto stats_to_json(const Term_stats& stats) -> nlohmann::ordered_json
{
    return nlohmann::ordered_json{
        {"mean",   stats.mean},
        {"median", stats.median},
        {"p99",    stats.p99},
        {"min",    stats.min},
        {"max",    stats.max}
    };
}

auto read_vec3(const nlohmann::json& object, const char* key, glm::vec3& out) -> bool
{
    if (!object.contains(key)) {
        return true; // optional, keeps the default
    }
    const nlohmann::json& value = object[key];
    if (!value.is_array() || (value.size() != 3) || !value[0].is_number() || !value[1].is_number() || !value[2].is_number()) {
        log_startup->error("benchmark file: '{}' must be an array of 3 numbers", key);
        return false;
    }
    out = glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
    return true;
}

// Thermal status (Android AThermal: 0 none .. 6 shutdown), -1 when unavailable.
auto query_thermal_status() -> int
{
#if defined(ERHE_OS_ANDROID)
    if (__builtin_available(android 30, *)) {
        AThermalManager* manager = AThermal_acquireManager();
        if (manager == nullptr) {
            return -1;
        }
        const AThermalStatus status = AThermal_getCurrentThermalStatus(manager);
        AThermal_releaseManager(manager);
        return static_cast<int>(status);
    }
#endif
    return -1;
}

auto query_system_property(const char* name) -> std::string
{
#if defined(ERHE_OS_ANDROID)
    char value[PROP_VALUE_MAX] = {};
    const int length = __system_property_get(name, value);
    if (length > 0) {
        return std::string{value, static_cast<std::size_t>(length)};
    }
#else
    static_cast<void>(name);
#endif
    return {};
}

} // anonymous namespace

auto get_benchmark_output_directory() -> std::string
{
#if defined(ERHE_OS_ANDROID)
    const char* external = SDL_GetAndroidExternalStoragePath();
    if ((external != nullptr) && (external[0] != '\0')) {
        return fmt::format("{}/logs", external);
    }
#endif
    return "logs";
}

auto Benchmark_camera_path::evaluate(const int64_t frame) const -> Benchmark_pose
{
    Benchmark_pose pose{};
    if (type == Benchmark_camera_path_type::orbit) {
        const int64_t period  = std::max<int64_t>(period_frames, 1);
        const int64_t f       = ((frame % period) + period) % period;
        const double  theta   = 2.0 * std::numbers::pi * static_cast<double>(f) / static_cast<double>(period);
        pose.position = center + glm::vec3{
            radius * static_cast<float>(std::sin(theta)),
            height,
            radius * static_cast<float>(std::cos(theta))
        };
        const glm::vec3 direction = look_at - pose.position;
        if (glm::dot(direction, direction) > 0.0f) {
            // quatLookAt is undefined (NaN) for a direction parallel to the
            // up vector: looking straight up or down uses +Z as up.
            const glm::vec3 unit_direction = glm::normalize(direction);
            const glm::vec3 up = (std::abs(unit_direction.y) > 0.9999f)
                ? glm::vec3{0.0f, 0.0f, 1.0f}
                : glm::vec3{0.0f, 1.0f, 0.0f};
            pose.orientation = glm::quatLookAt(unit_direction, up);
        }
        return pose;
    }

    if (keyframes.empty()) {
        return pose;
    }
    const auto to_orientation = [](const glm::vec3& yaw_pitch_roll_deg) -> glm::quat {
        const glm::vec3 r = glm::radians(yaw_pitch_roll_deg);
        return
            glm::angleAxis(r.x, glm::vec3{0.0f, 1.0f, 0.0f}) *
            glm::angleAxis(r.y, glm::vec3{1.0f, 0.0f, 0.0f}) *
            glm::angleAxis(r.z, glm::vec3{0.0f, 0.0f, 1.0f});
    };
    const int64_t loop_length = keyframes.back().frame;
    if ((keyframes.size() == 1) || (loop_length <= 0)) {
        pose.position    = keyframes.front().position;
        pose.orientation = to_orientation(keyframes.front().yaw_pitch_roll_deg);
        return pose;
    }
    const int64_t f = ((frame % loop_length) + loop_length) % loop_length;
    std::size_t segment = 0;
    while (((segment + 2) < keyframes.size()) && (keyframes[segment + 1].frame <= f)) {
        ++segment;
    }
    const Benchmark_keyframe& a = keyframes[segment];
    const Benchmark_keyframe& b = keyframes[segment + 1];
    const float t = static_cast<float>(f - a.frame) / static_cast<float>(b.frame - a.frame);
    pose.position    = glm::mix(a.position, b.position, t);
    pose.orientation = to_orientation(glm::mix(a.yaw_pitch_roll_deg, b.yaw_pitch_roll_deg, t));
    return pose;
}

auto Benchmark_driver::load(const std::string& path) -> std::unique_ptr<Benchmark_driver>
{
    const std::optional<std::string> content = erhe::file::read("benchmark file", path);
    if (!content.has_value()) {
        log_startup->error("Benchmark failed {}: file not readable", path);
        return {};
    }
    const nlohmann::json root = nlohmann::json::parse(content.value(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        log_startup->error("Benchmark failed {}: not a JSON object", path);
        return {};
    }

    std::unique_ptr<Benchmark_driver> driver = std::make_unique<Benchmark_driver>();
    driver->m_path      = path;
    driver->m_file_hash = fnv1a(c_fnv_offset_basis, content.value().data(), content.value().size());

    if (!root.contains("name") || !root["name"].is_string()) {
        log_startup->error("Benchmark failed {}: missing string 'name'", path);
        return {};
    }
    driver->m_name = root["name"].get<std::string>();
    const bool name_ok = !driver->m_name.empty() && std::all_of(
        driver->m_name.begin(), driver->m_name.end(),
        [](const char c) { return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '_') || (c == '-'); }
    );
    if (!name_ok) {
        log_startup->error("Benchmark failed {}: 'name' must be [A-Za-z0-9_-]+ (it names logs/benchmark_<name>.json)", path);
        return {};
    }

    const auto read_count = [&](const char* key, int64_t& out, const int64_t minimum) -> bool {
        if (!root.contains(key)) {
            return true;
        }
        if (!root[key].is_number_integer() || (root[key].get<int64_t>() < minimum)) {
            log_startup->error("Benchmark failed {}: '{}' must be an integer >= {}", path, key, minimum);
            return false;
        }
        out = root[key].get<int64_t>();
        return true;
    };
    if (!read_count("warmup_frames",         driver->m_warmup_frames,         0) ||
        !read_count("measure_frames",        driver->m_measure_frames,        1) ||
        !read_count("settle_timeout_frames", driver->m_settle_timeout_frames, 1)) {
        return {};
    }
    if (root.contains("frame_dt")) {
        // Time::prepare_update() caps a frame's simulation advance at 25 ms,
        // so a larger frame_dt would be hashed but not simulated.
        if (!root["frame_dt"].is_number() || !(root["frame_dt"].get<double>() > 0.0) || (root["frame_dt"].get<double>() > 0.025)) {
            log_startup->error("Benchmark failed {}: 'frame_dt' must be a number in (0, 0.025] seconds (the simulation caps a frame's advance at 25 ms)", path);
            return {};
        }
        driver->m_frame_dt_ns = static_cast<int64_t>(std::llround(root["frame_dt"].get<double>() * 1e9));
    }
    if (root.contains("display_refresh_rate_hz")) {
        if (!root["display_refresh_rate_hz"].is_number() || !(root["display_refresh_rate_hz"].get<double>() > 0.0)) {
            log_startup->error("Benchmark failed {}: 'display_refresh_rate_hz' must be a number > 0", path);
            return {};
        }
        driver->m_display_refresh_rate_hz = root["display_refresh_rate_hz"].get<float>();
    }

    if (!root.contains("camera_path") || !root["camera_path"].is_object()) {
        log_startup->error("Benchmark failed {}: missing object 'camera_path'", path);
        return {};
    }
    const nlohmann::json& camera_path = root["camera_path"];
    const std::string     path_type   = camera_path.value("type", std::string{});
    Benchmark_camera_path& out_path   = driver->m_camera_path;
    if (path_type == "orbit") {
        out_path.type = Benchmark_camera_path_type::orbit;
        if (!read_vec3(camera_path, "center", out_path.center) || !read_vec3(camera_path, "look_at", out_path.look_at)) {
            return {};
        }
        out_path.radius        = camera_path.value("radius",        out_path.radius);
        out_path.height        = camera_path.value("height",        out_path.height);
        out_path.period_frames = camera_path.value("period_frames", out_path.period_frames);
        if (out_path.period_frames < 1) {
            log_startup->error("Benchmark failed {}: 'period_frames' must be >= 1", path);
            return {};
        }
    } else if (path_type == "keyframes") {
        out_path.type = Benchmark_camera_path_type::keyframes;
        if (!camera_path.contains("frames") || !camera_path["frames"].is_array() || camera_path["frames"].empty()) {
            log_startup->error("Benchmark failed {}: keyframes path needs a non-empty 'frames' array", path);
            return {};
        }
        for (const nlohmann::json& keyframe_json : camera_path["frames"]) {
            Benchmark_keyframe keyframe{};
            if (!keyframe_json.is_object() || !keyframe_json.contains("frame") || !keyframe_json["frame"].is_number_integer()) {
                log_startup->error("Benchmark failed {}: every keyframe needs an integer 'frame'", path);
                return {};
            }
            keyframe.frame = keyframe_json["frame"].get<int64_t>();
            if (!read_vec3(keyframe_json, "position", keyframe.position) ||
                !read_vec3(keyframe_json, "yaw_pitch_roll_deg", keyframe.yaw_pitch_roll_deg)) {
                return {};
            }
            const bool ordered = out_path.keyframes.empty()
                ? (keyframe.frame == 0)
                : (keyframe.frame > out_path.keyframes.back().frame);
            if (!ordered) {
                log_startup->error("Benchmark failed {}: keyframe frames must start at 0 and increase", path);
                return {};
            }
            out_path.keyframes.push_back(keyframe);
        }
    } else {
        log_startup->error("Benchmark failed {}: camera_path 'type' must be 'orbit' or 'keyframes'", path);
        return {};
    }

    const std::size_t measure_frames = static_cast<std::size_t>(driver->m_measure_frames);
    for (std::vector<double>* series : {&driver->m_wall_us, &driver->m_full_tick_us, &driver->m_m1_us, &driver->m_m2_us, &driver->m_m3_us, &driver->m_r_us}) {
        series->reserve(measure_frames);
    }

    log_startup->info(
        "Benchmark '{}' from {}: warmup {} frames, measure {} frames, frame_dt {} ns, settle timeout {} frames, {} camera path",
        driver->m_name, path, driver->m_warmup_frames, driver->m_measure_frames, driver->m_frame_dt_ns,
        driver->m_settle_timeout_frames, path_type
    );
    return driver;
}

auto Benchmark_driver::get_name() const -> const std::string&
{
    return m_name;
}

auto Benchmark_driver::get_display_refresh_rate_hz() const -> std::optional<float>
{
    return m_display_refresh_rate_hz;
}

auto Benchmark_driver::get_frame_advance_ns() const -> int64_t
{
    return (m_state == State::measure) ? m_frame_dt_ns : 0;
}

auto Benchmark_driver::get_head_pose() const -> Benchmark_pose
{
    return m_camera_path.evaluate(m_path_frame);
}

auto Benchmark_driver::is_waiting_for_settle() const -> bool
{
    return m_state == State::settle;
}

auto Benchmark_driver::needs_settle_inputs() const -> bool
{
    return
        (m_state == State::settle) ||
        ((m_state == State::measure) && ((m_state_frame + 1) >= m_measure_frames));
}

auto Benchmark_driver::is_measuring() const -> bool
{
    return m_state == State::measure;
}

auto Benchmark_driver::is_done() const -> bool
{
    return m_state == State::done;
}

void Benchmark_driver::set_run_identity(const Benchmark_run_identity& identity)
{
    m_identity = identity;
    // Cold (before the frame loop): the scene setup the run started from
    // seeds input_hash, so results of different scenes cannot match.
    m_commands_file_hash = 0;
    if (!m_identity.commands_path.empty()) {
        const std::optional<std::string> content = erhe::file::read("benchmark commands file", m_identity.commands_path);
        if (content.has_value()) {
            m_commands_file_hash = fnv1a(c_fnv_offset_basis, content.value().data(), content.value().size());
        }
    }
}

void Benchmark_driver::set_xr_display_period(const uint64_t predicted_display_period_ns)
{
    m_xr_display_period_ns = predicted_display_period_ns;
}

void Benchmark_driver::set_counter(const std::size_t index, const std::string_view name, const double value)
{
    if (index >= m_counters.size()) {
        if ((m_state == State::measure) || (m_state == State::done) || (index != m_counters.size())) {
            return; // the counter list is fixed once the measured window starts
        }
        std::string series_name{name};
        for (const Counter_series& series : m_counters) {
            if (series.name == series_name) {
                series_name = fmt::format("{}#{}", name, index); // display names can repeat
                break;
            }
        }
        m_counters.push_back(Counter_series{.name = std::move(series_name), .values = {}, .pending = 0.0});
    }
    m_counters[index].pending = value;
}

void Benchmark_driver::hash_bytes(const void* data, const std::size_t size)
{
    m_input_hash = fnv1a(m_input_hash, data, size);
}

void Benchmark_driver::begin_measure(const Benchmark_frame_inputs& inputs)
{
    // Cold transition, before the first measured frame starts: reserve the
    // counter series, latch the refresh rate and the thermal status.
    for (Counter_series& series : m_counters) {
        series.values.reserve(static_cast<std::size_t>(m_measure_frames));
    }
    m_display_refresh_hz         = inputs.display_refresh_hz;
    m_thermal_status_begin       = query_thermal_status();
    m_applied_cpu_level_begin    = inputs.applied_cpu_performance_level;
    m_applied_gpu_level_begin    = inputs.applied_gpu_performance_level;
    m_thermal_step_downs_begin   = inputs.thermal_step_down_count;
    m_measure_begin_wall_clock_s = inputs.wall_clock_s;
    m_settle_frames              = m_state_frame;
    m_input_hash                 = fnv1a(c_fnv_offset_basis, &m_commands_file_hash, sizeof(m_commands_file_hash));
    m_path_frame                 = 0;
    m_state                      = State::measure;
    m_state_frame                = 0;
    log_frame->info(
        "Benchmark {}: scene settled after {} frames ({:.2f} s), measuring {} frames at {:.2f} Hz",
        m_name, m_settle_frames, inputs.wall_clock_s - m_settle_begin_wall_clock_s, m_measure_frames, m_display_refresh_hz
    );
}

void Benchmark_driver::fail(const std::string& reason)
{
    log_frame->error("Benchmark failed {}: {}", m_name, reason);
    m_state = State::done;
}

void Benchmark_driver::on_frame_end(const Benchmark_frame_inputs& inputs)
{
    if ((m_state != State::done) && (inputs.refresh_rate_failure != nullptr) && !inputs.refresh_rate_failure->empty()) {
        fail(*inputs.refresh_rate_failure);
        m_previous_wall_clock_s = inputs.wall_clock_s;
        return;
    }
    switch (m_state) {
        case State::warmup: {
            ++m_state_frame;
            ++m_path_frame;
            if (m_state_frame >= m_warmup_frames) {
                m_state                     = State::settle;
                m_state_frame               = 0;
                m_settle_begin_wall_clock_s = inputs.wall_clock_s;
            }
            break;
        }
        case State::settle: {
            if (inputs.frame_rendered && inputs.scene_settled && inputs.refresh_rate_settled) {
                begin_measure(inputs);
                break;
            }
            ++m_state_frame;
            ++m_path_frame;
            if (m_state_frame >= m_settle_timeout_frames) {
                if (!inputs.frame_rendered) {
                    fail("frames do not render (XR session not running or not visible)");
                } else if (!inputs.scene_settled) {
                    fail("scene did not settle");
                } else {
                    fail(fmt::format(
                        "display refresh rate did not settle (requested {:.2f} Hz, achieved {:.2f} Hz)",
                        m_display_refresh_rate_hz.value_or(0.0f), inputs.display_refresh_hz
                    ));
                }
            }
            break;
        }
        case State::measure: {
            if (!inputs.frame_rendered) {
                fail(fmt::format("frame {} not rendered", m_state_frame));
                break;
            }
            // The inputs this frame rendered from, as their sources rather
            // than the float pose (so builds with a different libm or code
            // generation still prove identical inputs): the benchmark file
            // (it defines the camera path), frame_dt and the measured frame
            // index the path is evaluated at.
            hash_bytes(&m_file_hash,   sizeof(m_file_hash));
            hash_bytes(&m_frame_dt_ns, sizeof(m_frame_dt_ns));
            hash_bytes(&m_path_frame,  sizeof(m_path_frame));

            m_wall_us     .push_back((inputs.wall_clock_s - m_previous_wall_clock_s) * 1.0e6);
            m_full_tick_us.push_back(inputs.full_tick_us);
            m_m1_us       .push_back(inputs.m1_us);
            m_m2_us       .push_back(inputs.m2_us);
            m_m3_us       .push_back(inputs.m3_us);
            m_r_us        .push_back(inputs.r_us);
            for (Counter_series& series : m_counters) {
                series.values.push_back(series.pending);
            }
            ++m_path_frame;
            ++m_state_frame;
            if (m_state_frame >= m_measure_frames) {
                finish(inputs);
            }
            break;
        }
        case State::done: {
            break;
        }
    }
    m_previous_wall_clock_s = inputs.wall_clock_s;
}

void Benchmark_driver::finish(const Benchmark_frame_inputs& inputs)
{
    m_state                  = State::done;
    m_display_refresh_hz_end = inputs.display_refresh_hz;

    const double wall_time_s       = inputs.wall_clock_s - m_measure_begin_wall_clock_s;
    const double frames_per_second = (wall_time_s > 0.0) ? (static_cast<double>(m_measure_frames) / wall_time_s) : 0.0;
    const double refresh_period_us = (m_display_refresh_hz > 0.0) ? (1.0e6 / m_display_refresh_hz) : 0.0;
    int64_t frames_at_refresh_ceiling = 0;
    if (refresh_period_us > 0.0) {
        for (const double wall_us : m_wall_us) {
            if (std::abs(wall_us - refresh_period_us) <= (0.05 * refresh_period_us)) {
                ++frames_at_refresh_ceiling;
            }
        }
    }
    const int thermal_status_end = query_thermal_status();

    const Term_stats wall      = compute_stats(m_wall_us);
    const Term_stats full_tick = compute_stats(m_full_tick_us);
    const Term_stats m1        = compute_stats(m_m1_us);
    const Term_stats m2        = compute_stats(m_m2_us);
    const Term_stats m3        = compute_stats(m_m3_us);
    const Term_stats r         = compute_stats(m_r_us);

    const auto format_term = [](const char* label, const Term_stats& stats) -> std::string {
        return fmt::format("{} {:.1f}/{:.1f}/{:.1f}/{:.1f}", label, stats.mean, stats.median, stats.p99, stats.max);
    };
    std::string counters_text;
    for (const Counter_series& series : m_counters) {
        const Term_stats stats = compute_stats(series.values);
        counters_text += fmt::format(" {} {:.3f}/{:.3f}/{:.3f}", series.name, stats.mean, stats.median, stats.p99);
    }
    log_frame->info(
        "Benchmark result {}: wall_time_s {:.4f} frames_per_second {:.2f} frames {} refresh_hz {:.2f} frames_at_refresh_ceiling {} "
        "(us mean/median/p99/max) {} {} {} {} {} {}; xr counters (mean/median/p99){}; input_hash {:016x}",
        m_name, wall_time_s, frames_per_second, m_measure_frames, m_display_refresh_hz, frames_at_refresh_ceiling,
        format_term("wall", wall), format_term("full_tick", full_tick), format_term("M1", m1),
        format_term("M2", m2), format_term("M3", m3), format_term("R", r),
        counters_text.empty() ? std::string{" none"} : counters_text,
        m_input_hash
    );

    nlohmann::ordered_json counters_summary = nlohmann::ordered_json::object();
    nlohmann::ordered_json counters_frames  = nlohmann::ordered_json::object();
    for (const Counter_series& series : m_counters) {
        counters_summary[series.name] = stats_to_json(compute_stats(series.values));
        counters_frames [series.name] = series.values;
    }
    const nlohmann::ordered_json result = {
        {"wall_time_s",       wall_time_s},
        {"frames_per_second", frames_per_second},
        {"name",              m_name},
        {"measure_frames",    m_measure_frames},
        {"warmup_frames",     m_warmup_frames},
        {"settle_frames",     m_settle_frames},
        {"frame_dt_ns",       m_frame_dt_ns},
        {"input_hash",        fmt::format("{:016x}", m_input_hash)},
        {"refresh", {
            {"display_refresh_hz",             m_display_refresh_hz},
            {"display_refresh_hz_end",         m_display_refresh_hz_end},
            {"requested_display_refresh_hz",   m_display_refresh_rate_hz.has_value() ? nlohmann::ordered_json(m_display_refresh_rate_hz.value()) : nlohmann::ordered_json(nullptr)},
            {"refresh_period_us",              refresh_period_us},
            {"xr_predicted_display_period_ns", m_xr_display_period_ns},
            {"frames_at_refresh_ceiling",      frames_at_refresh_ceiling}
        }},
        {"identity", {
            {"version",               erhe::version::get_description()},
            {"benchmark_file",        m_path},
            {"benchmark_file_hash",   fmt::format("{:016x}", m_file_hash)},
            {"commands_path",         m_identity.commands_path},
            {"commands_file_hash",    fmt::format("{:016x}", m_commands_file_hash)},
            {"device_model",          query_system_property("ro.product.model")},
            {"os_build",              query_system_property("ro.build.display.id")},
            {"cpu_performance_level", m_identity.cpu_performance_level},
            {"gpu_performance_level", m_identity.gpu_performance_level},
            {"thermal_status_begin",  m_thermal_status_begin},
            {"thermal_status_end",    thermal_status_end},
            {"applied_cpu_performance_level_begin", m_applied_cpu_level_begin},
            {"applied_cpu_performance_level_end",   inputs.applied_cpu_performance_level},
            {"applied_gpu_performance_level_begin", m_applied_gpu_level_begin},
            {"applied_gpu_performance_level_end",   inputs.applied_gpu_performance_level},
            {"thermal_step_downs_in_window",        inputs.thermal_step_down_count - m_thermal_step_downs_begin},
            {"thermal_step_downs_total",            inputs.thermal_step_down_count}
        }},
        {"summary", {
            {"wall_us",      stats_to_json(wall)},
            {"full_tick_us", stats_to_json(full_tick)},
            {"m1_us",        stats_to_json(m1)},
            {"m2_us",        stats_to_json(m2)},
            {"m3_us",        stats_to_json(m3)},
            {"r_us",         stats_to_json(r)},
            {"xr_counters",  counters_summary}
        }},
        {"frames", {
            {"wall_us",      m_wall_us},
            {"full_tick_us", m_full_tick_us},
            {"m1_us",        m_m1_us},
            {"m2_us",        m_m2_us},
            {"m3_us",        m_m3_us},
            {"r_us",         m_r_us},
            {"xr_counters",  counters_frames}
        }}
    };

    const std::string output_directory = get_benchmark_output_directory();
    const std::string output_path      = fmt::format("{}/benchmark_{}.json", output_directory, m_name);
    static_cast<void>(erhe::file::ensure_directory_exists(output_directory));
    if (!erhe::file::write_file(output_path, result.dump(2))) {
        log_frame->error("Benchmark {}: writing {} failed", m_name, output_path);
    } else {
        log_frame->info("Benchmark {}: wrote {}", m_name, output_path);
    }
}

} // namespace editor
