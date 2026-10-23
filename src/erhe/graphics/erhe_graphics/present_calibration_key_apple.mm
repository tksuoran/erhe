// Present calibration cache key on Apple platforms (present_calibration_cache.hpp):
// the window's NSScreen, its CGDisplay identity and mode, and the OS build.
// Shared by the Metal, Metal 4 and Vulkan (MoltenVK) backends: the display
// and the compositor are the same objects whichever API presents.

#include "erhe_graphics/present_calibration_cache.hpp"
#include "erhe_window/window.hpp"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

#include <fmt/format.h>

#include <sys/sysctl.h>

#include <array>

#if defined(ERHE_WINDOW_LIBRARY_SDL)
#   include <SDL3/SDL.h>
#endif

namespace erhe::graphics {

namespace {

auto get_os_build() -> std::string
{
    std::array<char, 256> buffer{};
    std::size_t size = buffer.size();
    if (sysctlbyname("kern.osversion", buffer.data(), &size, nullptr, 0) != 0) {
        return {};
    }
    return std::string{buffer.data()};
}

} // anonymous namespace

auto query_present_calibration_key(
    erhe::window::Context_window* const window,
    const std::string_view              gpu_name,
    const Present_path                  present_path
) -> std::optional<Present_calibration_key>
{
    if (window == nullptr) {
        return std::nullopt;
    }
#if defined(ERHE_WINDOW_LIBRARY_SDL)
    SDL_Window* const sdl_window = static_cast<SDL_Window*>(window->get_sdl_window());
    if (sdl_window == nullptr) {
        return std::nullopt;
    }
    @autoreleasepool {
        NSWindow* const ns_window = (__bridge NSWindow*)SDL_GetPointerProperty(
            SDL_GetWindowProperties(sdl_window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr
        );
        NSScreen* const screen = (ns_window != nil) ? [ns_window screen] : nil;
        if (screen == nil) {
            return std::nullopt;
        }
        NSNumber* const screen_number = [[screen deviceDescription] objectForKey: @"NSScreenNumber"];
        if (screen_number == nil) {
            return std::nullopt;
        }
        const CGDirectDisplayID display_id = static_cast<CGDirectDisplayID>([screen_number unsignedIntValue]);

        Present_calibration_key key{};
        key.display_vendor = CGDisplayVendorNumber(display_id);
        key.display_model  = CGDisplayModelNumber (display_id);
        key.display_serial = CGDisplaySerialNumber(display_id);
        // No identity (virtual, Sidecar and AirPlay displays report vendor
        // and model 0) or no mode: no key, so no caching. A serial of 0 is
        // common for real panels and stays acceptable.
        if ((key.display_vendor == 0) && (key.display_model == 0)) {
            return std::nullopt;
        }
        CGDisplayModeRef const mode = CGDisplayCopyDisplayMode(display_id);
        if (mode == nullptr) {
            return std::nullopt;
        }
        key.mode_pixel_width  = static_cast<std::uint32_t>(CGDisplayModeGetPixelWidth (mode));
        key.mode_pixel_height = static_cast<std::uint32_t>(CGDisplayModeGetPixelHeight(mode));
        key.mode_refresh_rate = CGDisplayModeGetRefreshRate(mode);
        key.mode_io_flags     = CGDisplayModeGetIOFlags(mode);
        CGDisplayModeRelease(mode);
        key.backing_scale_factor = static_cast<double>([screen backingScaleFactor]);
        if (@available(macOS 12.0, *)) {
            key.minimum_refresh_interval  = static_cast<double>([screen minimumRefreshInterval]);
            key.maximum_refresh_interval  = static_cast<double>([screen maximumRefreshInterval]);
            key.update_granularity        = static_cast<double>([screen displayUpdateGranularity]);
            key.maximum_frames_per_second = static_cast<std::int64_t>([screen maximumFramesPerSecond]);
        }
        key.window_mode = window->is_fullscreen() ? Present_window_mode::fullscreen : Present_window_mode::windowed;

        const NSOperatingSystemVersion os = [[NSProcessInfo processInfo] operatingSystemVersion];
        key.os_version = fmt::format("{}.{}.{}", static_cast<long>(os.majorVersion), static_cast<long>(os.minorVersion), static_cast<long>(os.patchVersion));
        key.os_build   = get_os_build();

        key.gpu_name            = std::string{gpu_name};
        key.present_path        = present_path;
        key.calibration_version = c_present_calibration_version;
        return key;
    }
#else
    static_cast<void>(gpu_name);
    static_cast<void>(present_path);
    return std::nullopt;
#endif
}

} // namespace erhe::graphics
