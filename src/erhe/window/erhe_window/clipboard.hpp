#pragma once

#include <string>
#include <string_view>

namespace erhe::window {

// Copy diagnostic text to the system clipboard for inspection by the
// developer. When SDL is the selected window library this calls
// SDL_SetClipboardText. On Android the message is emitted to logcat under
// tag "erhe.clipboard" instead, because app processes have no general-purpose
// system clipboard SDL can write to and the diagnostic dumps callers pass
// here can exceed the binder parcel size limit. For other window libraries
// (glfw) or a headless build (none) there is no clipboard backend and this
// is a no-op.
void copy_to_clipboard(std::string_view text);

// The system clipboard as text (SDL desktop builds); false where there is none
// (Android, glfw, headless), where the setter is a no-op and the getter is empty.
[[nodiscard]] auto has_system_clipboard() -> bool;
void set_clipboard_text(std::string_view text);
[[nodiscard]] auto get_clipboard_text() -> std::string;

} // namespace erhe::window
