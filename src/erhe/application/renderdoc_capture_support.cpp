#include "log.hpp"

#include "erhe/toolkit/verify.hpp"

//// #include <renderdoc_app.h>

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

namespace editor {

//// RENDERDOC_API_1_1_2* renderdoc_api{nullptr};

void initialize_renderdoc_capture_support()
{

#if 0 // defined(_WIN32) || defined(WIN32)
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
            log_renderdoc.trace("RenderDoc: RENDERDOC_GetAPI() not found in renderdoc.dll\n");
            return;
        }
        //auto RENDERDOC_MaskOverlayBits = reinterpret_cast<pRENDERDOC_MaskOverlayBits>(
        //    GetProcAddress(
        //        renderdoc_module,
        //        "RENDERDOC_MaskOverlayBits"
        //    )
        //);

        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **)&renderdoc_api);
        log_renderdoc.trace("Loaded RenderDoc DLL, RENDERDOC_GetAPI() return value = {}\n", ret);
        ERHE_VERIFY(ret == 1);

        if (renderdoc_api->MaskOverlayBits == nullptr)
        {
            log_renderdoc.trace("RenderDoc: RENDERDOC_MaskOverlayBits() not found in renderdoc.dll\n");
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
        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **)&renderdoc_api);
        log_renderdoc.trace("Loaded RenderDoc DLL, RENDERDOC_GetAPI() return value = {}\n", ret);
        ERHE_VERIFY(ret == 1);
    }
#endif
    /////if (renderdoc_api != nullptr)
    /////{
    /////    log_renderdoc.trace("RenderDoc: SetCaptureKeys(nullptr, 0)\n");
    /////    RENDERDOC_InputButton capture_keys[] = { eRENDERDOC_Key_F1 };
    /////    renderdoc_api->SetCaptureKeys(&capture_keys[0], 1);
    /////    renderdoc_api->SetCaptureOptionU32(RENDERDOC_CaptureOption::eRENDERDOC_Option_DebugOutputMute, 0);
    ///// }
}

//// auto get_renderdoc_api() -> RENDERDOC_API_1_1_2*
//// {
////     return renderdoc_api;
//// }

}
