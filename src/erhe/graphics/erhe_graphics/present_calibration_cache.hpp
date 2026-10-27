#pragma once

#include "erhe_graphics/present_calibration.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::window { class Context_window; }

namespace erhe::graphics {

// Present calibration cache (doc/erhe/graphics.md "Present calibration
// cache"): the present calibration (present_calibration.hpp) measured on one
// display, display mode, OS build, GPU and present path is reused by later
// runs on the same combination instead of running the probes again.

// Bumped on any change to the calibration runner's constants or phases
// (present_calibration_runner.cpp) or to the probe layer / swapchain
// configuration of a backend (pixel format, drawable / image count, display
// sync, queue depth): an entry measured under another version is never
// looked up (the version is part of the key, so the file name differs).
inline constexpr int c_present_calibration_version = 2;

// Which presentation path the probes ran on. The display and compositor are
// shared between them; the path is part of the key because each path
// presents through its own API.
enum class Present_path : unsigned int {
    metal = 0,               // Metal 3: presentDrawable:atTime:
    metal4,                  // Metal 4: MTLDrawable presentAtTime:
    moltenvk_display_timing, // Vulkan on MoltenVK: VK_GOOGLE_display_timing
    vulkan_display_timing    // another Vulkan driver honoring VK_GOOGLE_display_timing
};
[[nodiscard]] auto c_str(Present_path value) -> const char*;

enum class Present_window_mode : unsigned int {
    windowed = 0,
    fullscreen
};
[[nodiscard]] auto c_str(Present_window_mode value) -> const char*;

// Everything the calibration result depends on, from OS APIs (never from
// window-system display names). Two keys compare equal only when every
// field is equal.
class Present_calibration_key
{
public:
    // CGDisplayVendorNumber / CGDisplayModelNumber / CGDisplaySerialNumber
    // of the window's NSScreen (NSScreenNumber).
    std::uint32_t       display_vendor           {0};
    std::uint32_t       display_model            {0};
    std::uint32_t       display_serial           {0};
    // CGDisplayCopyDisplayMode: pixel size, refresh rate (Hz, 0 for some
    // built-in panels) and IO flags.
    std::uint32_t       mode_pixel_width         {0};
    std::uint32_t       mode_pixel_height        {0};
    double              mode_refresh_rate        {0.0};
    std::uint32_t       mode_io_flags            {0};
    // NSScreen: backingScaleFactor and the refresh range (the source of
    // Surface_impl::query_display_presentation), seconds.
    double              backing_scale_factor     {0.0};
    double              minimum_refresh_interval {0.0};
    double              maximum_refresh_interval {0.0};
    double              update_granularity       {0.0};
    std::int64_t        maximum_frames_per_second{0};
    Present_window_mode window_mode              {Present_window_mode::windowed};
    // NSProcessInfo operatingSystemVersion "major.minor.patch" and the
    // kern.osversion build string.
    std::string         os_version;
    std::string         os_build;
    // MTLDevice name (Metal), VkPhysicalDeviceProperties deviceName (Vulkan).
    std::string         gpu_name;
    Present_path        present_path             {Present_path::metal};
    int                 calibration_version      {c_present_calibration_version};

    [[nodiscard]] auto operator==(const Present_calibration_key& other) const -> bool = default;
};

// The key of the display the window is on now (main thread: AppKit).
// nullopt where there is no such query (not Apple, no window, the window has
// no screen yet).
[[nodiscard]] auto query_present_calibration_key(
    erhe::window::Context_window* window,
    std::string_view              gpu_name,
    Present_path                  present_path
) -> std::optional<Present_calibration_key>;

// FNV-1a 64 of the key's canonical text: stable across builds and runs.
[[nodiscard]] auto get_present_calibration_key_hash(const Present_calibration_key& key) -> std::uint64_t;

// <directory>/<16 hex digits of the key hash>.json
[[nodiscard]] auto make_present_calibration_cache_path(
    const std::filesystem::path&   directory,
    const Present_calibration_key& key
) -> std::filesystem::path;

class Present_calibration_cache_entry
{
public:
    Present_calibration_key key;
    std::string             display_label;         // window system display name, informational only
    // done, valid, status, period, request window, deadline and samples;
    // prewarm_seconds_per_iteration is measured every start and not stored.
    Present_calibration     calibration;
    std::int64_t            measured_unix_seconds{0};
    std::string             measured_utc;          // ISO 8601, informational
    std::string             build;                 // erhe::version::get_description() of the writer
};

// Reads the entry and checks that its key fields equal the given key.
// nullopt with the reason when the file is absent, unreadable, malformed or
// written for another key.
[[nodiscard]] auto read_present_calibration_cache_file(
    const std::filesystem::path&   path,
    const Present_calibration_key& key,
    std::string&                   out_reason
) -> std::optional<Present_calibration_cache_entry>;

// Writes the entry atomically (temporary file in the same directory, then
// rename), creating the directory when needed. Floats are hexfloat strings.
[[nodiscard]] auto write_present_calibration_cache_file(
    const std::filesystem::path&           path,
    const Present_calibration_cache_entry& entry
) -> bool;

enum class Present_calibration_source : unsigned int {
    none = 0, // no calibration on this path, or cleared after a display change
    measured, // the probes ran in this process
    cached    // adopted from the cache
};
[[nodiscard]] auto c_str(Present_calibration_source value) -> const char*;

// What Device reports about its present calibration (MCP, Settings window).
class Present_calibration_state
{
public:
    Present_calibration                    calibration;
    Present_calibration_source             source{Present_calibration_source::none};
    std::optional<Present_calibration_key> key;
    std::string                            display_label;
    std::string                            cache_file;            // file name of the key's entry; empty without a key
    std::int64_t                           measured_unix_seconds{0};
    std::string                            measured_utc;
    bool                                   cache_reads_enabled{false};
};

enum class Present_calibration_cache_reads : unsigned int {
    disabled = 0, // always measure (and rewrite the entry)
    enabled
};

// The key of the window's display now, with the window system's display name
// (a label only).
class Present_calibration_key_query
{
public:
    std::optional<Present_calibration_key> key;
    std::string                            display_label;
};

// Fixed-capacity summary for per-frame UI (no heap allocation).
class Present_calibration_summary
{
public:
    bool                       done  {false};
    bool                       valid {false};
    Present_calibration_source source{Present_calibration_source::none};
    std::array<char, 32>       measured_utc{};
    std::array<char, 32>       cache_file  {};
};

// Owned by Device: the current calibration, where it came from, the
// calibrations of this session per key, and the cache file operations
// (present_calibration_cache.cpp). Thread-safe: the presenting thread reads
// the request offset, the main thread changes the calibration on a display
// change. File reads, writes and deletes run outside the mutex.
class Present_calibration_store
{
public:
    Present_calibration_store(Present_calibration_cache_reads cache_reads, std::filesystem::path directory);

    // Device init: the window, GPU and present path the key is queried for
    // (here and on display changes).
    void set_key_source(erhe::window::Context_window* window, std::string_view gpu_name, Present_path present_path);
    [[nodiscard]] auto is_keyed () const -> bool;
    // Main thread (AppKit).
    [[nodiscard]] auto query_key() const -> Present_calibration_key_query;

    // Device init, before the probes: on a cache file hit with a valid
    // entry, adopts it (keeping in_out's prewarm rate) and returns true: the
    // caller skips the probes.
    [[nodiscard]] auto begin(const Present_calibration_key_query& query, Present_calibration& in_out) -> bool;
    // Device init, after the probes: stores the result as measured,
    // remembers it for this session and, when valid and keyed, writes the
    // cache entry.
    void finish_measured(const Present_calibration& result);

    // Display changed (main thread): on a changed key adopts this session's
    // calibration of that key, else the cache file's entry (when reads are
    // enabled), else clears the calibration. Returns true when the
    // calibration changed. The probes never run here.
    [[nodiscard]] auto refresh_for_display(const Present_calibration_key_query& query) -> bool;
    // Deletes the current key's entry (stale evidence or an explicit
    // re-measure); the session keeps its calibration. Returns the file name
    // deleted, empty when there was none.
    auto remove_entry(std::string_view reason) -> std::string;

    [[nodiscard]] auto get_calibration   () const -> Present_calibration;
    [[nodiscard]] auto get_state         () const -> Present_calibration_state;
    void               get_summary       (Present_calibration_summary& out) const;
    [[nodiscard]] auto get_request_offset() const -> std::optional<double>;

private:
    class Session_entry
    {
    public:
        Present_calibration_key    key;
        Present_calibration        calibration;
        Present_calibration_source source{Present_calibration_source::none};
        std::int64_t               measured_unix_seconds{0};
        std::string                measured_utc;
    };

    void set_key_locked               (const Present_calibration_key_query& query);
    [[nodiscard]] auto find_session_entry_locked(const Present_calibration_key& key) const -> const Session_entry*;
    void remember_session_entry_locked();
    [[nodiscard]] auto read_file_entry(const Present_calibration_key& key, const Present_calibration& live, Present_calibration& out) const -> std::optional<Present_calibration_cache_entry>;

    mutable std::mutex                     m_mutex;
    const Present_calibration_cache_reads  m_cache_reads;
    const std::filesystem::path            m_directory;
    erhe::window::Context_window*          m_window{nullptr};
    std::string                            m_gpu_name;
    Present_path                           m_present_path{Present_path::metal};
    bool                                   m_keyed{false}; // set_key_source() ran
    Present_calibration                    m_calibration;
    Present_calibration_source             m_source{Present_calibration_source::none};
    std::optional<Present_calibration_key> m_key;
    std::string                            m_display_label;
    std::string                            m_cache_file;
    std::int64_t                           m_measured_unix_seconds{0};
    std::string                            m_measured_utc;
    std::vector<Session_entry>             m_session_entries; // valid calibrations of this session, per key
};

} // namespace erhe::graphics
