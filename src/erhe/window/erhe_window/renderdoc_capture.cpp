#include "erhe_window/renderdoc_capture.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_log.hpp"
#include "erhe_window/renderdoc_app.h"
#include "erhe_utility/env.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#if defined(ERHE_OS_WINDOWS)
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

#if defined(ERHE_OS_LINUX) || defined(ERHE_OS_MACOS) || defined(ERHE_OS_ANDROID)
#   include <dlfcn.h>
#endif

namespace erhe::window {

namespace {

RENDERDOC_API_1_7_0* renderdoc_api{nullptr};
bool is_initialized{false};

// Fetch the in-application API from an already-resolved RENDERDOC_GetAPI and
// store it in renderdoc_api. Returns true on success.
//
// RENDERDOC_GetAPI() fails outright (returns 0) when asked for a version the
// loaded module predates - it does not negotiate down - so the versions erhe can
// use are tried newest first. This matters because RenderDoc Meta Fork, the
// supported capture tool on Quest, is forked from RenderDoc 1.41 and tops out at
// API 1.6.0; asking only for 1.7.0 fails against it.
//
// Every RENDERDOC_API_1_*_0 name in renderdoc_app.h is a typedef of the newest
// struct and the struct is append-only, so an older module still fills in every
// entry point erhe calls - all of which existed by 1.6.0. The one hazard is that
// below 1.7.0 the struct RenderDoc returns is SHORTER than RENDERDOC_API_1_7_0:
// its final two members, SetObjectAnnotation and SetCommandAnnotation (new in
// 1.7.0), are absent and must never be called. erhe uses neither; anything added
// here that does must check the negotiated version first.
auto negotiate_renderdoc_api(const pRENDERDOC_GetAPI RENDERDOC_GetAPI) -> bool
{
    static constexpr RENDERDOC_Version versions[] = {
        eRENDERDOC_API_Version_1_7_0,
        eRENDERDOC_API_Version_1_6_0
    };
    for (const RENDERDOC_Version version : versions) {
        if (RENDERDOC_GetAPI(version, reinterpret_cast<void**>(&renderdoc_api)) == 1) {
            log_renderdoc->info("RenderDoc: in-application API version {} negotiated", static_cast<int>(version));
            return true;
        }
    }
    renderdoc_api = nullptr;
    log_renderdoc->warn(
        "RenderDoc: RENDERDOC_GetAPI() rejected every API version erhe can use ({} and {}); "
        "in-application frame capture is unavailable",
        static_cast<int>(eRENDERDOC_API_Version_1_7_0),
        static_cast<int>(eRENDERDOC_API_Version_1_6_0)
    );
    return false;
}

#if defined(ERHE_OS_ANDROID)
// Attach to a RenderDoc capture layer that something else has already loaded
// into this process. Returns true once renderdoc_api is resolved.
//
// erhe never loads the layer itself: RTLD_NOLOAD attaches to the module only if
// it is already present, which on Quest means the app was launched from the
// RenderDoc host with injection (see doc/quest_renderdoc_capture.md). Passive
// attach is deliberate on two counts. It is free when RenderDoc is not attached,
// so it can be attempted on every run rather than gated on a config flag baked
// into the APK. And it cannot produce a second copy of the layer in the process,
// which would trip RenderDoc's own multi-instance __debugbreak() - the same
// hazard apply_renderdoc_override_env() works to avoid on Windows.
auto attach_injected_capture_layer() -> bool
{
    if (renderdoc_api != nullptr) {
        return true;
    }
    void* renderdoc_so = dlopen("libVkLayer_GLES_RenderDoc.so", RTLD_NOW | RTLD_NOLOAD);
    if (renderdoc_so == nullptr) {
        return false;
    }
    pRENDERDOC_GetAPI RENDERDOC_GetAPI = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(renderdoc_so, "RENDERDOC_GetAPI"));
    if (RENDERDOC_GetAPI == nullptr) {
        log_renderdoc->warn("RenderDoc: RENDERDOC_GetAPI() not found in libVkLayer_GLES_RenderDoc.so");
        return false;
    }
    log_renderdoc->info("RenderDoc: attaching to injected capture layer");
    return negotiate_renderdoc_api(RENDERDOC_GetAPI);
}
#endif

void set_env_or_warn(const char* key, const char* value)
{
    if (!erhe::utility::set_env(key, value)) {
        log_renderdoc->warn("RenderDoc: failed to set environment variable {}={}", key, value);
    }
}

// Apply the environment variables needed to make the Vulkan loader pick up the
// override RenderDoc library's implicit capture layer (an experimental fork at
// a custom location) instead of the system-installed RenderDoc layer. Must run
// before vkCreateInstance. The layer search path is derived from the override
// library's own directory so it stays in sync with the override library path.
//
// Loading two RenderDoc copies into one process trips RenderDoc's multi-instance
// __debugbreak(), so the stock install's layer must be kept out. Both manifests
// declare the same layer name (VK_LAYER_RENDERDOC_Capture) and the same
// version-less enable key (ENABLE_VULKAN_RENDERDOC_CAPTURE), so the enable knob
// alone cannot separate them. Two independent mechanisms are used:
//
//  1. VK_IMPLICIT_LAYER_PATH (an override, unlike the additive
//     VK_ADD_IMPLICIT_LAYER_PATH) makes the loader scan ONLY the override
//     library's directory for implicit layers, so the stock layer is excluded
//     whatever its version. This is the version-proof mechanism and needs no
//     maintenance when either install is upgraded. VK_ADD_IMPLICIT_LAYER_PATH is
//     set to the same directory as well, so the fork layer is found regardless
//     of which of the two variables a given loader build honors.
//  2. The per-version DISABLE_VULKAN_RENDERDOC_CAPTURE_1_NN keys, as a fallback
//     for a loader too old for VK_IMPLICIT_LAYER_PATH (< 1.3.234). Each
//     renderdoc.json names its own key via "disable_environment"; these are the
//     stock-install versions seen so far. The fork's OWN key must never be
//     set: the fork's version moves with every rebase (1.46 -> 1.45 -> ...)
//     and stock installs are upgraded independently, so the key the fork
//     declares in the renderdoc.json next to the override library is read
//     and skipped instead of relying on a hardcoded version pairing.
//     Mechanism 1 covers the case where the stock list has gone stale.
auto read_own_disable_key(const std::filesystem::path& layer_directory) -> std::string
{
    std::ifstream manifest{layer_directory / "renderdoc.json"};
    if (!manifest) {
        return {};
    }
    const std::string text{std::istreambuf_iterator<char>{manifest}, std::istreambuf_iterator<char>{}};
    const std::string_view prefix{"DISABLE_VULKAN_RENDERDOC_CAPTURE_"};
    const std::size_t start = text.find(prefix);
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t end = text.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return text.substr(start, end - start);
}

void apply_renderdoc_override_env(std::string_view library_path_override)
{
    const std::filesystem::path library_path{library_path_override};
    const std::string layer_directory = library_path.parent_path().string();
    const std::string own_disable_key = read_own_disable_key(library_path.parent_path());
    if (own_disable_key.empty()) {
        log_renderdoc->warn(
            "RenderDoc: could not read disable_environment key from '{}' - the stock-version disable "
            "list below may disable the override layer itself",
            (library_path.parent_path() / "renderdoc.json").string()
        );
    }
    for (const char* stock_disable_key : {
        "DISABLE_VULKAN_RENDERDOC_CAPTURE_1_45",
        "DISABLE_VULKAN_RENDERDOC_CAPTURE_1_44",
        "DISABLE_VULKAN_RENDERDOC_CAPTURE_1_41"
    }) {
        if (own_disable_key == stock_disable_key) {
            log_renderdoc->info("RenderDoc: not setting {} - it is the override layer's own disable key", stock_disable_key);
            continue;
        }
        set_env_or_warn(stock_disable_key, "1");
    }
    set_env_or_warn("VK_IMPLICIT_LAYER_PATH", layer_directory.c_str());
    set_env_or_warn("VK_ADD_IMPLICIT_LAYER_PATH", layer_directory.c_str());
    set_env_or_warn("ENABLE_VULKAN_RENDERDOC_CAPTURE", "1");
    log_renderdoc->info(
        "RenderDoc: override library '{}' active, VK_IMPLICIT_LAYER_PATH='{}'",
        library_path_override, layer_directory
    );
}

}

void initialize_frame_capture(std::string_view library_path_override)
{
    const bool have_override = !library_path_override.empty();

#if defined(ERHE_OS_MACOS)
    // The system-installed RenderDoc (qRenderDoc) cannot wrap the macOS Vulkan
    // driver (Mesa KosmicKrisp, Vulkan-on-Metal). Loading its librenderdoc.dylib
    // makes RenderDoc insert its Vulkan capture layer, whose hooked vkCreateDevice
    // crashes (SIGTRAP) building its VulkanShaderCache before KosmicKrisp's real
    // device creation runs - so the editor never starts. Until the erhe RenderDoc
    // fork supports macOS, only initialize RenderDoc when an explicit override
    // library path (that fork) is supplied via renderdoc_library_path; never load
    // the stock library here. Re-enabling on macOS is then a config change (point
    // renderdoc_library_path at the fork), not a code change.
    if (!have_override) {
        log_renderdoc->info(
            "RenderDoc: stock RenderDoc capture is disabled on macOS (incompatible "
            "with the KosmicKrisp Vulkan driver). Set renderdoc_library_path to the "
            "erhe RenderDoc fork to enable capture."
        );
        return;
    }
#endif

    if (have_override) {
        apply_renderdoc_override_env(library_path_override);
    }

#if defined(ERHE_OS_WINDOWS)
    const std::string library_path = have_override
        ? std::string{library_path_override}
        : std::string{"C:\\Program Files\\RenderDoc\\renderdoc.dll"};
    HMODULE renderdoc_module = LoadLibraryExA(library_path.c_str(), NULL, 0);
    if (renderdoc_module) {
        auto RENDERDOC_GetAPI = reinterpret_cast<pRENDERDOC_GetAPI>(
            GetProcAddress(renderdoc_module, "RENDERDOC_GetAPI")
        );
        if (RENDERDOC_GetAPI == nullptr) {
            log_renderdoc->warn("RenderDoc: RENDERDOC_GetAPI() not found in renderdoc.dll");
            return;
        }

        log_renderdoc->trace("Loaded RenderDoc .dll");
        negotiate_renderdoc_api(RENDERDOC_GetAPI);
    } else {
        log_renderdoc->warn("RenderDoc: failed to load library '{}'", library_path);
    }
#elif defined(ERHE_OS_LINUX)
    // For android replace librenderdoc.so with libVkLayer_GLES_RenderDoc.so
    std::string library_path = have_override
        ? std::string{library_path_override}
        : std::string{"librenderdoc.so"};
    void* renderdoc_so = dlopen(library_path.c_str(), RTLD_NOW);
    if (renderdoc_so == nullptr) {
        library_path = std::string{"/opt/renderdoc/lib/librenderdoc.so"};
        renderdoc_so = dlopen(library_path.c_str(), RTLD_NOW);
    }
    if (renderdoc_so != nullptr) {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(renderdoc_so, "RENDERDOC_GetAPI");
        log_renderdoc->trace("Loaded RenderDoc .so");
        negotiate_renderdoc_api(RENDERDOC_GetAPI);
    }
#elif defined(ERHE_OS_ANDROID)
    // library_path_override is not honored here: the layer lives inside the
    // RenderDoc APK, at a path that is not erhe's to choose.
    //
    // This first attempt usually finds nothing, and that is expected. The layer
    // is loaded by the Vulkan loader during vkCreateInstance, whereas this runs
    // at window creation - earlier. try_attach_frame_capture(), called once the
    // Vulkan instance exists, is the attempt that actually succeeds.
    static_cast<void>(have_override);
    attach_injected_capture_layer();
#elif defined(ERHE_OS_MACOS)
    const std::string library_path = have_override
        ? std::string{library_path_override}
        : std::string{"/Applications/qrenderdoc.app/Contents/lib/librenderdoc.dylib"};
    void* renderdoc_so = dlopen(library_path.c_str(), RTLD_NOW);
    if (renderdoc_so != nullptr) {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(renderdoc_so, "RENDERDOC_GetAPI");
        log_renderdoc->trace("Loaded RenderDoc .dylib");
        negotiate_renderdoc_api(RENDERDOC_GetAPI);
    } else {
        log_renderdoc->warn("dlopen librenderdoc.dylib failed: {}", dlerror());
    }
#endif
    // Enable capturing callstacks
    if (renderdoc_api != nullptr) {
        if (renderdoc_api->MaskOverlayBits == nullptr) {
            log_renderdoc->warn("RenderDoc: RENDERDOC_MaskOverlayBits() not found in renderdoc.dll");
        } else {
            renderdoc_api->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
        }

        renderdoc_api->SetCaptureOptionU32(eRENDERDOC_Option_CaptureCallstacks, 1);
    }

    is_initialized = true;
}

void try_attach_frame_capture()
{
#if defined(ERHE_OS_ANDROID)
    if (renderdoc_api != nullptr) {
        return;
    }
    if (!attach_injected_capture_layer()) {
        log_renderdoc->info(
            "RenderDoc: libVkLayer_GLES_RenderDoc.so is not loaded in this process - in-application "
            "frame capture is unavailable. Launch the app from the RenderDoc host so it injects the "
            "capture layer."
        );
        return;
    }
    // Match what initialize_frame_capture() configures on the platforms that
    // load the library themselves, so a capture taken through an injected layer
    // behaves the same.
    if (renderdoc_api->MaskOverlayBits != nullptr) {
        renderdoc_api->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
    }
    renderdoc_api->SetCaptureOptionU32(eRENDERDOC_Option_CaptureCallstacks, 1);
#endif
}

auto get_renderdoc_api() -> void*
{
    return renderdoc_api;
}

#if 0
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
#endif

}
