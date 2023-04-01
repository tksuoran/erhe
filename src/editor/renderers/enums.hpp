#pragma once

namespace editor
{

//enum class Fill_mode : int
//{
//    fill    = 0,
//    outline = 1
//};

enum class Blend_mode : int
{
    opaque      = 0,
    translucent = 1
};

//enum class Selection_mode : int
//{
//    not_selected = 0,
//    selected     = 1,
//    any          = 2
//};

enum class Visualization_mode : unsigned int
{
    off = 0,
    selected,
    all
};

enum class Primitive_color_source : unsigned int
{
    id_offset = 0,
    mesh_wireframe_color,
    constant_color,
};

enum class Primitive_size_source : unsigned int
{
    mesh_point_size = 0,
    mesh_line_width,
    constant_size
};

}
