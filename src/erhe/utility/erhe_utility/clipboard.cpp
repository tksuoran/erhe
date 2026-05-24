#include "erhe_utility/clipboard.hpp"

#if defined(__ANDROID__)
#   include <android/log.h>
#   include <string>
#elif defined(ERHE_WINDOW_LIBRARY_SDL)
#   include <SDL3/SDL_clipboard.h>
#   include <string>
#endif

namespace erhe::utility {

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

} // namespace erhe::utility
