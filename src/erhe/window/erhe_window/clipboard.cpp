#include "erhe_window/clipboard.hpp"

#if defined(__ANDROID__)
#   include <android/log.h>
#   include <string>
#elif defined(ERHE_WINDOW_LIBRARY_SDL)
#   include <SDL3/SDL_clipboard.h>
#   include <SDL3/SDL_stdinc.h>
#   include <string>
#endif

namespace erhe::window {

void copy_to_clipboard(std::string_view text)
{
#if defined(__ANDROID__)
    __android_log_write(ANDROID_LOG_INFO, "erhe.clipboard", "=== clipboard ===");
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        if (end > start) {
            const std::string line(text.data() + start, end - start);
            __android_log_write(ANDROID_LOG_INFO, "erhe.clipboard", line.c_str());
        }
        start = end + 1;
    }
#elif defined(ERHE_WINDOW_LIBRARY_SDL)
    const std::string null_terminated{text};
    SDL_SetClipboardText(null_terminated.c_str());
#else
    // No SDL clipboard backend for the selected window library (glfw / none):
    // copy_to_clipboard() is a no-op.
    static_cast<void>(text);
#endif
}

auto has_system_clipboard() -> bool
{
#if defined(ERHE_WINDOW_LIBRARY_SDL) && !defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}

void set_clipboard_text(std::string_view text)
{
#if defined(ERHE_WINDOW_LIBRARY_SDL) && !defined(__ANDROID__)
    const std::string null_terminated{text};
    SDL_SetClipboardText(null_terminated.c_str());
#else
    static_cast<void>(text);
#endif
}

auto get_clipboard_text() -> std::string
{
#if defined(ERHE_WINDOW_LIBRARY_SDL) && !defined(__ANDROID__)
    char* const text = SDL_GetClipboardText();
    if (text == nullptr) {
        return {};
    }
    std::string result{text};
    SDL_free(text);
    return result;
#else
    return {};
#endif
}

} // namespace erhe::window
