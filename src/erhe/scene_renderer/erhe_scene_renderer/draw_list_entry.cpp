#include "erhe_scene_renderer/draw_list_entry.hpp"

namespace erhe::scene_renderer {

static_assert(sizeof(Draw_list_entry) <= 64, "Draw_list_entry should stay within one cache line");

} // namespace erhe::scene_renderer
