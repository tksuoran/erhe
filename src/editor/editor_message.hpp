#pragma once

#include <memory>

namespace editor
{

class Changed_flag_bit
{
public:
    static constexpr uint64_t c_flag_bit_selection         = (1u << 0);
    static constexpr uint64_t c_flag_bit_viewport          = (1u << 1);
    static constexpr uint64_t c_flag_bit_hover             = (1u << 2);
    static constexpr uint64_t c_flag_bit_graphics_settings = (1u << 3);
};

class Scene_view;

class Editor_message
{
public:
    uint64_t    changed{0};
    Scene_view* scene_view{nullptr};
};

}
