#include "erhe_window/sdl_window_x11.hpp"
#include "erhe_window/window_log.hpp"

#include <fmt/format.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_video.h>

// Xlib last: its macros (None, Bool, Status, ...) must not reach other headers.
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <string>
#include <string_view>

namespace erhe::window {

void request_x11_alpha_visual()
{
    const char* const video_driver = SDL_GetCurrentVideoDriver();
    if ((video_driver == nullptr) || (std::string_view{video_driver} != "x11")) {
        return;
    }
    const char* const forced_visual = SDL_GetHint(SDL_HINT_VIDEO_X11_WINDOW_VISUALID);
    if ((forced_visual != nullptr) && (forced_visual[0] != '\0')) {
        return;
    }
    Display* const display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        log_window->warn("Window transparency: XOpenDisplay() failed, the window stays opaque");
        return;
    }
    XVisualInfo visual_info{};
    const int found = XMatchVisualInfo(display, DefaultScreen(display), 32, TrueColor, &visual_info);
    XCloseDisplay(display);
    if (found == 0) {
        log_window->warn("Window transparency: no depth-32 TrueColor X11 visual, the window stays opaque");
        return;
    }
    const std::string visual_id = fmt::format("0x{:x}", static_cast<unsigned long>(visual_info.visualid));
    SDL_SetHint(SDL_HINT_VIDEO_X11_WINDOW_VISUALID, visual_id.c_str());
    log_window->info("Window transparency: using depth-32 X11 visual {}", visual_id);
}

} // namespace erhe::window
