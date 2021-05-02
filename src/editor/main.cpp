#include "application.hpp"
#include "log.hpp"

#if __unix__
#   include <dlfcn.h>
#endif
using namespace editor;

#include "renderdoc_app.h"

RENDERDOC_API_1_1_2* renderdoc_api{nullptr};

auto main(int argc, char** argv)
-> int
{
    erhe::log::Log::console_init();

    static_cast<void>(argc);
    static_cast<void>(argv);

#if 1
#if defined(_WIN32) || defined(WIN32)
    HMODULE renderdoc_module = LoadLibraryExA("C:\\Program Files\\RenderDoc\\renderdoc.dll", NULL, 0);
    if (renderdoc_module)
    {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(renderdoc_module, "RENDERDOC_GetAPI");
        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **)&renderdoc_api);
        log_renderdoc.trace("Loaded RenderDoc DLL, RENDERDOC_GetAPI() return value = {}\n", ret);
        VERIFY(ret == 1);
    }
#elif __unix__
    // For android replace librenderdoc.so with libVkLayer_GLES_RenderDoc.so
    void* renderdoc_so = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
    if (renderdoc_so != nullptr)
    {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(renderdoc_so, "RENDERDOC_GetAPI");
        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **)&renderdoc_api);
        log_renderdoc.trace("Loaded RenderDoc DLL, RENDERDOC_GetAPI() return value = {}\n", ret);
        VERIFY(ret == 1);
    }
#endif
#endif
    if (renderdoc_api != nullptr)
    {
        log_renderdoc.trace("RenderDoc: SetCaptureKeys(nullptr, 0)\n");
        RENDERDOC_InputButton capture_keys[] = { eRENDERDOC_Key_F1 };
        renderdoc_api->SetCaptureKeys(&capture_keys[0], 1);
    }

    std::shared_ptr<Application> g_application;

    int return_value = EXIT_FAILURE;

    g_application = std::make_shared<Application>(renderdoc_api);
    if (g_application->on_load())
    {
        g_application->run();
        return_value = EXIT_SUCCESS;
    }
    else
    {
        return_value = EXIT_FAILURE;
    }

    g_application.reset();

    exit(return_value);
}
