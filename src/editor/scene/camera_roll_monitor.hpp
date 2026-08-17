#pragma once

// Unwanted-camera-roll diagnostics.
//
// Off by default. The instrumentation sits in the fly camera's per-fixed-step
// path and formats a detail string for every orientation write, so it must cost
// nothing while it is not being used. With this at 0 every hook below compiles
// away completely: no measurement, no string formatting, no per-controller
// event storage, and no diagnostics UI.
//
// To re-enable, set this to 1 (or define it as a compile definition) and
// rebuild. See the "Camera Roll Diagnostics" section of the Fly Camera window
// and the editor.camera_roll log channel.
//
// Background: the fly camera picked up unwanted roll because Frame_controller
// held its orientation in a matrix that nothing kept orthonormal. The
// orientation is a normalized quaternion now, which makes that state
// unexpressible, and the issue has not recurred - but the exact term that
// amplified the error was never identified, so the instrumentation is kept.
#if !defined(ERHE_CAMERA_ROLL_DIAGNOSTICS)
#   define ERHE_CAMERA_ROLL_DIAGNOSTICS 0
#endif

#if ERHE_CAMERA_ROLL_DIAGNOSTICS

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace editor {

// Orientation health of a camera basis, measured against world up (0, 1, 0).
//
// The editor camera is expected to stay roll free: the fly camera pitches around
// its own X axis and yaws around world Y, and neither operation should tilt the
// horizon. Any nonzero roll therefore means some transform was applied that we
// did not intend, which is what this measurement is for.
class Roll_measurement
{
public:
    bool  roll_valid          {false}; // false when forward is (near) parallel to world up - roll is not measurable there
    float roll_radians        {0.0f};
    float pitch_radians       {0.0f};
    float heading_radians     {0.0f};
    float up_dot_world_up     {1.0f}; // negative == camera is upside down
    float orthonormality_error{0.0f}; // max deviation of the basis from orthonormal
    float determinant         {1.0f};
};

[[nodiscard]] auto measure_camera_orientation(const glm::mat4& world_from_node) -> Roll_measurement;
// Deliberately does not normalize: a non-unit quaternion shows up as basis scale
// in the measurement, which is exactly the defect worth seeing.
[[nodiscard]] auto measure_camera_orientation(const glm::quat& world_from_node) -> Roll_measurement;

class Camera_roll_event
{
public:
    uint64_t    frame_number        {0};
    std::string source;
    std::string detail;
    float       roll_before_degrees {0.0f};
    float       roll_after_degrees  {0.0f};
    float       delta_degrees       {0.0f};
    float       pitch_after_degrees {0.0f};
    float       orthonormality_error{0.0f};
    bool        first               {false}; // first time roll crossed the report threshold since the last clear()
    std::string callstack;
};

// Tracks the roll of a camera orientation across instrumented mutation sites and
// attributes every change to the innermost scope that caused it.
//
// Usage: wrap each site that writes the orientation in a Camera_roll_scope. On
// scope entry the monitor compares the orientation against the value it last saw
// - a difference there means something outside every instrumented scope wrote the
// transform, which is reported as its own event. On scope exit it compares again
// and attributes the delta to that scope. Nesting works: the innermost scope pops
// first, so it takes the blame, and the enclosing scope only reports whatever
// changed outside of it.
class Camera_roll_monitor
{
public:
    void set_frame_number(uint64_t frame_number);

    // Scope bookkeeping - prefer Camera_roll_scope over calling these directly.
    void push(const char* source, const Roll_measurement& current);
    void pop (const Roll_measurement& current, const std::string& detail);

    // One-shot check for a site that is not a scope (a single write, or a
    // periodic sanity check). Compares against the last seen orientation.
    void sample(const char* source, const Roll_measurement& current, const std::string& detail);

    // Forget the current orientation without reporting: use when the camera
    // legitimately jumps (camera switch, deserialize) and comparing to the
    // previous camera's orientation would be meaningless.
    void rebase(const Roll_measurement& current);

    void clear();

    // Names the writer of the next camera transform write that reaches the
    // controller from outside. Set it immediately before a direct write to the
    // camera node, so the resulting report says who did it instead of just
    // "external node transform write". Consumed by the next report.
    void set_next_write_hint(std::string hint);

    [[nodiscard]] auto get_events                () const -> const std::deque<Camera_roll_event>&;
    [[nodiscard]] auto get_current               () const -> const Roll_measurement&;
    [[nodiscard]] auto get_max_abs_roll_degrees  () const -> float;
    [[nodiscard]] auto get_suppressed_log_count  () const -> std::size_t;

    // Settings (exposed in the Fly Camera window)
    bool  enabled                  {true};
    bool  capture_callstack        {true};
    bool  break_on_roll            {false}; // DebugBreak() on the first reported event, to catch the writer in a debugger
    float delta_threshold_degrees  {0.0005f}; // smallest roll change that gets recorded - catches slow drift
    float report_threshold_degrees {0.01f};   // absolute roll that counts as "the camera is now visibly wrong"

private:
    class Scope
    {
    public:
        const char* source{nullptr};
    };

    void report(const char* source, const std::string& detail, const Roll_measurement& before, const Roll_measurement& after);

    std::string                   m_next_write_hint;
    std::vector<Scope>            m_stack;
    std::deque<Camera_roll_event> m_events;
    Roll_measurement              m_current;
    bool                          m_has_current       {false};
    bool                          m_first_reported    {false};
    float                         m_max_abs_roll_degrees{0.0f};
    uint64_t                      m_frame_number      {0};
    uint64_t                      m_last_log_frame    {0};
    std::size_t                   m_max_events        {256};
    std::size_t                   m_suppressed_logs   {0};
};

class Camera_roll_scope
{
public:
    // watched must outlive the scope - pass the orientation member that the
    // instrumented code mutates in place.
    Camera_roll_scope(Camera_roll_monitor& monitor, const char* source, const glm::quat& watched);
    ~Camera_roll_scope();

    Camera_roll_scope (const Camera_roll_scope&) = delete;
    auto operator=    (const Camera_roll_scope&) -> Camera_roll_scope& = delete;

    void set_detail(std::string detail);

private:
    Camera_roll_monitor& m_monitor;
    const glm::quat&     m_watched;
    std::string          m_detail;
};

}

// Declares a scope named roll_scope that attributes any roll change made while
// it is alive to source. watched must outlive the scope.
#define ERHE_CAMERA_ROLL_SCOPE(monitor, source, watched) \
    ::editor::Camera_roll_scope roll_scope{monitor, source, watched}

// Attaches a detail string to the enclosing ERHE_CAMERA_ROLL_SCOPE. detail_expr
// is only evaluated when the diagnostics are enabled, so the fmt::format calls
// at the instrumented sites cost nothing when they are not.
#define ERHE_CAMERA_ROLL_DETAIL(detail_expr) \
    do { roll_scope.set_detail(detail_expr); } while (false)

#else // ERHE_CAMERA_ROLL_DIAGNOSTICS

#define ERHE_CAMERA_ROLL_SCOPE(monitor, source, watched) do {} while (false)
#define ERHE_CAMERA_ROLL_DETAIL(detail_expr)             do {} while (false)

#endif // ERHE_CAMERA_ROLL_DIAGNOSTICS
