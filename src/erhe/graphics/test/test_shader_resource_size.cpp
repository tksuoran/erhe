// Regression tests for Shader_resource struct-size rounding.
//
// Before this fix, Shader_resource::get_size_bytes() returned the raw
// post-last-member offset for Type::struct_type, without rounding up to
// the struct's GLSL base alignment (std140 / std430 rule 9). Callers
// that sized a UBO from a Shader_resource therefore reported a smaller
// size than the actual std140 layout consumed -- e.g. a struct with raw
// offset 40 (vec4 + vec4 + float + uint) reported 40 instead of 48.
//
// The math now lives in two free functions, glsl_struct_base_alignment
// and glsl_round_up_size, which these tests exercise directly. The free
// functions are pure and do not require a graphics Device, which keeps
// this test target deviceless and CI-friendly.

#include <gtest/gtest.h>

#include "erhe_graphics/shader_resource.hpp"

using erhe::graphics::glsl_struct_base_alignment;
using erhe::graphics::glsl_round_up_size;
using Layout = erhe::graphics::Shader_resource::Layout;

TEST(ShaderResourceSize, std140_struct_of_floats_base_alignment)
{
    // A struct whose members are all floats has member_max_alignment = 4.
    // Under std140 rule 9 the struct base alignment is rounded up to
    // vec4 (16). Under std430 it stays at 4.
    EXPECT_EQ(glsl_struct_base_alignment(4, Layout::std140), 16u);
    EXPECT_EQ(glsl_struct_base_alignment(4, Layout::std430), 4u);
}

TEST(ShaderResourceSize, std140_struct_with_vec4_base_alignment)
{
    // A struct containing a vec4 has member_max_alignment = 16. Both
    // layouts agree: base alignment 16.
    EXPECT_EQ(glsl_struct_base_alignment(16, Layout::std140), 16u);
    EXPECT_EQ(glsl_struct_base_alignment(16, Layout::std430), 16u);
}

TEST(ShaderResourceSize, std430_struct_with_vec2_base_alignment)
{
    // member_max_alignment = 8 (a vec2). std430 keeps 8; std140 rounds
    // up to vec4 = 16.
    EXPECT_EQ(glsl_struct_base_alignment(8, Layout::std430), 8u);
    EXPECT_EQ(glsl_struct_base_alignment(8, Layout::std140), 16u);
}

TEST(ShaderResourceSize, round_up_size_already_aligned)
{
    EXPECT_EQ(glsl_round_up_size(64u, 16u), 64u);
    EXPECT_EQ(glsl_round_up_size(48u, 16u), 48u);
    EXPECT_EQ(glsl_round_up_size( 0u, 16u),  0u);
}

TEST(ShaderResourceSize, round_up_size_pads_to_next_multiple)
{
    // The canonical case in the bug report: vec4 + vec4 + float + uint
    // = 40 raw bytes, std140 base alignment 16, padded size 48.
    EXPECT_EQ(glsl_round_up_size(40u, 16u), 48u);
    EXPECT_EQ(glsl_round_up_size(12u, 16u), 16u);
    EXPECT_EQ(glsl_round_up_size( 1u, 16u), 16u);
    EXPECT_EQ(glsl_round_up_size(17u, 16u), 32u);
}

TEST(ShaderResourceSize, round_up_size_std430_no_pad_for_scalar_struct)
{
    // A struct of three floats: raw size 12, std430 base alignment 4,
    // padded size 12 (no padding under std430).
    EXPECT_EQ(glsl_round_up_size(12u, 4u), 12u);
}

TEST(ShaderResourceSize, round_up_size_eight_byte_alignment)
{
    // uint64 member only: base alignment 8.
    EXPECT_EQ(glsl_round_up_size(12u, 8u), 16u);
    EXPECT_EQ(glsl_round_up_size( 8u, 8u),  8u);
    EXPECT_EQ(glsl_round_up_size( 0u, 8u),  0u);
}
