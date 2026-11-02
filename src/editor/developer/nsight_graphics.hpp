#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace editor {

// Nsight Graphics activities started by the editor itself through the
// Nsight Graphics SDK (doc/editor/nsight_graphics.md): a GPU Trace or a
// Graphics Capture of frames a script picked over MCP, so the result covers
// a scene state the script set up instead of a timed guess. ngfx launches
// the editor with the activity injected and waiting for the application
// (GPU Trace: --start-with-ngfx-sdk --stop-with-ngfx-sdk; Graphics Capture:
// --hotkey-capture). Built only with ERHE_NSIGHT_GRAPHICS_SDK_DIR on the
// Vulkan backend; other builds report the tools as unavailable.
class Nsight_graphics
{
public:
    enum class Activity : unsigned int
    {
        gpu_trace,
        graphics_capture
    };

    enum class State : unsigned int
    {
        idle,     // nothing requested yet
        pending,  // requested; starts at a later render phase frame start
        tracing,  // GPU Trace started; the stop is issued at the start of the last frame
        stopped,  // GPU Trace stop issued (ends at the next frame boundary)
        capturing,// Graphics Capture requested from the SDK (frames end at presents)
        failed    // an SDK call failed; get_status().error says which
    };

    class Status
    {
    public:
        bool          sdk_built          {false};
        Activity      activity           {Activity::gpu_trace};
        State         state              {State::idle};
        std::uint32_t frame_count        {0};
        std::uint32_t frames_started     {0};
        std::string   ngfx_status;       // GPU Trace: the SDK's status, empty when it cannot be queried
        std::uint32_t capture_file_count {0}; // Graphics Capture: completed capture files of this run
        std::string   capture_file_path; // Graphics Capture: the newest completed capture file
        std::string   error;
    };

    // Main thread (MCP): runs the activity over frame_count consecutive
    // frames, starting at the render phase frame start skip_frames frames
    // after the next one. Returns an empty string, or why nothing was
    // requested.
    [[nodiscard]] auto request   (Activity activity, std::uint32_t frame_count, std::uint32_t skip_frames) -> std::string;
    [[nodiscard]] auto get_status() -> Status;

    // Render phase, at the start of each frame (before any submit): issues
    // the SDK calls of a requested activity.
    void on_frame_start();

private:
    [[nodiscard]] auto initialize(Activity activity) -> std::string;

    std::mutex    m_mutex;
    bool          m_initialized   {false};
    Activity      m_activity      {Activity::gpu_trace};
    State         m_state         {State::idle};
    std::uint32_t m_frame_count   {0};
    std::uint32_t m_skip_frames   {0};
    std::uint32_t m_frames_started{0};
    std::string   m_error;
};

[[nodiscard]] auto to_string(Nsight_graphics::Activity activity) -> const char*;
[[nodiscard]] auto to_string(Nsight_graphics::State state) -> const char*;

} // namespace editor
