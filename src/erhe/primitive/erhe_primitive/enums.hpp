#pragma once

#include "erhe_graphics/enums.hpp"
#include "erhe_property/enum_info.hpp"

#include <cstddef>
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

// Which build of a Primitive's renderable data is meant. Each build is a
// separate Primitive_render_shape hanging off the Primitive, carrying its own
// Buffer_mesh and its own Element_mappings - see Primitive.
//
// `original` is source order and carries valid per-corner facet ids. It is
// ALWAYS built, which is what lets the ID renderer, picking and ray tracing
// work unconditionally - none of them ever look at `optimized`.
//
// `optimized` is the meshoptimizer build: welded and reordered, with the facet
// id bytes zeroed because welding makes them meaningless. It exists only when
// mesh optimization is enabled, and it is dropped again on the first GPU vertex
// edit. There is no "active variant" state: a renderer queries which variants
// are present and chooses.
// uint16_t so it fits the padding slot in the hot Draw_list_entry.
enum class Mesh_variant : uint16_t {
    original  = 0,
    optimized = 1,
    count     = 2
};

[[nodiscard]] auto c_str(Mesh_variant variant) -> const char*;

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

// Normal map storage encoding. Right handed = OpenGL Y convention, left
// handed = Direct3D (the shader flips Y). Three channel stores unit
// normals as RGB = XYZ; two channel stores X+Y only and the shader
// reconstructs Z. The two-channel channel layout is part of the value:
// GA = X in RGB / Y in A (KTX2 normal-mode; RGBA8, ASTC L+A blocks),
// RG = X in R / Y in G (BC5).
#define ERHE_NORMALMAP_ENCODING_RIGHT_HANDED_THREE_CHANNEL  0 // OpenGL
#define ERHE_NORMALMAP_ENCODING_RIGHT_HANDED_TWO_CHANNEL_GA 1 // OpenGL
#define ERHE_NORMALMAP_ENCODING_LEFT_HANDED_THREE_CHANNEL   2 // Direct3D
#define ERHE_NORMALMAP_ENCODING_LEFT_HANDED_TWO_CHANNEL_GA  3 // Direct3D
#define ERHE_NORMALMAP_ENCODING_RIGHT_HANDED_TWO_CHANNEL_RG 4 // OpenGL
#define ERHE_NORMALMAP_ENCODING_LEFT_HANDED_TWO_CHANNEL_RG  5 // Direct3D

#define ERHE_TEXGEN_MODE_UV0      0
#define ERHE_TEXGEN_MODE_UV1      1
#define ERHE_TEXGEN_MODE_UV2      2
#define ERHE_TEXGEN_MODE_WORLD_XY 3
#define ERHE_TEXGEN_MODE_WORLD_XZ 4
#define ERHE_TEXGEN_MODE_WORLD_YZ 5
#define ERHE_TEXGEN_MODE_NODE_XY  6
#define ERHE_TEXGEN_MODE_NODE_XZ  7
#define ERHE_TEXGEN_MODE_NODE_YZ  8
#define ERHE_TEXGEN_MODE_TANGENT  9

enum class Texgen_mode : uint16_t {
    uv0      = ERHE_TEXGEN_MODE_UV0     ,
    uv1      = ERHE_TEXGEN_MODE_UV1     ,
    uv2      = ERHE_TEXGEN_MODE_UV2     ,
    world_xy = ERHE_TEXGEN_MODE_WORLD_XY,
    world_xz = ERHE_TEXGEN_MODE_WORLD_XZ,
    world_yz = ERHE_TEXGEN_MODE_WORLD_YZ,
    node_xy  = ERHE_TEXGEN_MODE_NODE_XY ,
    node_xz  = ERHE_TEXGEN_MODE_NODE_XZ ,
    node_yz  = ERHE_TEXGEN_MODE_NODE_YZ ,
    tangent  = ERHE_TEXGEN_MODE_TANGENT 
};

// Which channel of a bound texture a scalar material input reads. USD's
// UsdPreviewSurface names the channel in the connection (`outputs:r`,
// `outputs:g`, ...); glTF fixes the packing, and the defaults are glTF's:
// metallic in B, roughness in G, occlusion in R, opacity in A. The value is
// the component index the shader indexes the sampled vec4 with, so it
// travels to the GPU as it stands.
//
// `none` is the input that reads no channel of the slot's texture: its
// factor stands on its own. One slot carries the metallic-roughness image
// that UsdPreviewSurface reads through two separate inputs, so a file that
// textures one of them and gives the other a plain value names `none` for
// that other input; the shader multiplies by one where it stands. It is the
// index one past the sampled vec4's components, which is how the shader
// recognizes it.
enum class Texture_channel : uint16_t {
    r    = 0,
    g    = 1,
    b    = 2,
    a    = 3,
    none = 4
};

enum class Normalmap_encoding : uint16_t {
    right_handed_three_channel  = ERHE_NORMALMAP_ENCODING_RIGHT_HANDED_THREE_CHANNEL,
    right_handed_two_channel_ga = ERHE_NORMALMAP_ENCODING_RIGHT_HANDED_TWO_CHANNEL_GA,
    left_handed_three_channel   = ERHE_NORMALMAP_ENCODING_LEFT_HANDED_THREE_CHANNEL,
    left_handed_two_channel_ga  = ERHE_NORMALMAP_ENCODING_LEFT_HANDED_TWO_CHANNEL_GA,
    right_handed_two_channel_rg = ERHE_NORMALMAP_ENCODING_RIGHT_HANDED_TWO_CHANNEL_RG,
    left_handed_two_channel_rg  = ERHE_NORMALMAP_ENCODING_LEFT_HANDED_TWO_CHANNEL_RG
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

static const char* const c_bxdf_model_names[] = {
    "Unlit",
    "Isotropic BRDF",
    "Anisotropic BRDF",
    "Anisotropic Slope",
    "Anisotropic Engine-Ready"
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

static const char* const c_texgen_mode_names[] = {
    "Uv 0",
    "Uv 1",
    "Uv 2",
    "World XY",
    "World XZ",
    "World YZ",
    "Node XY",
    "Node XZ",
    "Node YZ",
    "Tangent"
};

static const char* const c_normalmap_encoding_names[] = {
    "Right Handed RGB",
    "Right Handed X+Y (GA)",
    "Left Handed RGB",
    "Left Handed X+Y (GA)",
    "Right Handed X+Y (RG)",
    "Left Handed X+Y (RG)"
};

// Enumerator tables for the erhe::property enumeration properties of
// Material (labels are the c_*_names entries above, values the enumerators).
extern const erhe::property::Enum_info c_bxdf_model_enum_info;
extern const erhe::property::Enum_info c_material_blending_mode_enum_info;
extern const erhe::property::Enum_info c_texgen_mode_enum_info;
extern const erhe::property::Enum_info c_normalmap_encoding_enum_info;
extern const erhe::property::Enum_info c_texture_channel_enum_info;
// The erhe::graphics sampler enums, for the Material slot sampler properties.
extern const erhe::property::Enum_info c_sampler_address_mode_enum_info;
extern const erhe::property::Enum_info c_filter_enum_info;
extern const erhe::property::Enum_info c_sampler_mipmap_mode_enum_info;

[[nodiscard]] auto supports_anisotropy(Bxdf_model bxdf_model) -> bool;
[[nodiscard]] auto c_str(Primitive_mode primitive_mode) -> const char*;
[[nodiscard]] auto c_str(Normal_style normal_style) -> const char*;
[[nodiscard]] auto c_str(Bxdf_model bxdf_model) -> const char*;
[[nodiscard]] auto c_str(Material_blending_mode blending_mode) -> const char*;
[[nodiscard]] auto c_str(Texgen_mode texgen_mode) -> const char*;
[[nodiscard]] auto c_str(Normalmap_encoding normalmap_encoding) -> const char*;
[[nodiscard]] auto c_str(Texture_channel texture_channel) -> const char*;
[[nodiscard]] auto to_uint32(Texgen_mode texgen_mode) -> uint32_t;
[[nodiscard]] auto to_uint32(Normalmap_encoding normalmap_encoding) -> uint32_t;
[[nodiscard]] auto to_uint32(Texture_channel texture_channel) -> uint32_t;

} // namespace erhe::primitive
