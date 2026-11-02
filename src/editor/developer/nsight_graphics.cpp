#include "developer/nsight_graphics.hpp"

#include "editor_log.hpp"

#if defined(ERHE_NSIGHT_GRAPHICS_SDK)
#   include <vulkan/vulkan.h>
#   include "NGFX_GPUTrace_Vulkan.h"
#   include "NGFX_GraphicsCapture_Vulkan.h"
#endif

#include <fmt/format.h>

#include <array>

namespace editor {

auto to_string(const Nsight_graphics::Activity activity) -> const char*
{
    switch (activity) {
        case Nsight_graphics::Activity::gpu_trace:        return "gpu_trace";
        case Nsight_graphics::Activity::graphics_capture: return "graphics_capture";
        default:                                          return "?";
    }
}

auto to_string(const Nsight_graphics::State state) -> const char*
{
    switch (state) {
        case Nsight_graphics::State::idle:      return "idle";
        case Nsight_graphics::State::pending:   return "pending";
        case Nsight_graphics::State::tracing:   return "tracing";
        case Nsight_graphics::State::stopped:   return "stopped";
        case Nsight_graphics::State::capturing: return "capturing";
        case Nsight_graphics::State::failed:    return "failed";
        default:                                return "?";
    }
}

#if defined(ERHE_NSIGHT_GRAPHICS_SDK)
namespace {

auto c_str(const NGFX_Result result) -> const char*
{
    switch (result) {
        case NGFX_Result_Success:                   return "Success";
        case NGFX_Result_NotImplemented:            return "NotImplemented";
        case NGFX_Result_LibNotFound:               return "LibNotFound";
        case NGFX_Result_InvalidLib:                return "InvalidLib";
        case NGFX_Result_DifferentActivityInjected: return "DifferentActivityInjected";
        case NGFX_Result_InvalidParameter:          return "InvalidParameter";
        case NGFX_Result_InvalidState:              return "InvalidState";
        case NGFX_Result_UnspecifiedError:          return "UnspecifiedError";
        case NGFX_Result_Timeout:                   return "Timeout";
        case NGFX_Result_InsufficientBuffer:        return "InsufficientBuffer";
        default:                                    return "?";
    }
}

auto c_str(const NGFX_GPUTrace_Status status) -> const char*
{
    switch (status) {
        case NGFX_GPUTrace_Status_Inactive: return "inactive";
        case NGFX_GPUTrace_Status_Active:   return "active";
        case NGFX_GPUTrace_Status_Tracing:  return "tracing";
        case NGFX_GPUTrace_Status_Draining: return "draining";
        case NGFX_GPUTrace_Status_Errored:  return "errored";
        default:                            return "?";
    }
}

} // anonymous namespace
#endif

auto Nsight_graphics::initialize(const Activity activity) -> std::string
{
#if defined(ERHE_NSIGHT_GRAPHICS_SDK)
    if (m_initialized) {
        if (activity != m_activity) {
            return fmt::format("the {} activity is initialized; one process runs one activity", to_string(m_activity));
        }
        return {};
    }
    // Succeeds only in a process ngfx launched with that activity injected;
    // InvalidState means it was not.
    NGFX_Result result = NGFX_Result_UnspecifiedError;
    const char* ngfx_arguments = "";
    if (activity == Activity::gpu_trace) {
        NGFX_GPUTrace_InitializeActivity_Vulkan_Params params{};
        params.version = NGFX_GPUTrace_InitializeActivity_Vulkan_Params_VER;
        result = NGFX_GPUTrace_InitializeActivity_Vulkan(&params);
        ngfx_arguments = "--activity \"GPU Trace Profiler\" --start-with-ngfx-sdk --stop-with-ngfx-sdk";
    } else {
        NGFX_GraphicsCapture_InitializeActivity_Vulkan_Params params{};
        params.version = NGFX_GraphicsCapture_InitializeActivity_Vulkan_Params_VER;
        result = NGFX_GraphicsCapture_InitializeActivity_Vulkan(&params);
        ngfx_arguments = "--activity \"Graphics Capture\" --hotkey-capture";
    }
    if (result != NGFX_Result_Success) {
        return fmt::format("initializing the Nsight {} activity failed: {} (the editor must run under ngfx {})", to_string(activity), c_str(result), ngfx_arguments);
    }
    m_initialized = true;
    m_activity    = activity;
    return {};
#else
    static_cast<void>(activity);
    return "editor built without ERHE_NSIGHT_GRAPHICS_SDK_DIR (Vulkan only)";
#endif
}

auto Nsight_graphics::request(const Activity activity, const std::uint32_t frame_count, const std::uint32_t skip_frames) -> std::string
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    if ((m_state == State::pending) || (m_state == State::tracing)) {
        return fmt::format("a {} is already {}", to_string(m_activity), to_string(m_state));
    }
    if (frame_count == 0) {
        return "frame_count must be at least 1";
    }
    if ((activity == Activity::graphics_capture) && (frame_count > 60)) {
        return "a Graphics Capture covers at most 60 frames";
    }
    const std::string error = initialize(activity);
    if (!error.empty()) {
        return error;
    }
    m_state          = State::pending;
    m_frame_count    = frame_count;
    m_skip_frames    = skip_frames;
    m_frames_started = 0;
    m_error.clear();
    log_render->info("Nsight {} requested: {} frame(s) after skipping {}", to_string(activity), frame_count, skip_frames);
    return {};
}

auto Nsight_graphics::get_status() -> Status
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    Status status{
        .sdk_built          = false,
        .activity           = m_activity,
        .state              = m_state,
        .frame_count        = m_frame_count,
        .frames_started     = m_frames_started,
        .ngfx_status        = {},
        .capture_file_count = 0,
        .capture_file_path  = {},
        .error              = m_error
    };
#if defined(ERHE_NSIGHT_GRAPHICS_SDK)
    status.sdk_built = true;
    if (!m_initialized) {
        return status;
    }
    if (m_activity == Activity::gpu_trace) {
        NGFX_GPUTrace_GetStatus_Params params{};
        params.version = NGFX_GPUTrace_GetStatus_Params_VER;
        const NGFX_Result result = NGFX_GPUTrace_GetStatus(&params);
        status.ngfx_status = (result == NGFX_Result_Success) ? c_str(params.status) : c_str(result);
        return status;
    }
    NGFX_ArtifactFileCount_Params count_params{};
    count_params.version = NGFX_ArtifactFileCount_Params_VER;
    if (NGFX_GraphicsCapture_GetCaptureFileCount(&count_params) != NGFX_Result_Success) {
        return status;
    }
    status.capture_file_count = count_params.count;
    if (count_params.count > 0) {
        std::array<NGFX_PathChar, 4096> path{};
        NGFX_ArtifactFilePath_Params path_params{};
        path_params.version          = NGFX_ArtifactFilePath_Params_VER;
        path_params.artifactIndex    = count_params.count - 1;
        path_params.filePath         = path.data();
        path_params.filePathCapacity = static_cast<uint32_t>(path.size());
        if (NGFX_GraphicsCapture_GetCaptureFilePath(&path_params) == NGFX_Result_Success) {
            status.capture_file_path = path.data();
        }
    }
#endif
    return status;
}

void Nsight_graphics::on_frame_start()
{
#if defined(ERHE_NSIGHT_GRAPHICS_SDK)
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (m_state == State::pending) {
        if (m_skip_frames > 0) {
            --m_skip_frames;
            return;
        }
        if (m_activity == Activity::graphics_capture) {
            // The capture state machine starts at the next present and ends
            // after frame_count presents by itself.
            NGFX_GraphicsCapture_RequestCapture_Vulkan_Params params{};
            params.version           = NGFX_GraphicsCapture_RequestCapture_Vulkan_Params_VER;
            params.delimiter         = NGFX_GraphicsCapture_Delimiter_Present;
            params.framesBeforeStart = 0;
            params.framesToCapture   = m_frame_count;
            const NGFX_Result result = NGFX_GraphicsCapture_RequestCapture_Vulkan(&params);
            if (result != NGFX_Result_Success) {
                m_error = fmt::format("NGFX_GraphicsCapture_RequestCapture_Vulkan failed: {}", c_str(result));
                m_state = State::failed;
                log_render->error("Nsight Graphics Capture: {}", m_error);
                return;
            }
            log_render->info("Nsight Graphics Capture requested from the SDK: {} frame(s)", m_frame_count);
            m_state = State::capturing;
            return;
        }
        NGFX_GPUTrace_StartTrace_Vulkan_Params params{};
        params.version = NGFX_GPUTrace_StartTrace_Vulkan_Params_VER;
        const NGFX_Result result = NGFX_GPUTrace_StartTrace_Vulkan(&params);
        if (result != NGFX_Result_Success) {
            m_error = fmt::format("NGFX_GPUTrace_StartTrace_Vulkan failed: {}", c_str(result));
            m_state = State::failed;
            log_render->error("Nsight GPU trace: {}", m_error);
            return;
        }
        log_render->info("Nsight GPU trace started");
        m_state = State::tracing;
    }
    if (m_state != State::tracing) {
        return;
    }
    ++m_frames_started;
    if (m_frames_started < m_frame_count) {
        return;
    }
    // The last traced frame: the trace ends at the frame boundary after it
    // (its present), so no queue is needed.
    NGFX_GPUTrace_StopTrace_Vulkan_Params params{};
    params.version = NGFX_GPUTrace_StopTrace_Vulkan_Params_VER;
    params.flags   = NGFX_GPUTrace_StopTraceFlag_NextFrameBoundary;
    const NGFX_Result result = NGFX_GPUTrace_StopTrace_Vulkan(&params);
    if (result != NGFX_Result_Success) {
        m_error = fmt::format("NGFX_GPUTrace_StopTrace_Vulkan failed: {}", c_str(result));
        m_state = State::failed;
        log_render->error("Nsight GPU trace: {}", m_error);
        return;
    }
    log_render->info("Nsight GPU trace stop issued after {} frame(s)", m_frames_started);
    m_state = State::stopped;
#endif
}

} // namespace editor
