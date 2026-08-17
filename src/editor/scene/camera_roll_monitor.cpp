#include "scene/camera_roll_monitor.hpp"

#include "editor_log.hpp"

#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#endif

namespace editor {

namespace {

constexpr glm::vec3 world_up{0.0f, 1.0f, 0.0f};

// Above this |sin(pitch)| the camera looks (near) straight up or down and the
// reference right vector cross(forward, world_up) degenerates, so roll is not
// measurable. This is also exactly where an unwanted roll is most likely to be
// introduced, which is why the transition in and out of this cone is logged.
constexpr float vertical_limit{0.9995f}; // ~1.8 degrees from vertical

[[nodiscard]] auto safe_normalize(const glm::vec3 v) -> glm::vec3
{
    const float length = glm::length(v);
    return (length > 1e-8f) ? (v / length) : glm::vec3{0.0f, 0.0f, 0.0f};
}

} // anonymous namespace

auto measure_camera_orientation(const glm::mat4& world_from_node) -> Roll_measurement
{
    Roll_measurement result{};

    const glm::vec3 axis_x = glm::vec3{world_from_node[0]};
    const glm::vec3 axis_y = glm::vec3{world_from_node[1]};
    const glm::vec3 axis_z = glm::vec3{world_from_node[2]};

    result.determinant = glm::determinant(glm::mat3{world_from_node});
    result.orthonormality_error = std::max(
        {
            std::abs(glm::length(axis_x) - 1.0f),
            std::abs(glm::length(axis_y) - 1.0f),
            std::abs(glm::length(axis_z) - 1.0f),
            std::abs(glm::dot(axis_x, axis_y)),
            std::abs(glm::dot(axis_y, axis_z)),
            std::abs(glm::dot(axis_z, axis_x))
        }
    );

    const glm::vec3 right   = safe_normalize(axis_x);
    const glm::vec3 up      = safe_normalize(axis_y);
    const glm::vec3 forward = -safe_normalize(axis_z); // camera looks down its own -Z

    if ((glm::length(forward) < 0.5f) || (glm::length(right) < 0.5f)) {
        // Degenerate basis - nothing meaningful to measure.
        return result;
    }

    const float sin_pitch = std::clamp(glm::dot(forward, world_up), -1.0f, 1.0f);
    result.pitch_radians   = std::asin(sin_pitch);
    result.heading_radians = std::atan2(-forward.x, -forward.z);
    result.up_dot_world_up = glm::dot(up, world_up);

    if (std::abs(sin_pitch) >= vertical_limit) {
        result.roll_valid = false;
        return result;
    }

    const glm::vec3 reference_right = safe_normalize(glm::cross(forward, world_up));
    const float     cos_roll        = glm::dot(reference_right, right);
    const float     sin_roll        = glm::dot(glm::cross(reference_right, right), forward);
    result.roll_valid   = true;
    result.roll_radians = std::atan2(sin_roll, cos_roll);
    return result;
}

auto measure_camera_orientation(const glm::quat& world_from_node) -> Roll_measurement
{
    return measure_camera_orientation(glm::toMat4(world_from_node));
}

void Camera_roll_monitor::set_frame_number(const uint64_t frame_number)
{
    m_frame_number = frame_number;
}

void Camera_roll_monitor::rebase(const Roll_measurement& current)
{
    m_current     = current;
    m_has_current = true;
}

void Camera_roll_monitor::set_next_write_hint(std::string hint)
{
    m_next_write_hint = std::move(hint);
}

void Camera_roll_monitor::clear()
{
    m_events.clear();
    m_first_reported       = false;
    m_max_abs_roll_degrees = 0.0f;
    m_suppressed_logs      = 0;
}

auto Camera_roll_monitor::get_events() const -> const std::deque<Camera_roll_event>&
{
    return m_events;
}

auto Camera_roll_monitor::get_current() const -> const Roll_measurement&
{
    return m_current;
}

auto Camera_roll_monitor::get_max_abs_roll_degrees() const -> float
{
    return m_max_abs_roll_degrees;
}

auto Camera_roll_monitor::get_suppressed_log_count() const -> std::size_t
{
    return m_suppressed_logs;
}

void Camera_roll_monitor::push(const char* source, const Roll_measurement& current)
{
    if (!enabled) {
        return;
    }
    // Anything that changed since the last thing we observed happened outside
    // every instrumented scope: report it before this scope takes over, so it
    // does not get misattributed to this scope on pop().
    sample("(uninstrumented write)", current, std::string{"detected on entry to "} + source);
    m_stack.push_back(Scope{.source = source});
}

void Camera_roll_monitor::pop(const Roll_measurement& current, const std::string& detail)
{
    if (!enabled) {
        return;
    }
    if (m_stack.empty()) {
        return;
    }
    const Scope scope = m_stack.back();
    m_stack.pop_back();
    sample(scope.source, current, detail);
}

void Camera_roll_monitor::sample(const char* source, const Roll_measurement& after, const std::string& detail)
{
    if (!enabled) {
        return;
    }
    if (!m_has_current) {
        m_current     = after;
        m_has_current = true;
        return;
    }
    const Roll_measurement before = m_current;
    m_current = after;
    report(source, detail, before, after);
}

void Camera_roll_monitor::report(
    const char*             source,
    const std::string&      detail,
    const Roll_measurement& before,
    const Roll_measurement& after
)
{
    const float roll_after_degrees  = glm::degrees(after.roll_radians);
    const float roll_before_degrees = glm::degrees(before.roll_radians);

    if (after.roll_valid) {
        m_max_abs_roll_degrees = std::max(m_max_abs_roll_degrees, std::abs(roll_after_degrees));
    }

    // Crossing into / out of the near-vertical cone is worth a note even without
    // a measurable delta: that is where roll typically gets introduced.
    const bool validity_changed = (before.roll_valid != after.roll_valid);
    const bool both_valid       = before.roll_valid && after.roll_valid;
    const float delta_degrees   = both_valid ? (roll_after_degrees - roll_before_degrees) : 0.0f;

    // Pitching past vertical flips the camera upside down, which reads as (near)
    // 180 degrees of roll once the view direction comes back off vertical. That
    // is the classic way an unwanted roll appears, and roll is not measurable at
    // the moment of the flip itself, so report the flip on its own.
    const bool flipped = (before.up_dot_world_up >= 0.0f) != (after.up_dot_world_up >= 0.0f);

    const bool delta_exceeded = both_valid && (std::abs(delta_degrees) >= delta_threshold_degrees);
    if (!delta_exceeded && !validity_changed && !flipped) {
        return;
    }

    const bool over_report_threshold = flipped || (after.roll_valid && (std::abs(roll_after_degrees) >= report_threshold_degrees));
    const bool first                 = over_report_threshold && !m_first_reported;
    if (first) {
        m_first_reported = true;
    }

    std::string full_detail = detail;
    if (!m_next_write_hint.empty()) {
        full_detail = m_next_write_hint + (full_detail.empty() ? std::string{} : (" | " + full_detail));
        m_next_write_hint.clear();
    }

    Camera_roll_event event{};
    event.frame_number         = m_frame_number;
    event.source               = (source != nullptr) ? source : "(unknown)";
    event.detail               = full_detail;
    event.roll_before_degrees  = roll_before_degrees;
    event.roll_after_degrees   = roll_after_degrees;
    event.delta_degrees        = delta_degrees;
    event.pitch_after_degrees  = glm::degrees(after.pitch_radians);
    event.orthonormality_error = after.orthonormality_error;
    event.first                = first;

    // The first crossing of the report threshold is the interesting one: capture
    // the stack there unconditionally so the writer is identified even when the
    // source tag is a shared entry point.
    if (capture_callstack && (first || over_report_threshold)) {
        event.callstack = erhe_get_callstack();
    }

    m_events.push_back(event);
    while (m_events.size() > m_max_events) {
        m_events.pop_front();
    }

    // Rate limit the log: recording every event is cheap, spamming the log for a
    // slow per-fixed-step drift is not. The first threshold crossing always logs.
    const bool log_now = first || (m_frame_number != m_last_log_frame) || over_report_threshold;
    if (!log_now) {
        ++m_suppressed_logs;
        return;
    }
    m_last_log_frame = m_frame_number;

    const char* const prefix = flipped
        ? "camera flipped upside down"
        : (first ? "FIRST unwanted camera roll" : "camera roll change");
    const std::string suppressed = (m_suppressed_logs > 0)
        ? fmt::format(" ({} similar events suppressed)", m_suppressed_logs)
        : std::string{};
    m_suppressed_logs = 0;

    log_camera_roll->warn(
        "{}: source '{}' {} | roll {:.6f} -> {:.6f} deg (delta {:.6f}), pitch {:.3f} deg, "
        "roll_valid {} -> {}, up.y {:.6f}, orthonormality error {:.3e}, det {:.6f}, frame {}{}",
        prefix,
        event.source,
        event.detail,
        roll_before_degrees,
        roll_after_degrees,
        delta_degrees,
        event.pitch_after_degrees,
        before.roll_valid,
        after.roll_valid,
        after.up_dot_world_up,
        after.orthonormality_error,
        after.determinant,
        m_frame_number,
        suppressed
    );
    if (!event.callstack.empty()) {
        log_camera_roll->warn("camera roll writer callstack:\n{}", event.callstack);
    }

    if (break_on_roll && first) {
#if defined(_WIN32)
        if (IsDebuggerPresent() != FALSE) {
            DebugBreak();
        }
#endif
    }
}

Camera_roll_scope::Camera_roll_scope(Camera_roll_monitor& monitor, const char* source, const glm::quat& watched)
    : m_monitor{monitor}
    , m_watched{watched}
{
    m_monitor.push(source, measure_camera_orientation(m_watched));
}

Camera_roll_scope::~Camera_roll_scope()
{
    m_monitor.pop(measure_camera_orientation(m_watched), m_detail);
}

void Camera_roll_scope::set_detail(std::string detail)
{
    m_detail = std::move(detail);
}

}
