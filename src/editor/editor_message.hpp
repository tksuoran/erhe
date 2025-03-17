#pragma once

#include <memory>
#include <vector>

namespace erhe {
    class Item_base;
}
namespace erhe::scene {
    class Node;
}

namespace editor {

class Graphics_preset;

class Message_flag_bit
{
public:
    static constexpr uint64_t c_flag_bit_selection                    = (1u << 0);
    static constexpr uint64_t c_flag_bit_hover_viewport               = (1u << 1);
    static constexpr uint64_t c_flag_bit_hover_mesh                   = (1u << 2);
    static constexpr uint64_t c_flag_bit_hover_scene_view             = (1u << 3);
    static constexpr uint64_t c_flag_bit_hover_scene_item_tree        = (1u << 4);
    static constexpr uint64_t c_flag_bit_graphics_settings            = (1u << 5);
    static constexpr uint64_t c_flag_bit_render_scene_view            = (1u << 6);
    static constexpr uint64_t c_flag_bit_tool_select                  = (1u << 7);
    static constexpr uint64_t c_flag_bit_node_touched_operation_stack = (1u << 8);
    static constexpr uint64_t c_flag_bit_node_touched_transform_tool  = (1u << 9);
    static constexpr uint64_t c_flag_bit_animation_update             = (1u << 10);
};

class Scene_root;
class Scene_view;

class Editor_message
{
public:
    uint64_t                                      update_flags      {0};
    Scene_view*                                   scene_view        {nullptr};
    Scene_root*                                   scene_root        {nullptr};
    erhe::scene::Node*                            node              {nullptr};
    std::vector<std::shared_ptr<erhe::Item_base>> no_longer_selected{};
    std::vector<std::shared_ptr<erhe::Item_base>> newly_selected    {};
    Graphics_preset*                              graphics_preset   {nullptr};
};

}
