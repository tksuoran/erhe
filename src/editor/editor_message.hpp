#pragma once

#include <memory>

namespace editor
{

class Message_flag_bit
{
public:
    static constexpr uint64_t c_flag_bit_selection         = (1u << 0);
    static constexpr uint64_t c_flag_bit_viewport          = (1u << 1);
    static constexpr uint64_t c_flag_bit_hover             = (1u << 2);
    static constexpr uint64_t c_flag_bit_scene_view        = (1u << 3);
    static constexpr uint64_t c_flag_bit_graphics_settings = (1u << 4);
};

class Scene_view;

class Editor_message
{
public:
    uint64_t    update_flags{0};
    Scene_view* scene_view  {nullptr};
};

}
