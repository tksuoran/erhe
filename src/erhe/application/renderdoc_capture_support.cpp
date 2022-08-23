#include "erhe/application/renderdoc_capture_support.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/toolkit/renderdoc_app.h"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/window.hpp"

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

namespace erhe::application {

RENDERDOC_API_1_5_0* renderdoc_api{nullptr};

Renderdoc_capture_support::Renderdoc_capture_support()
    : erhe::components::Component{c_type_name}
{
}


void Renderdoc_capture_support::declare_required_components()
{
    //m_configuration = require<Configuration>();
}

void Renderdoc_capture_support::initialize_component()
{
    if (m_is_initialized)
    {
        return;
    }
    const auto& config = m_components->get<Configuration>();
        //get<Configuration>();
    if (!config->renderdoc.capture_support)
    {
        return;
    }

#if defined(_WIN32) || defined(WIN32)
    HMODULE renderdoc_module = LoadLibraryExA("C:\\Program Files\\RenderDoc\\renderdoc.dll", NULL, 0);
    if (renderdoc_module)
    {
        auto RENDERDOC_GetAPI = reinterpret_cast<pRENDERDOC_GetAPI>(
            GetProcAddress(
                renderdoc_module,
                "RENDERDOC_GetAPI"
            )
        );
        if (RENDERDOC_GetAPI == nullptr)
        {
            log_renderdoc->warn("RenderDoc: RENDERDOC_GetAPI() not found in renderdoc.dll");
            return;
        }

        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_5_0, (void **)&renderdoc_api);
        log_renderdoc->trace("Loaded RenderDoc DLL, RENDERDOC_GetAPI() return value = {}", ret);
        ERHE_VERIFY(ret == 1);

        if (renderdoc_api->MaskOverlayBits == nullptr)
        {
            log_renderdoc->warn("RenderDoc: RENDERDOC_MaskOverlayBits() not found in renderdoc.dll");
        }
        else
        {
            renderdoc_api->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
        }

    }
#elif __unix__
    // For android replace librenderdoc.so with libVkLayer_GLES_RenderDoc.so
    void* renderdoc_so = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
    if (renderdoc_so != nullptr)
    {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(renderdoc_so, "RENDERDOC_GetAPI");
        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_5_0, (void **)&renderdoc_api);
        log_renderdoc->trace("Loaded RenderDoc DLL, RENDERDOC_GetAPI() return value = {}", ret);
        ERHE_VERIFY(ret == 1);
    }
#endif
    m_is_initialized = true;
}

void Renderdoc_capture_support::start_frame_capture(const erhe::toolkit::Context_window* context_window)
{
    if (!renderdoc_api || (context_window == nullptr) || !m_is_initialized)
    {
        return;
    }
    log_renderdoc->info("RenderDoc: StartFrameCapture()");

    const auto device = context_window->get_device_pointer();
    const auto window = context_window->get_window_handle();
    renderdoc_api->DiscardFrameCapture(device, window);
    renderdoc_api->SetActiveWindow(device, window);
    //renderdoc_api->TriggerCapture();
    renderdoc_api->StartFrameCapture(device, window);
}

void Renderdoc_capture_support::end_frame_capture(const erhe::toolkit::Context_window* context_window)
{
    if (!renderdoc_api || (context_window == nullptr) || !m_is_initialized)
    {
        return;
    }
    log_renderdoc->info("RenderDoc: EndFrameCapture()");
    const auto device = context_window->get_device_pointer();
    const auto window = context_window->get_window_handle();
    renderdoc_api->EndFrameCapture(device, window);
    renderdoc_api->LaunchReplayUI(1, nullptr);
    renderdoc_api->ShowReplayUI();
}

}
