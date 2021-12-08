#pragma once

namespace erhe::primitive
{

enum class Normal_style : unsigned int
{
    none            = 0,
    corner_normals  = 1,
    polygon_normals = 2,
    point_normals   = 3
};

enum class Primitive_mode : unsigned int
{
    not_set           = 0,
    polygon_fill      = 1,
    edge_lines        = 2,
    corner_points     = 3,
    corner_normals    = 4,
    polygon_centroids = 5,
    count             = 6
};

static constexpr const char* c_mode_strings[] =
{
    "Not Set",
    "Polygon Fill",
    "Edge Lines",
    "Corner Points",
    "Corner Normals",
    "Polygon Centroids"
};

static constexpr const char* c_normal_style_strings[] =
{
    "None",
    "Corner Normals",
    "Polygon Normals",
    "Point Normals"
};

[[nodiscard]] auto c_str(const Primitive_mode primitive_mode) -> const char*;
[[nodiscard]] auto c_str(const Normal_style normal_style) -> const char*;

}