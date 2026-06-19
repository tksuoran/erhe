#pragma once

#include <cstdint>

namespace erhe::primitive {

enum class Normal_style : unsigned int {
    none            = 0,
    corner_normals  = 1,
    polygon_normals = 2,
    point_normals   = 3
};

enum class Primitive_mode : unsigned int {
    not_set           = 0,
    polygon_fill      = 1,
    edge_lines        = 2,
    corner_points     = 3,
    corner_normals    = 4,
    polygon_centroids = 5,
    solid_wireframe   = 6, // expanded fill triangles, drawn with the solid-wireframe shader variant
    count             = 7
};

enum class Primitive_type : unsigned int {
    none                     = 0,
    line_loop                = 1,
    line_strip               = 2,
    line_strip_adjacency     = 3,
    lines                    = 4,
    lines_adjacency          = 5,
    patches                  = 6,
    points                   = 7,
    quads                    = 8,
    triangle_fan             = 9,
    triangle_strip           = 10,
    triangle_strip_adjacency = 11,
    triangles                = 12,
    triangles_adjacency      = 13
};

enum class Bxdf_model : uint16_t
{
    unlit                    = 0,
    isotropic_brdf           = 1,
    anisotropic_brdf         = 2,
    anisotropic_slope        = 3,
    anisotropic_engine_ready = 4
};

enum class Material_blending_mode : uint16_t
{
    opaque       = 0,
    alpha_blend  = 1,
    multiply     = 2,
    add          = 3,
    subtract     = 4,
    screen_door  = 5,
    alpha_test   = 6
};

static constexpr const char* c_mode_strings[] = {
    "Not Set",
    "Polygon Fill",
    "Edge Lines",
    "Corner Points",
    "Corner Normals",
    "Polygon Centroids",
    "Solid Wireframe"
};

static constexpr const char* c_normal_style_strings[] = {
    "None",
    "Corner Normals",
    "Polygon Normals",
    "Point Normals"
};

static const char* const c_material_blending_mode_names[] = {
    "Opaque",
    "Alpha Blend",
    "Multiply",
    "Add",
    "Subtract",
    "Screen Door",
    "Alpha Test"
};

[[nodiscard]] auto c_str(Primitive_mode primitive_mode) -> const char*;
[[nodiscard]] auto c_str(Normal_style normal_style) -> const char*;
[[nodiscard]] auto c_str(Bxdf_model bxdf_model) -> const char*;
[[nodiscard]] auto c_str(Material_blending_mode blending_mode) -> const char*;

} // namespace erhe::primitive
