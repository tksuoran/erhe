#pragma once

#include <imgui/imgui.h>

#include <string>
#include <string_view>
#include <vector>

namespace erhe::imgui {

template <typename T>
class Graph
{
public:
    Graph(std::string_view name, std::string_view x_label, std::string_view x_unit, std::string_view y_label, std::string_view y_unit)
        : name   {name}
        , x_label{x_label}
        , x_unit {x_unit}
        , y_label{y_label}
        , y_unit {y_unit}
    {
    }

    void clear() { samples.clear(); }

    std::vector<T>   samples;
    std::string      name;
    std::string_view x_label;
    std::string_view x_unit;
    std::string_view y_label;
    std::string_view y_unit;       // aabbggrr
    ImU32            path_color    {0xffcc8822};
    ImU32            key_color     {0xcc33bb33};
    ImU32            hover_color   {0xffdd9966};
    float            path_thickness{1.0f};
    bool             draw_path     {true};
    bool             draw_keys     {true};
    bool             plot          {false};
    float            y_scale       {1.0f};
};

} // namespace erhe::imgui
