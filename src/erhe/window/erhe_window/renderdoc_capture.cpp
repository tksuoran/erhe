#include "erhe_window/renderdoc_capture.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_log.hpp"
#include "erhe_window/renderdoc_app.h"
#include "erhe_verify/verify.hpp"

#if defined(_WIN32)
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   define VC_EXTRALEAN
#   ifndef STRICT
#       define STRICT
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX       // Macros min(a,b) and max(a,b)
#   endif
#   include <windows.h>
#endif

#if __unix__
#   include <dlfcn.h>
#endif

namespace erhe::window {

namespace {

RENDERDOC_API_1_6_0* renderdoc_api{nullptr};
bool is_initialized{false};

}

void initialize_frame_capture()
{
#if defined(_WIN32) || defined(WIN32)
    HMODULE renderdoc_module = LoadLibraryExA("C:\\Program Files\\RenderDoc\\renderdoc.dll", NULL, 0);
    if (renderdoc_module) {
        auto RENDERDOC_GetAPI = reinterpret_cast<pRENDERDOC_GetAPI>(
            GetProcAddress(renderdoc_module, "RENDERDOC_GetAPI")
        );
        if (RENDERDOC_GetAPI == nullptr) {
            log_renderdoc->warn("RenderDoc: RENDERDOC_GetAPI() not found in renderdoc.dll");
            return;
        }

        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void **)&renderdoc_api);
        log_renderdoc->trace("Loaded RenderDoc DLL, RENDERDOC_GetAPI() return value = {}", ret);
        ERHE_VERIFY(ret == 1);

        if (renderdoc_api->MaskOverlayBits == nullptr) {
            log_renderdoc->warn("RenderDoc: RENDERDOC_MaskOverlayBits() not found in renderdoc.dll");
        } else {
            renderdoc_api->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
        }

        // Enable capturing callstacks
        renderdoc_api->SetCaptureOptionU32(eRENDERDOC_Option_CaptureCallstacks, 1);
    }
#elif __unix__
    // For android replace librenderdoc.so with libVkLayer_GLES_RenderDoc.so
    void* renderdoc_so = dlopen("librenderdoc.so", RTLD_NOW);
    if (renderdoc_so != nullptr) {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(renderdoc_so, "RENDERDOC_GetAPI");
        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void **)&renderdoc_api);
        log_renderdoc->trace("Loaded RenderDoc DLL, RENDERDOC_GetAPI() return value = {}", ret);
        ERHE_VERIFY(ret == 1);
    }
#endif
    is_initialized = true;
}

void start_frame_capture(const erhe::window::Context_window& context_window)
{
    if (!renderdoc_api || !is_initialized) {
        return;
    }
    log_renderdoc->info("RenderDoc: StartFrameCapture()");

    const auto device = context_window.get_device_pointer();
    const auto window = context_window.get_window_handle();
    //renderdoc_api->DiscardFrameCapture(device, window);
    renderdoc_api->SetActiveWindow(device, window);
    renderdoc_api->StartFrameCapture(device, window);
}

void end_frame_capture(const erhe::window::Context_window& context_window)
{
    if (!renderdoc_api || !is_initialized) {
        return;
    }
    log_renderdoc->info("RenderDoc: EndFrameCapture()");
    const auto device = context_window.get_device_pointer();
    const auto window = context_window.get_window_handle();
    const uint32_t end_capture_ok = renderdoc_api->EndFrameCapture(device, window);
    if (end_capture_ok == 0) {
        log_renderdoc->warn("RenderDoc: EndFrameCapture() failed");
        return;
    }
    const uint32_t is_connected = renderdoc_api->IsTargetControlConnected();
    if (is_connected) {
        const uint32_t show_ok = renderdoc_api->ShowReplayUI();
        if (show_ok == 1) {
            return;
        } else {
            log_renderdoc->warn("RenderDoc: ShowReplayUI() failed while IsTargetControlConnected() returned 1");
        }
    }

    const uint32_t launch_pid = renderdoc_api->LaunchReplayUI(1, nullptr);
    if (launch_pid == 0) {
        log_renderdoc->warn("RenderDoc: LaunchReplayUI() failed");
    }
}

}
