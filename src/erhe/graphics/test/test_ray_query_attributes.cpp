// Ray query intersection attribute ports of the agfx ray tracing tests
// (doc/plans/graphics_tests_agfx_port.md phase 8): barycentrics, primitive
// index, instance custom index, opaque and non-opaque geometry with the
// candidate loop, and the instance mask (erhe-only). Each test traces one ray
// per texel of a 64x64 orthographic grid in a compute shader
// (Ray_query_test::trace_image: SSBO copied into a texture) and checks the
// result against the CPU model and an image golden. Every test skips without
// Device_info::use_ray_query (OpenGL).

#include "gpu_test_fixture.hpp"

#include "erhe_graphics/acceleration_structure.hpp"
#include "erhe_graphics/command_buffer.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace erhe::graphics::test {

namespace {

// One trace shader, its output selected by a define. Every variant runs the
// candidate loop with no ray flags: a candidate triangle (reported only for
// non-opaque geometry) is confirmed in the left half of the image (texel x <
// 32) and ignored in the right half. Miss -> black; for a hit, t in [0, 2]
// encodes as g = 0..255 and:
//   OUTPUT_BARYCENTRICS:  (barycentric u, barycentric v, 255)
//   OUTPUT_PRIMITIVE_ID:  (64 * primitive index, g, 255)
//   OUTPUT_CUSTOM_INDEX:  (32 * instance custom index, g, 255)
//   OUTPUT_CONFIRMED:     (0, g, 255) if the loop confirmed a candidate, else
//                         (255, g, 0): committed as opaque geometry.
constexpr const char* c_attribute_source = R"glsl(
uint trace_texel(uvec2 texel, vec3 origin, vec3 direction)
{
    rayQueryEXT ray_query;
    rayQueryInitializeEXT(ray_query, s_tlas, gl_RayFlagsNoneEXT, RAY_CULL_MASK, origin, 0.0, direction, RAY_T_MAX);
    bool confirmed = false;
    while (rayQueryProceedEXT(ray_query)) {
        if (rayQueryGetIntersectionTypeEXT(ray_query, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
            if (texel.x < uint(IMAGE_SIZE / 2)) {
                rayQueryConfirmIntersectionEXT(ray_query);
                confirmed = true;
            }
        }
    }
    if (rayQueryGetIntersectionTypeEXT(ray_query, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
        return pack_rgba8(0u, 0u, 0u, 255u);
    }
    uint g = encode_unorm8(rayQueryGetIntersectionTEXT(ray_query, true) * 0.5);
#if defined(OUTPUT_BARYCENTRICS)
    vec2 barycentrics = rayQueryGetIntersectionBarycentricsEXT(ray_query, true);
    return pack_rgba8(encode_unorm8(barycentrics.x), encode_unorm8(barycentrics.y), 255u, 255u);
#elif defined(OUTPUT_PRIMITIVE_ID)
    return pack_rgba8(64u * uint(rayQueryGetIntersectionPrimitiveIndexEXT(ray_query, true)), g, 255u, 255u);
#elif defined(OUTPUT_CUSTOM_INDEX)
    return pack_rgba8(32u * uint(rayQueryGetIntersectionInstanceCustomIndexEXT(ray_query, true)), g, 255u, 255u);
#elif defined(OUTPUT_CONFIRMED)
    return confirmed ? pack_rgba8(0u, g, 255u, 255u) : pack_rgba8(255u, g, 0u, 255u);
#else
#error "no OUTPUT_* define"
#endif
}
)glsl";

enum class Attribute_output : unsigned int {
    barycentrics,
    primitive_id,
    custom_index,
    confirmed
};

[[nodiscard]] auto get_output_define(const Attribute_output output) -> const char*
{
    switch (output) {
        case Attribute_output::barycentrics: return "OUTPUT_BARYCENTRICS";
        case Attribute_output::primitive_id: return "OUTPUT_PRIMITIVE_ID";
        case Attribute_output::custom_index: return "OUTPUT_CUSTOM_INDEX";
        case Attribute_output::confirmed:    return "OUTPUT_CONFIRMED";
        default:                             return "OUTPUT_NONE";
    }
}

constexpr std::array<uint32_t, 3> c_triangle_indices{0u, 1u, 2u};

// A large triangle at z = 0.
constexpr std::array<glm::vec3, 3> c_large_triangle{
    glm::vec3{-0.80f, -0.70f, 0.0f},
    glm::vec3{ 0.85f, -0.55f, 0.0f},
    glm::vec3{-0.10f,  0.80f, 0.0f}
};

// A square split into four triangles around an off-centre raised apex, so the
// diagonals miss the texel centres.
constexpr std::array<glm::vec3, 5> c_fan_positions{
    glm::vec3{ 0.07f,  0.04f,  0.3f}, // apex
    glm::vec3{-0.80f, -0.80f,  0.0f},
    glm::vec3{ 0.80f, -0.80f,  0.0f},
    glm::vec3{ 0.80f,  0.80f,  0.0f},
    glm::vec3{-0.80f,  0.80f,  0.0f}
};
constexpr std::array<uint32_t, 12> c_fan_indices{
    0u, 1u, 2u,
    0u, 2u, 3u,
    0u, 3u, 4u,
    0u, 4u, 1u
};

// A small tilted triangle, placed by instance transforms.
constexpr std::array<glm::vec3, 3> c_small_triangle{
    glm::vec3{-0.40f, -0.30f,  0.15f},
    glm::vec3{ 0.40f, -0.35f, -0.15f},
    glm::vec3{ 0.05f,  0.40f,  0.00f}
};

// A square of two triangles around the origin.
constexpr std::array<glm::vec3, 4> c_square{
    glm::vec3{-0.45f, -0.45f, 0.0f},
    glm::vec3{ 0.45f, -0.45f, 0.0f},
    glm::vec3{ 0.45f,  0.45f, 0.0f},
    glm::vec3{-0.45f,  0.45f, 0.0f}
};
constexpr std::array<uint32_t, 6> c_square_indices{0u, 1u, 2u, 0u, 2u, 3u};

// Opacity scene: a front triangle at z = 0.5 (geometry 0) over a back
// rectangle at z = -0.5 (geometry 1) covering the lower part of the image.
constexpr std::array<glm::vec3, 3> c_front_triangle{
    glm::vec3{-0.75f, -0.60f, 0.5f},
    glm::vec3{ 0.70f, -0.50f, 0.5f},
    glm::vec3{-0.03f,  0.85f, 0.5f}
};
constexpr std::array<glm::vec3, 4> c_back_rectangle{
    glm::vec3{-0.90f, -0.85f, -0.5f},
    glm::vec3{ 0.90f, -0.85f, -0.5f},
    glm::vec3{ 0.90f,  0.13f, -0.5f},
    glm::vec3{-0.90f,  0.13f, -0.5f}
};

class Expectation
{
public:
    std::vector<uint8_t> expected;
    std::vector<uint8_t> ambiguous;
};

} // namespace

class Ray_query_attribute_test : public Ray_query_test
{
protected:
    [[nodiscard]] auto make_blas(const std::vector<erhe::graphics::Acceleration_structure_triangles>& geometries)
        -> erhe::graphics::Acceleration_structure
    {
        return erhe::graphics::Acceleration_structure{
            device(),
            erhe::graphics::Acceleration_structure_create_info{
                .type                = erhe::graphics::Acceleration_structure_type::bottom_level,
                .triangle_geometries = geometries,
                .debug_label         = erhe::utility::Debug_label{"ray query BLAS"}
            }
        };
    }

    [[nodiscard]] auto make_tlas(const std::size_t instance_count) -> erhe::graphics::Acceleration_structure
    {
        return erhe::graphics::Acceleration_structure{
            device(),
            erhe::graphics::Acceleration_structure_create_info{
                .type               = erhe::graphics::Acceleration_structure_type::top_level,
                .max_instance_count = static_cast<uint32_t>(instance_count),
                .debug_label        = erhe::utility::Debug_label{"ray query TLAS"}
            }
        };
    }

    // Trace c_attribute_source with output and extra defines.
    [[nodiscard]] auto trace(
        const char* const                                           name,
        const Attribute_output                                      output,
        const std::vector<std::pair<std::string, std::string>>&     extra_defines,
        const std::function<void(erhe::graphics::Command_buffer&)>& record_builds,
        const erhe::graphics::Acceleration_structure&               tlas
    ) -> Ray_query_image
    {
        std::vector<std::pair<std::string, std::string>> defines = extra_defines;
        defines.emplace_back(get_output_define(output), "1");
        return trace_image(name, c_attribute_source, defines, record_builds, tlas);
    }

    // Build the expected image: texel_fn(x, y, rgba) fills one texel (rgba
    // starts as opaque black) and returns whether the texel is ambiguous.
    [[nodiscard]] auto model_image(const std::function<bool(int, int, uint8_t*)>& texel_fn) -> Expectation
    {
        const std::size_t texel_count = static_cast<std::size_t>(c_image_size) * static_cast<std::size_t>(c_image_size);
        Expectation result{
            .expected  = std::vector<uint8_t>(texel_count * 4u, 0u),
            .ambiguous = std::vector<uint8_t>(texel_count, 0u)
        };
        for (int y = 0; y < c_image_size; ++y) {
            for (int x = 0; x < c_image_size; ++x) {
                const std::size_t texel = (static_cast<std::size_t>(y) * c_image_size) + static_cast<std::size_t>(x);
                uint8_t* rgba = result.expected.data() + (texel * 4u);
                rgba[3] = 255u;
                result.ambiguous[texel] = texel_fn(x, y, rgba) ? 1u : 0u;
            }
        }
        return result;
    }

    // Opacity scene with the front triangle's opacity as given; both tests
    // share the model: a candidate is confirmed only in the left half.
    void run_opacity_test(const char* const name, const Ray_query_opacity front_opacity)
    {
        const Ray_query_mesh front_mesh = make_mesh(c_front_triangle, c_triangle_indices, "ray query front triangle");
        const Ray_query_mesh back_mesh  = make_mesh(c_back_rectangle, c_square_indices,   "ray query back rectangle");
        erhe::graphics::Acceleration_structure blas = make_blas(
            {
                get_triangles(front_mesh, front_opacity),
                get_triangles(back_mesh,  Ray_query_opacity::opaque)
            }
        );
        erhe::graphics::Acceleration_structure tlas = make_tlas(1);
        const std::array<erhe::graphics::Acceleration_structure_instance, 1> instances{
            erhe::graphics::Acceleration_structure_instance{ .transform = glm::mat4{1.0f}, .bottom_level = &blas }
        };
        const Ray_query_image image = trace(
            name, Attribute_output::confirmed, {},
            [&](erhe::graphics::Command_buffer& command_buffer) {
                blas.build(command_buffer);
                tlas.build(command_buffer, instances);
            },
            tlas
        );
        if (HasFailure()) {
            return;
        }

        std::vector<Ray_query_model_triangle> all_triangles;
        append_model_triangles(all_triangles, c_front_triangle, c_triangle_indices, glm::mat4{1.0f}, 0u);
        append_model_triangles(all_triangles, c_back_rectangle, c_square_indices,   glm::mat4{1.0f}, 1u);
        std::vector<Ray_query_model_triangle> back_triangles;
        append_model_triangles(back_triangles, c_back_rectangle, c_square_indices, glm::mat4{1.0f}, 1u);

        const bool front_is_opaque = (front_opacity == Ray_query_opacity::opaque);
        const Expectation expectation = model_image(
            [&](const int x, const int y, uint8_t* rgba) -> bool {
                // A non-opaque front triangle is confirmed in the left half and
                // ignored (the ray continues to the back rectangle) in the
                // right half; an opaque one always commits.
                const bool                accept_front = front_is_opaque || (x < (c_image_size / 2));
                const Ray_query_model_hit hit          = accept_front ? trace_model(all_triangles, x, y) : trace_model(back_triangles, x, y);
                if (hit.hit) {
                    const bool confirmed = !front_is_opaque && (hit.triangle->id == 0u);
                    rgba[0] = confirmed ? 0u : 255u;
                    rgba[1] = encode_unorm8(hit.t * 0.5f);
                    rgba[2] = confirmed ? 255u : 0u;
                }
                return hit.ambiguous;
            }
        );
        expect_image(image, expectation.expected, expectation.ambiguous, 1, name);
    }
};

// agfx RTRayBarycentrics: one triangle, the committed hit's barycentrics as
// red (weight of vertex 1) and green (weight of vertex 2), blue 255 on hits.
// Golden ray_query_barycentrics.png.
TEST_F(Ray_query_attribute_test, barycentrics)
{
    const Ray_query_mesh mesh = make_mesh(c_large_triangle, c_triangle_indices, "ray query large triangle");
    erhe::graphics::Acceleration_structure blas = make_blas({ get_triangles(mesh, Ray_query_opacity::opaque) });
    erhe::graphics::Acceleration_structure tlas = make_tlas(1);
    const std::array<erhe::graphics::Acceleration_structure_instance, 1> instances{
        erhe::graphics::Acceleration_structure_instance{ .transform = glm::mat4{1.0f}, .bottom_level = &blas }
    };
    const Ray_query_image image = trace(
        "ray_query_barycentrics", Attribute_output::barycentrics, {},
        [&](erhe::graphics::Command_buffer& command_buffer) {
            blas.build(command_buffer);
            tlas.build(command_buffer, instances);
        },
        tlas
    );
    if (HasFailure()) {
        return;
    }
    std::vector<Ray_query_model_triangle> triangles;
    append_model_triangles(triangles, c_large_triangle, c_triangle_indices, glm::mat4{1.0f}, 0u);
    const Expectation expectation = model_image(
        [&](const int x, const int y, uint8_t* rgba) -> bool {
            const Ray_query_model_hit hit = trace_model(triangles, x, y);
            if (hit.hit) {
                rgba[0] = encode_unorm8(hit.barycentrics.x);
                rgba[1] = encode_unorm8(hit.barycentrics.y);
                rgba[2] = 255u;
            }
            return hit.ambiguous;
        }
    );
    expect_image(image, expectation.expected, expectation.ambiguous, 1, "ray_query_barycentrics");
}

// agfx RTRayPrimitiveID: one geometry of four triangles fanned around a raised
// apex; red = 64 * primitive index (0, 64, 128, 192), green = hit distance.
// Golden ray_query_primitive_id.png.
TEST_F(Ray_query_attribute_test, primitive_id)
{
    const Ray_query_mesh mesh = make_mesh(c_fan_positions, c_fan_indices, "ray query fan");
    erhe::graphics::Acceleration_structure blas = make_blas({ get_triangles(mesh, Ray_query_opacity::opaque) });
    erhe::graphics::Acceleration_structure tlas = make_tlas(1);
    const std::array<erhe::graphics::Acceleration_structure_instance, 1> instances{
        erhe::graphics::Acceleration_structure_instance{ .transform = glm::mat4{1.0f}, .bottom_level = &blas }
    };
    const Ray_query_image image = trace(
        "ray_query_primitive_id", Attribute_output::primitive_id, {},
        [&](erhe::graphics::Command_buffer& command_buffer) {
            blas.build(command_buffer);
            tlas.build(command_buffer, instances);
        },
        tlas
    );
    if (HasFailure()) {
        return;
    }
    std::vector<Ray_query_model_triangle> triangles;
    append_model_triangles(triangles, c_fan_positions, c_fan_indices, glm::mat4{1.0f}, 0u);
    const Expectation expectation = model_image(
        [&](const int x, const int y, uint8_t* rgba) -> bool {
            const Ray_query_model_hit hit = trace_model(triangles, x, y);
            if (hit.hit) {
                rgba[0] = static_cast<uint8_t>(64u * hit.triangle->primitive_index);
                rgba[1] = encode_unorm8(hit.t * 0.5f);
                rgba[2] = 255u;
            }
            return hit.ambiguous;
        }
    );
    expect_image(image, expectation.expected, expectation.ambiguous, 1, "ray_query_primitive_id");
}

// agfx RTRayUserID: one bottom level structure in three instances with
// instance_custom_index 3, 5 and 6; red = 32 * custom index (96, 160, 192).
// Golden ray_query_user_id.png.
TEST_F(Ray_query_attribute_test, user_id)
{
    const Ray_query_mesh mesh = make_mesh(c_small_triangle, c_triangle_indices, "ray query small triangle");
    erhe::graphics::Acceleration_structure blas = make_blas({ get_triangles(mesh, Ray_query_opacity::opaque) });
    erhe::graphics::Acceleration_structure tlas = make_tlas(3);
    const std::array<glm::mat4, 3> transforms{
        glm::translate(glm::mat4{1.0f}, glm::vec3{-0.45f,  0.45f,  0.30f}),
        glm::translate(glm::mat4{1.0f}, glm::vec3{ 0.45f,  0.40f,  0.00f}),
        glm::translate(glm::mat4{1.0f}, glm::vec3{ 0.02f, -0.45f, -0.30f})
    };
    const std::array<uint32_t, 3> custom_indices{3u, 5u, 6u};
    std::array<erhe::graphics::Acceleration_structure_instance, 3> instances{};
    std::vector<Ray_query_model_triangle> triangles;
    for (std::size_t i = 0; i < instances.size(); ++i) {
        instances[i] = erhe::graphics::Acceleration_structure_instance{
            .transform             = transforms[i],
            .instance_custom_index = custom_indices[i],
            .bottom_level          = &blas
        };
        append_model_triangles(triangles, c_small_triangle, c_triangle_indices, transforms[i], custom_indices[i]);
    }
    const Ray_query_image image = trace(
        "ray_query_user_id", Attribute_output::custom_index, {},
        [&](erhe::graphics::Command_buffer& command_buffer) {
            blas.build(command_buffer);
            tlas.build(command_buffer, instances);
        },
        tlas
    );
    if (HasFailure()) {
        return;
    }
    const Expectation expectation = model_image(
        [&](const int x, const int y, uint8_t* rgba) -> bool {
            const Ray_query_model_hit hit = trace_model(triangles, x, y);
            if (hit.hit) {
                rgba[0] = static_cast<uint8_t>(32u * hit.triangle->id);
                rgba[1] = encode_unorm8(hit.t * 0.5f);
                rgba[2] = 255u;
            }
            return hit.ambiguous;
        }
    );
    expect_image(image, expectation.expected, expectation.ambiguous, 1, "ray_query_user_id");
}

// agfx RTOpaqueGeometry: the opacity scene with both geometries opaque. The
// query reports no candidates, so the loop never confirms: every hit is
// committed as opaque (red), even in the left half where the loop would
// confirm a candidate. Golden ray_query_opaque_geometry.png.
TEST_F(Ray_query_attribute_test, opaque_geometry)
{
    run_opacity_test("ray_query_opaque_geometry", Ray_query_opacity::opaque);
}

// agfx RTRayNonOpaqueCandidate: the opacity scene with the front triangle
// non-opaque (Acceleration_structure_triangles::opaque = false). Its hits come
// to the loop as candidates (rayQueryGetIntersectionTypeEXT(q, false) ==
// gl_RayQueryCandidateIntersectionTriangleEXT): confirmed with
// rayQueryConfirmIntersectionEXT in the left half (blue), ignored in the right
// half, where the ray continues to the opaque back rectangle (red, farther).
// Golden ray_query_non_opaque_candidate.png.
TEST_F(Ray_query_attribute_test, non_opaque_candidate)
{
    run_opacity_test("ray_query_non_opaque_candidate", Ray_query_opacity::non_opaque);
}

// erhe-only (agfx has no instance mask test): two instances of one square with
// Acceleration_structure_instance::mask 0x01 (custom index 4, nearer) and 0x02
// (custom index 7, farther), overlapping in the middle. Traced three times
// with ray cull mask 0x01, 0x02 and 0x03: the first two see only their own
// instance, the third sees both, the nearer one in the overlap. Red = 32 *
// custom index. Goldens ray_query_instance_mask_01.png, _02.png, _03.png.
TEST_F(Ray_query_attribute_test, instance_mask)
{
    const Ray_query_mesh mesh = make_mesh(c_square, c_square_indices, "ray query square");
    erhe::graphics::Acceleration_structure blas = make_blas({ get_triangles(mesh, Ray_query_opacity::opaque) });
    erhe::graphics::Acceleration_structure tlas = make_tlas(2);
    const std::array<glm::mat4, 2> transforms{
        glm::translate(glm::mat4{1.0f}, glm::vec3{-0.26f,  0.21f,  0.30f}),
        glm::translate(glm::mat4{1.0f}, glm::vec3{ 0.27f, -0.24f, -0.30f})
    };
    const std::array<uint32_t, 2> masks         {0x01u, 0x02u};
    const std::array<uint32_t, 2> custom_indices{4u, 7u};
    std::array<erhe::graphics::Acceleration_structure_instance, 2> instances{};
    for (std::size_t i = 0; i < instances.size(); ++i) {
        instances[i] = erhe::graphics::Acceleration_structure_instance{
            .transform             = transforms[i],
            .instance_custom_index = custom_indices[i],
            .mask                  = masks[i],
            .bottom_level          = &blas
        };
    }

    const std::array<uint32_t, 3>    cull_masks  {0x01u, 0x02u, 0x03u};
    const std::array<const char*, 3> golden_names{"ray_query_instance_mask_01", "ray_query_instance_mask_02", "ray_query_instance_mask_03"};
    for (std::size_t pass = 0; pass < cull_masks.size(); ++pass) {
        const uint32_t    cull_mask = cull_masks[pass];
        const char* const name      = golden_names[pass];
        // The structures are built by the first trace's command buffer; later
        // traces reuse them.
        const std::function<void(erhe::graphics::Command_buffer&)> record_builds = (pass == 0)
            ? std::function<void(erhe::graphics::Command_buffer&)>{
                [&](erhe::graphics::Command_buffer& command_buffer) {
                    blas.build(command_buffer);
                    tlas.build(command_buffer, instances);
                }
            }
            : std::function<void(erhe::graphics::Command_buffer&)>{};
        const Ray_query_image image = trace(
            name, Attribute_output::custom_index, { { "RAY_CULL_MASK", std::to_string(cull_mask) + "u" } }, record_builds, tlas
        );
        if (HasFailure()) {
            return;
        }

        std::vector<Ray_query_model_triangle> triangles;
        for (std::size_t i = 0; i < instances.size(); ++i) {
            if ((masks[i] & cull_mask) != 0u) {
                append_model_triangles(triangles, c_square, c_square_indices, transforms[i], custom_indices[i]);
            }
        }
        const Expectation expectation = model_image(
            [&](const int x, const int y, uint8_t* rgba) -> bool {
                const Ray_query_model_hit hit = trace_model(triangles, x, y);
                if (hit.hit) {
                    rgba[0] = static_cast<uint8_t>(32u * hit.triangle->id);
                    rgba[1] = encode_unorm8(hit.t * 0.5f);
                    rgba[2] = 255u;
                }
                return hit.ambiguous;
            }
        );
        expect_image(image, expectation.expected, expectation.ambiguous, 1, name);
    }
}

} // namespace erhe::graphics::test
