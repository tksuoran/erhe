#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/mesh_optimizer.hpp"
#include "erhe_primitive/primitive.hpp"

#include "erhe_buffer/ibuffer.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

// The compact non-position attribute encodings of the optimized variant
// (doc/meshoptimizer_attribute_encodings.md): the TBN quaternion, the
// per-primitive texcoord affine, and the implicit-sum joint weights.
//
// Every one of them is a pair - a C++ encoder and a GLSL decoder that must
// agree exactly - and a mismatch does not fail, it renders subtly wrong. The
// decoders below are line-by-line C++ mirrors of the GLSL, so a change to
// either side that is not made to the other shows up here rather than as a
// puzzling image.

namespace {

using erhe::dataformat::Format;
using erhe::dataformat::Vertex_attribute_usage;
using erhe::dataformat::Vertex_format;
using erhe::dataformat::Vertex_stream;

// Mirror of erhe_decode_vertex_tbn() in res/shaders/erhe_vertex_tbn.glsl.
void decode_tbn_quaternion(
    const std::array<int16_t, 4>& encoded,
    glm::vec3&                    normal,
    glm::vec3&                    tangent,
    float&                        handedness
)
{
    const float inv_scale = 1.0f / (32767.0f * 1.4142135623730951f);

    const int control = static_cast<int>(encoded[3]);
    const int largest = control & 3;
    handedness = ((control & 4) != 0) ? -1.0f : 1.0f;

    const float qa = static_cast<float>(encoded[0]) * inv_scale;
    const float qb = static_cast<float>(encoded[1]) * inv_scale;
    const float qc = static_cast<float>(encoded[2]) * inv_scale;
    const float qd = std::sqrt(std::max(0.0f, 1.0f - qa * qa - qb * qb - qc * qc));

    float q[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    q[(largest + 1) & 3] = qa;
    q[(largest + 2) & 3] = qb;
    q[(largest + 3) & 3] = qc;
    q[largest]           = qd;

    const float xx = q[0] * q[0];
    const float yy = q[1] * q[1];
    const float zz = q[2] * q[2];
    const float xy = q[0] * q[1];
    const float xz = q[0] * q[2];
    const float yz = q[1] * q[2];
    const float wx = q[3] * q[0];
    const float wy = q[3] * q[1];
    const float wz = q[3] * q[2];

    tangent = glm::vec3(1.0f - 2.0f * (yy + zz),        2.0f * (xy + wz),        2.0f * (xz - wy));
    normal  = glm::vec3(       2.0f * (xz + wy),        2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy));
}

// Mirror of erhe_decode_vertex_joint_weights() in
// res/shaders/erhe_vertex_joint_weights.glsl.
[[nodiscard]] auto decode_implicit_sum_joint_weights(const std::array<uint16_t, 3>& encoded) -> glm::vec4
{
    const glm::vec3 stored{
        static_cast<float>(encoded[0]) / 65535.0f,
        static_cast<float>(encoded[1]) / 65535.0f,
        static_cast<float>(encoded[2]) / 65535.0f
    };
    return glm::vec4{stored, std::max(0.0f, 1.0f - (stored.x + stored.y + stored.z))};
}

// Mirror of erhe_decode_vertex_texcoord() in
// res/shaders/erhe_vertex_texcoord.glsl, for one channel.
[[nodiscard]] auto decode_texcoord(const std::array<uint16_t, 2>& encoded, const glm::vec2 scale, const glm::vec2 offset) -> glm::vec2
{
    const glm::vec2 normalized{
        static_cast<float>(encoded[0]) / 65535.0f,
        static_cast<float>(encoded[1]) / 65535.0f
    };
    return normalized * scale + offset;
}

[[nodiscard]] auto orthonormalize(const glm::vec3& normal, const glm::vec3& tangent) -> glm::vec3
{
    const glm::vec3 n = glm::normalize(normal);
    return glm::normalize(tangent - n * glm::dot(n, tangent));
}

} // namespace

TEST(AttributeEncodings, tbn_quaternion_round_trips_frames_and_handedness)
{
    // Axis-aligned frames plus a few oblique ones, so every one of the four
    // possible omitted quaternion components gets exercised.
    const glm::vec3 normals[] = {
        { 1.0f,  0.0f,  0.0f},
        {-1.0f,  0.0f,  0.0f},
        { 0.0f,  1.0f,  0.0f},
        { 0.0f, -1.0f,  0.0f},
        { 0.0f,  0.0f,  1.0f},
        { 0.0f,  0.0f, -1.0f},
        glm::normalize(glm::vec3{ 1.0f,  1.0f,  1.0f}),
        glm::normalize(glm::vec3{-1.0f,  2.0f, -0.5f}),
        glm::normalize(glm::vec3{ 0.3f, -0.7f,  0.2f}),
        glm::normalize(glm::vec3{-0.1f, -0.2f,  0.97f})
    };
    const glm::vec3 tangent_hints[] = {
        { 0.0f,  1.0f,  0.0f},
        { 0.0f,  0.0f,  1.0f},
        glm::normalize(glm::vec3{1.0f, 0.0f, 1.0f})
    };

    for (const glm::vec3& normal : normals) {
        for (const glm::vec3& hint : tangent_hints) {
            // Skip a hint parallel to the normal - that is the degenerate case,
            // covered by its own test below.
            if (std::abs(glm::dot(glm::normalize(normal), hint)) > 0.99f) {
                continue;
            }
            const glm::vec3 expected_tangent = orthonormalize(normal, hint);
            for (const float handedness : {1.0f, -1.0f}) {
                const glm::vec4 tangent4{hint.x, hint.y, hint.z, handedness};
                const std::array<int16_t, 4> encoded = erhe::primitive::encode_tbn_quaternion(normal, tangent4);

                glm::vec3 decoded_normal{0.0f};
                glm::vec3 decoded_tangent{0.0f};
                float     decoded_handedness = 0.0f;
                decode_tbn_quaternion(encoded, decoded_normal, decoded_tangent, decoded_handedness);

                // 16-bit quaternion components: the frame comes back well
                // inside a milliradian.
                EXPECT_NEAR(decoded_normal.x,  normal.x, 1e-3f);
                EXPECT_NEAR(decoded_normal.y,  normal.y, 1e-3f);
                EXPECT_NEAR(decoded_normal.z,  normal.z, 1e-3f);
                EXPECT_NEAR(decoded_tangent.x, expected_tangent.x, 1e-3f);
                EXPECT_NEAR(decoded_tangent.y, expected_tangent.y, 1e-3f);
                EXPECT_NEAR(decoded_tangent.z, expected_tangent.z, 1e-3f);
                // Handedness is a bit, not a quantized value: it is exact or it
                // is a bug. Mirrored UV shells depend on it.
                EXPECT_EQ(decoded_handedness, handedness);
                // And the decoded frame is still orthonormal.
                EXPECT_NEAR(glm::length(decoded_normal),  1.0f, 1e-3f);
                EXPECT_NEAR(glm::length(decoded_tangent), 1.0f, 1e-3f);
                EXPECT_NEAR(glm::dot(decoded_normal, decoded_tangent), 0.0f, 1e-3f);
            }
        }
    }
}

TEST(AttributeEncodings, tbn_quaternion_survives_degenerate_input)
{
    // A tangent parallel to the normal, a zero tangent, and a zero normal:
    // none of these can produce the authored frame, but none of them may
    // produce NaN either - the encoder picks an arbitrary orthogonal tangent
    // and the result still has to be a usable orthonormal frame.
    const std::pair<glm::vec3, glm::vec4> cases[] = {
        {glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec4{0.0f, 0.0f, 1.0f,  1.0f}}, // parallel
        {glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec4{0.0f, 0.0f, 0.0f,  1.0f}}, // no tangent
        {glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec4{1.0f, 0.0f, 0.0f,  1.0f}}, // no normal
        {glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec4{0.0f, 0.0f, 0.0f, -1.0f}}  // neither
    };
    for (const auto& [normal, tangent] : cases) {
        const std::array<int16_t, 4> encoded = erhe::primitive::encode_tbn_quaternion(normal, tangent);
        glm::vec3 decoded_normal{0.0f};
        glm::vec3 decoded_tangent{0.0f};
        float     decoded_handedness = 0.0f;
        decode_tbn_quaternion(encoded, decoded_normal, decoded_tangent, decoded_handedness);
        EXPECT_TRUE(std::isfinite(decoded_normal.x) && std::isfinite(decoded_normal.y) && std::isfinite(decoded_normal.z));
        EXPECT_TRUE(std::isfinite(decoded_tangent.x) && std::isfinite(decoded_tangent.y) && std::isfinite(decoded_tangent.z));
        EXPECT_NEAR(glm::length(decoded_normal),  1.0f, 1e-3f);
        EXPECT_NEAR(glm::length(decoded_tangent), 1.0f, 1e-3f);
        EXPECT_NEAR(glm::dot(decoded_normal, decoded_tangent), 0.0f, 1e-3f);
        EXPECT_EQ(decoded_handedness, (tangent.w < 0.0f) ? -1.0f : 1.0f);
    }
}

TEST(AttributeEncodings, joint_influences_sort_descending_and_permute_indices)
{
    const erhe::primitive::Joint_influences influences = erhe::primitive::sort_joint_influences(
        glm::vec4{3.0f, 1.0f, 4.0f, 7.0f},
        glm::vec4{0.1f, 0.5f, 0.2f, 0.2f}
    );
    // Descending by weight, indices carried along, and stable: the two equal
    // 0.2 weights keep their author order (4 before 7), which is what makes the
    // encoding deterministic across builds of the same mesh.
    EXPECT_EQ(influences.indices[0], 1u);
    EXPECT_EQ(influences.indices[1], 4u);
    EXPECT_EQ(influences.indices[2], 7u);
    EXPECT_EQ(influences.indices[3], 3u);
    EXPECT_NEAR(influences.weights[0], 0.5f, 1e-6f);
    EXPECT_NEAR(influences.weights[1], 0.2f, 1e-6f);
    EXPECT_NEAR(influences.weights[2], 0.2f, 1e-6f);
    EXPECT_NEAR(influences.weights[3], 0.1f, 1e-6f);
}

TEST(AttributeEncodings, joint_influences_normalize_and_handle_no_influence)
{
    // glTF requires normalized weights but does not make it true, and the
    // implicit-sum encoding depends on it, so the sort normalizes.
    const erhe::primitive::Joint_influences unnormalized = erhe::primitive::sort_joint_influences(
        glm::vec4{0.0f, 1.0f, 2.0f, 3.0f},
        glm::vec4{2.0f, 1.0f, 1.0f, 0.0f}
    );
    EXPECT_NEAR(unnormalized.weights[0] + unnormalized.weights[1] + unnormalized.weights[2] + unnormalized.weights[3], 1.0f, 1e-6f);
    EXPECT_NEAR(unnormalized.weights[0], 0.5f,  1e-6f);
    EXPECT_EQ  (unnormalized.indices[0], 0u);

    // A negative weight is not a weight; it must not poison the normalization.
    const erhe::primitive::Joint_influences negative = erhe::primitive::sort_joint_influences(
        glm::vec4{0.0f, 1.0f, 2.0f, 3.0f},
        glm::vec4{1.0f, -4.0f, 1.0f, 0.0f}
    );
    EXPECT_NEAR(negative.weights[0] + negative.weights[1] + negative.weights[2] + negative.weights[3], 1.0f, 1e-6f);
    EXPECT_NEAR(negative.weights[0], 0.5f, 1e-6f);

    // No positive weight at all collapses to "fully influenced by the first
    // joint", which is what an unskinned vertex of a skinned mesh means.
    const erhe::primitive::Joint_influences none = erhe::primitive::sort_joint_influences(
        glm::vec4{5.0f, 6.0f, 7.0f, 8.0f},
        glm::vec4{0.0f, 0.0f, 0.0f, 0.0f}
    );
    EXPECT_NEAR(none.weights[0], 1.0f, 1e-6f);
    EXPECT_NEAR(none.weights[1], 0.0f, 1e-6f);
}

TEST(AttributeEncodings, implicit_sum_joint_weights_round_trip)
{
    const glm::vec4 cases[] = {
        {0.5f,   0.25f,  0.15f,  0.10f},
        {1.0f,   0.0f,   0.0f,   0.0f },
        {0.25f,  0.25f,  0.25f,  0.25f},
        {0.4f,   0.3f,   0.2f,   0.1f },
        {0.9999f, 0.0001f, 0.0f, 0.0f }
    };
    for (const glm::vec4& weights : cases) {
        const std::array<uint16_t, 3> encoded = erhe::primitive::encode_implicit_sum_joint_weights(weights);
        // The decoder derives the fourth weight from the other three, so the
        // three stored units must never sum past one unit or it would go
        // negative.
        EXPECT_LE(static_cast<int>(encoded[0]) + static_cast<int>(encoded[1]) + static_cast<int>(encoded[2]), 65535);

        const glm::vec4 decoded = decode_implicit_sum_joint_weights(encoded);
        for (glm::length_t i = 0; i < 4; ++i) {
            EXPECT_NEAR(decoded[i], weights[i], 4.0f / 65535.0f) << "component " << i;
        }
        // Skinning is a convex combination: the reconstructed set must still
        // sum to one, or the vertex changes size.
        EXPECT_NEAR(decoded.x + decoded.y + decoded.z + decoded.w, 1.0f, 1e-5f);
    }
}

TEST(AttributeEncodings, texcoord_quantization_is_identity_without_a_range)
{
    // A Buffer_mesh that never recorded a UV range decodes to itself, so an
    // unencoded channel is unaffected by the affine existing at all.
    const erhe::primitive::Buffer_mesh          buffer_mesh;
    const erhe::primitive::Texcoord_quantization quantization = erhe::primitive::get_texcoord_quantization(buffer_mesh);
    EXPECT_EQ(quantization.scale,  glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_EQ(quantization.offset, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
}

TEST(AttributeEncodings, texcoord_affine_round_trips_tiling_uvs)
{
    // Tiling UVs well outside [0, 1] - the case plain unorm16 cannot hold and
    // the whole reason channels 0 and 1 carry an affine.
    erhe::primitive::Buffer_mesh buffer_mesh;
    buffer_mesh.texcoord_ranges[0].add(glm::vec2{-1.0f, -3.0f});
    buffer_mesh.texcoord_ranges[0].add(glm::vec2{ 3.0f,  5.0f});
    buffer_mesh.texcoord_ranges[1].add(glm::vec2{ 0.0f,  0.0f});
    buffer_mesh.texcoord_ranges[1].add(glm::vec2{ 1.0f,  1.0f});

    const erhe::primitive::Texcoord_quantization quantization = erhe::primitive::get_texcoord_quantization(buffer_mesh);
    EXPECT_NEAR(quantization.scale.x,  4.0f, 1e-6f);
    EXPECT_NEAR(quantization.scale.y,  8.0f, 1e-6f);
    EXPECT_NEAR(quantization.offset.x, -1.0f, 1e-6f);
    EXPECT_NEAR(quantization.offset.y, -3.0f, 1e-6f);

    const glm::vec2 scale {quantization.scale.x,  quantization.scale.y};
    const glm::vec2 offset{quantization.offset.x, quantization.offset.y};
    const glm::vec2 inv_scale{1.0f / scale.x, 1.0f / scale.y};

    for (const glm::vec2& uv : {glm::vec2{-1.0f, -3.0f}, glm::vec2{3.0f, 5.0f}, glm::vec2{0.0f, 0.0f}, glm::vec2{2.5f, 1.25f}}) {
        const glm::vec2 normalized = glm::clamp((uv - offset) * inv_scale, glm::vec2{0.0f}, glm::vec2{1.0f});
        const std::array<uint16_t, 2> encoded = {
            static_cast<uint16_t>(normalized.x * 65535.0f + 0.5f),
            static_cast<uint16_t>(normalized.y * 65535.0f + 0.5f)
        };
        const glm::vec2 decoded = decode_texcoord(encoded, scale, offset);
        // One unorm16 step scaled by the range: 4/65535 and 8/65535.
        EXPECT_NEAR(decoded.x, uv.x, 2.0f * scale.x / 65535.0f);
        EXPECT_NEAR(decoded.y, uv.y, 2.0f * scale.y / 65535.0f);
    }
}

TEST(AttributeEncodings, format_encoding_axes_are_derived_from_the_format)
{
    using erhe::dataformat::get_vertex_joint_weights_encoding;
    using erhe::dataformat::get_vertex_tbn_encoding;
    using erhe::dataformat::get_vertex_texcoord_encoding;
    using erhe::dataformat::Vertex_joint_weights_encoding;
    using erhe::dataformat::Vertex_tbn_encoding;
    using erhe::dataformat::Vertex_texcoord_encoding;

    const Vertex_format content{
        Vertex_stream{0, {
            {Format::format_32_vec3_float, Vertex_attribute_usage::normal,        0},
            {Format::format_32_vec4_float, Vertex_attribute_usage::tangent,       0},
            {Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord,     0},
            {Format::format_8_vec4_unorm,  Vertex_attribute_usage::joint_weights, 0}
        }}
    };
    EXPECT_EQ(get_vertex_tbn_encoding(&content),           Vertex_tbn_encoding::passthrough);
    EXPECT_EQ(get_vertex_texcoord_encoding(&content),      Vertex_texcoord_encoding::passthrough);
    EXPECT_EQ(get_vertex_joint_weights_encoding(&content), Vertex_joint_weights_encoding::passthrough);

    const Vertex_format optimized{
        Vertex_stream{0, {
            {Format::format_16_vec4_sint,  Vertex_attribute_usage::tangent,       0},
            {Format::format_16_vec2_unorm, Vertex_attribute_usage::tex_coord,     0},
            {Format::format_16_vec3_unorm, Vertex_attribute_usage::joint_weights, 0}
        }}
    };
    EXPECT_EQ(get_vertex_tbn_encoding(&optimized),           Vertex_tbn_encoding::quaternion16);
    EXPECT_EQ(get_vertex_texcoord_encoding(&optimized),      Vertex_texcoord_encoding::unorm16x2_affine);
    EXPECT_EQ(get_vertex_joint_weights_encoding(&optimized), Vertex_joint_weights_encoding::unorm16x3_implicit_sum);

    // The texcoord axis is keyed on CHANNEL 0, deliberately: channel 2 is the
    // lightmap UV set, is unorm16 without an affine, and must not flip the axis
    // for a format whose channels 0 and 1 are still float.
    const Vertex_format lightmap_only{
        Vertex_stream{0, {
            {Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0},
            {Format::format_16_vec2_unorm, Vertex_attribute_usage::tex_coord, 2}
        }}
    };
    EXPECT_EQ(get_vertex_texcoord_encoding(&lightmap_only), Vertex_texcoord_encoding::passthrough);

    // A null format is passthrough on every axis (the material-identity key
    // derives with no vertex format at all).
    EXPECT_EQ(get_vertex_tbn_encoding(nullptr),           Vertex_tbn_encoding::passthrough);
    EXPECT_EQ(get_vertex_texcoord_encoding(nullptr),      Vertex_texcoord_encoding::passthrough);
    EXPECT_EQ(get_vertex_joint_weights_encoding(nullptr), Vertex_joint_weights_encoding::passthrough);
}

TEST(AttributeEncodings, geometry_path_builds_the_encoded_optimized_variant)
{
    // End to end through take_optimizable_snapshot(): the gather has to convert
    // the staged float attributes into the optimized format's encodings, and
    // DECLINE - build no optimized variant at all - for any pair it does not
    // know. A missing conversion therefore shows up here as a null
    // optimized_render_shape.
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("box");
    erhe::geometry::shapes::make_box(geometry->get_mesh(), -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
    geometry->process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_compute_facet_centroids |
                erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
                erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates |
                erhe::geometry::Geometry::process_flag_generate_tangents
        }
    );

    // One stream, so a single Cpu_vertex_buffer_sink serves it. The attribute
    // set and the substitutions mirror what Mesh_memory derives.
    const Vertex_format content_format{
        Vertex_stream{0, {
            {Format::format_32_vec3_float, Vertex_attribute_usage::position,  0},
            {Format::format_32_vec3_float, Vertex_attribute_usage::normal,    erhe::dataformat::normal_attribute},
            {Format::format_32_vec4_float, Vertex_attribute_usage::tangent,   0},
            {Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0},
            {Format::format_32_vec4_float, Vertex_attribute_usage::color,     0}
        }}
    };
    const Vertex_format optimized_format{
        Vertex_stream{0, {
            {Format::format_16_vec3_snorm, Vertex_attribute_usage::position,  0},
            {Format::format_16_vec4_sint,  Vertex_attribute_usage::tangent,   0},
            {Format::format_16_vec2_unorm, Vertex_attribute_usage::tex_coord, 0},
            {Format::format_8_vec4_unorm,  Vertex_attribute_usage::color,     0}
        }}
    };

    erhe::buffer::Cpu_buffer                vertex_buffer{"test_vertex", 1024 * 1024};
    erhe::buffer::Cpu_buffer                index_buffer {"test_index",  1024 * 1024};
    erhe::primitive::Cpu_vertex_buffer_sink vertex_buffer_sink{{&vertex_buffer}};
    erhe::primitive::Cpu_index_buffer_sink  index_buffer_sink {index_buffer};

    erhe::primitive::Primitive primitive{geometry};
    const erhe::primitive::Build_info build_info{
        .primitive_types = {
            .fill_triangles = true
        },
        .buffer_info = {
            .normal_style               = erhe::primitive::Normal_style::corner_normals,
            .index_type                 = Format::format_32_scalar_uint,
            .vertex_format              = content_format,
            .vertex_buffer_sink         = vertex_buffer_sink,
            .index_buffer_sink          = index_buffer_sink,
            .optimize_meshes            = true,
            .optimized_vertex_format    = &optimized_format,
            .optimized_vertex_input_key = 1
        },
        .normal_style = erhe::primitive::Normal_style::corner_normals
    };
    ASSERT_TRUE(primitive.make_renderable_mesh(build_info, erhe::primitive::Normal_style::corner_normals));
    ASSERT_TRUE(primitive.render_shape);
    // Null here means the gather declined - i.e. some attribute pair has no
    // conversion. That is the failure mode this whole test exists to catch.
    ASSERT_TRUE(primitive.optimized_render_shape);

    const erhe::primitive::Buffer_mesh& optimized_mesh = primitive.optimized_render_shape->get_renderable_mesh();
    ASSERT_FALSE(optimized_mesh.vertex_buffer_ranges.empty());
    const erhe::primitive::Buffer_range& range = optimized_mesh.vertex_buffer_ranges.front();
    // 6 (position) + 8 (TBN) + 4 (texcoord) + 4 (color) = 22. The real formats
    // are repacked to a 4-byte multiple by Mesh_memory; these test formats are
    // not, so the packed size is what is expected here.
    const std::size_t stride = optimized_format.streams.front().stride;
    EXPECT_EQ(stride, 22u);
    EXPECT_EQ(range.element_size, stride);
    ASSERT_GT(range.count, 0u);

    const erhe::dataformat::Attribute_stream tangent_attribute =
        optimized_format.find_attribute(Vertex_attribute_usage::tangent, 0);
    const erhe::dataformat::Attribute_stream texcoord_attribute =
        optimized_format.find_attribute(Vertex_attribute_usage::tex_coord, 0);
    ASSERT_NE(tangent_attribute.attribute,  nullptr);
    ASSERT_NE(texcoord_attribute.attribute, nullptr);
    // The normal is folded into the quaternion, so the optimized format has no
    // normal attribute at all - the property Shader_key::derive() has to work
    // around.
    EXPECT_EQ(optimized_format.find_attribute(Vertex_attribute_usage::normal, erhe::dataformat::normal_attribute).attribute, nullptr);

    const erhe::primitive::Texcoord_quantization quantization = erhe::primitive::get_texcoord_quantization(optimized_mesh);
    const glm::vec2 uv_scale {quantization.scale.x,  quantization.scale.y};
    const glm::vec2 uv_offset{quantization.offset.x, quantization.offset.y};

    // Every decoded normal of a box built with corner normals is an axis unit
    // vector, and every decoded UV lands inside the recorded range. A wrong
    // gather offset or a mismatched decoder shows up as neither.
    for (std::size_t vertex = 0; vertex < range.count; ++vertex) {
        const std::byte* const base = vertex_buffer.get_span().data() + range.byte_offset + vertex * stride;

        std::array<int16_t, 4> tbn{};
        std::memcpy(tbn.data(), base + tangent_attribute.attribute->offset, tbn.size() * sizeof(int16_t));
        glm::vec3 normal{0.0f};
        glm::vec3 tangent{0.0f};
        float     handedness = 0.0f;
        decode_tbn_quaternion(tbn, normal, tangent, handedness);
        EXPECT_NEAR(glm::length(normal), 1.0f, 1e-3f);
        const float largest_component = std::max(std::max(std::abs(normal.x), std::abs(normal.y)), std::abs(normal.z));
        EXPECT_NEAR(largest_component, 1.0f, 1e-2f) << "box corner normal is not axis aligned, vertex " << vertex;
        EXPECT_NEAR(glm::dot(normal, tangent), 0.0f, 1e-3f);
        EXPECT_TRUE((handedness == 1.0f) || (handedness == -1.0f));

        std::array<uint16_t, 2> uv{};
        std::memcpy(uv.data(), base + texcoord_attribute.attribute->offset, uv.size() * sizeof(uint16_t));
        const glm::vec2 decoded_uv = decode_texcoord(uv, uv_scale, uv_offset);
        EXPECT_GE(decoded_uv.x, optimized_mesh.texcoord_ranges[0].min.x - 1e-3f);
        EXPECT_LE(decoded_uv.x, optimized_mesh.texcoord_ranges[0].max.x + 1e-3f);
        EXPECT_GE(decoded_uv.y, optimized_mesh.texcoord_ranges[0].min.y - 1e-3f);
        EXPECT_LE(decoded_uv.y, optimized_mesh.texcoord_ranges[0].max.y + 1e-3f);
    }
}
