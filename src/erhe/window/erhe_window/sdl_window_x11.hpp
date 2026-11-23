#pragma once

namespace erhe::window {

// SDL picks a depth-32 visual for SDL_WINDOW_TRANSPARENT only for OpenGL windows on X11.
void request_x11_alpha_visual();

} // namespace erhe::window
