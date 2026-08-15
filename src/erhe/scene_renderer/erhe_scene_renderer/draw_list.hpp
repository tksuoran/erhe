#pragma once

#include "erhe_scene_renderer/draw_list_entry.hpp"
#include "erhe_scene_renderer/draw_list_key.hpp"

#include <vector>

namespace erhe::scene_renderer {

// A group of entries that share one Draw_list_key and can therefore be
// drawn with one pipeline, one buffer bind and one multi-draw
// (doc/draw_list_renderer_requirements.md R13/R14). Owned by
// Draw_list_scene; entries are appended on registration and swap-removed on
// unregistration (Draw_list_scene patches the moved entry's owner record).
class Draw_list
{
public:
    Draw_list_key                key;
    std::vector<Draw_list_entry> entries;
};

} // namespace erhe::scene_renderer
