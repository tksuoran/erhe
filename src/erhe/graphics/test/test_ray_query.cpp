// Ray query ports of the agfx acceleration structure tests
// (doc/plans/graphics_tests_agfx_port.md phase 8): bottom level structures with
// one and several triangle geometries, top level structures with one and
// several instances, and one bottom level structure shared by several
// instances. Each test traces one ray per texel of a 64x64 orthographic grid
// in a compute shader (Ray_query_test::trace_image: SSBO copied into a
// texture) and checks the silhouette against the CPU model and an image
// golden. Every test skips without Device_info::use_ray_query (OpenGL).

#include "gpu_test_fixture.hpp"

#include "erhe_graphics/acceleration_structure.hpp"
#include "erhe_graphics/command_buffer.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace erhe::graphics::test {

namespace {

// Silhouette: miss -> black; hit -> red 255, green = hit distance t in
// [0, 2] as 0..255, blue = 64 * geometry index + 16 * instance index (the
// instance's position in the top level structure).
constexpr const char* c_silhouette_source = R"glsl(
uint trace_texel(uvec2 texel, vec3 origin, vec3 direction)
{
    rayQueryEXT ray_query;
    rayQueryInitializeEXT(ray_query, s_tlas, gl_RayFlagsOpaqueEXT, RAY_CULL_MASK, origin, 0.0, direction, RAY_T_MAX);
    while (rayQueryProceedEXT(ray_query)) {
    }
    if (rayQueryGetIntersectionTypeEXT(ray_query, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
        return pack_rgba8(0u, 0u, 0u, 255u);
    }
    float t        = rayQueryGetIntersectionTEXT(ray_query, true);
    uint  geometry = uint(rayQueryGetIntersectionGeometryIndexEXT(ray_query, true));
    uint  instance = uint(rayQueryGetIntersectionInstanceIdEXT(ray_query, true));
    return pack_rgba8(255u, encode_unorm8(t * 0.5), (64u * geometry) + (16u * instance), 255u);
}
)glsl";

// A triangle tilted in z, so the hit distance varies across it.
constexpr std::array<glm::vec3, 3> c_tilted_triangle{
    glm::vec3{-0.70f, -0.60f,  0.40f},
    glm::vec3{ 0.80f, -0.45f, -0.30f},
    glm::vec3{-0.15f,  0.75f,  0.10f}
};

// A smaller tilted triangle around the origin, placed by instance transforms.
constexpr std::array<glm::vec3, 3> c_small_triangle{
    glm::vec3{-0.40f, -0.30f,  0.15f},
    glm::vec3{ 0.40f, -0.35f, -0.15f},
    glm::vec3{ 0.05f,  0.40f,  0.00f}
};

// A square of two triangles around the origin.
constexpr std::array<glm::vec3, 4> c_square{
    glm::vec3{-0.30f, -0.30f, 0.0f},
    glm::vec3{ 0.30f, -0.30f, 0.0f},
    glm::vec3{ 0.30f,  0.30f, 0.0f},
    glm::vec3{-0.30f,  0.30f, 0.0f}
};

constexpr std::array<uint32_t, 3> c_triangle_indices{0u, 1u, 2u};
constexpr std::array<uint32_t, 6> c_square_indices  {0u, 1u, 2u, 0u, 2u, 3u};

[[nodiscard]] auto silhouette_id(const uint32_t geometry_index, const uint32_t instance_index) -> uint32_t
{
    return (64u * geometry_index) + (16u * instance_index);
}

// A rotated, scaled and tilted placement: rotate 40 degrees about Z, tilt 35
// degrees about X, scale 0.8, then translate.
[[nodiscard]] auto rotated_scaled(const glm::vec3 translation) -> glm::mat4
{
    glm::mat4 transform = glm::translate(glm::mat4{1.0f}, translation);
    transform = glm::rotate(transform, glm::radians(35.0f), glm::vec3{1.0f, 0.0f, 0.0f});
    transform = glm::rotate(transform, glm::radians(40.0f), glm::vec3{0.0f, 0.0f, 1.0f});
    transform = glm::scale(transform, glm::vec3{0.8f});
    return transform;
}

class Silhouette_expectation
{
public:
    std::vector<uint8_t> expected;
    std::vector<uint8_t> ambiguous;
};

} // namespace

class Ray_query_silhouette_test : public Ray_query_test
{
protected:
    // CPU model of c_silhouette_source over the model triangles, whose id is
    // silhouette_id(geometry, instance).
    [[nodiscard]] auto model_silhouette(const std::span<const Ray_query_model_triangle> triangles) -> Silhouette_expectation
    {
        const std::size_t      texel_count = static_cast<std::size_t>(c_image_size) * static_cast<std::size_t>(c_image_size);
        Silhouette_expectation result{
            .expected  = std::vector<uint8_t>(texel_count * 4u, 0u),
            .ambiguous = std::vector<uint8_t>(texel_count, 0u)
        };
        for (int y = 0; y < c_image_size; ++y) {
            for (int x = 0; x < c_image_size; ++x) {
                const std::size_t         texel = (static_cast<std::size_t>(y) * c_image_size) + static_cast<std::size_t>(x);
                const Ray_query_model_hit hit   = trace_model(triangles, x, y);
                result.ambiguous[texel] = hit.ambiguous ? 1u : 0u;
                uint8_t* rgba = result.expected.data() + (texel * 4u);
                rgba[3] = 255u;
                if (hit.hit) {
                    rgba[0] = 255u;
                    rgba[1] = encode_unorm8(hit.t * 0.5f);
                    rgba[2] = static_cast<uint8_t>(hit.triangle->id);
                }
            }
        }
        return result;
    }

    // One top level structure over instances, built after the bottom level
    // structures blas in the trace's command buffer, traced with the
    // silhouette shader and compared against the model of triangles.
    void trace_and_expect(
        const char* const                                         name,
        const std::span<erhe::graphics::Acceleration_structure* const> blas,
        const std::span<const erhe::graphics::Acceleration_structure_instance> instances,
        const std::span<const Ray_query_model_triangle>           triangles
    )
    {
        erhe::graphics::Acceleration_structure tlas{
            device(),
            erhe::graphics::Acceleration_structure_create_info{
                .type               = erhe::graphics::Acceleration_structure_type::top_level,
                .max_instance_count = static_cast<uint32_t>(instances.size()),
                .debug_label        = erhe::utility::Debug_label{"ray query TLAS"}
            }
        };
        const Ray_query_image image = trace_image(
            name,
            c_silhouette_source,
            {},
            [&](erhe::graphics::Command_buffer& command_buffer) {
                for (erhe::graphics::Acceleration_structure* bottom_level : blas) {
                    bottom_level->build(command_buffer);
                }
                tlas.build(command_buffer, instances);
            },
            tlas
        );
        if (HasFailure()) {
            return;
        }
        const Silhouette_expectation expectation = model_silhouette(triangles);
        expect_image(image, expectation.expected, expectation.ambiguous, 1, name);
    }
};

// agfx RaytraceTriangleOneGeometry: one bottom level structure with one
// triangle geometry (tilted, so the hit distance in green ramps across it),
// one identity instance. Golden ray_query_triangle_one_geometry.png.
TEST_F(Ray_query_silhouette_test, triangle_one_geometry)
{
    const Ray_query_mesh mesh = make_mesh(c_tilted_triangle, c_triangle_indices, "ray query tilted triangle");
    erhe::graphics::Acceleration_structure blas{
        device(),
        erhe::graphics::Acceleration_structure_create_info{
            .type                = erhe::graphics::Acceleration_structure_type::bottom_level,
            .triangle_geometries = { get_triangles(mesh, Ray_query_opacity::opaque) },
            .debug_label         = erhe::utility::Debug_label{"ray query BLAS"}
        }
    };
    const std::array<erhe::graphics::Acceleration_structure*, 1> blas_list{&blas};
    const std::array<erhe::graphics::Acceleration_structure_instance, 1> instances{
        erhe::graphics::Acceleration_structure_instance{ .transform = glm::mat4{1.0f}, .bottom_level = &blas }
    };
    std::vector<Ray_query_model_triangle> triangles;
    append_model_triangles(triangles, c_tilted_triangle, c_triangle_indices, glm::mat4{1.0f}, silhouette_id(0u, 0u));
    trace_and_expect("ray_query_triangle_one_geometry", blas_list, instances, triangles);
}

// agfx RaytraceTriangleMultipleGeometry: one bottom level structure with three
// triangle geometries at z = 0.5, 0.0 and -0.5 (hit distance 0.5, 1.0, 1.5)
// that overlap in xy; each texel shows the nearest, blue names its geometry
// index. Golden ray_query_triangle_multiple_geometry.png.
TEST_F(Ray_query_silhouette_test, triangle_multiple_geometry)
{
    const std::array<std::array<glm::vec3, 3>, 3> geometries{
        std::array<glm::vec3, 3>{ glm::vec3{-0.80f, -0.70f,  0.5f}, glm::vec3{ 0.30f, -0.60f,  0.5f}, glm::vec3{-0.30f,  0.40f,  0.5f} },
        std::array<glm::vec3, 3>{ glm::vec3{-0.40f, -0.30f,  0.0f}, glm::vec3{ 0.70f, -0.35f,  0.0f}, glm::vec3{ 0.10f,  0.70f,  0.0f} },
        std::array<glm::vec3, 3>{ glm::vec3{-0.10f, -0.85f, -0.5f}, glm::vec3{ 0.85f,  0.10f, -0.5f}, glm::vec3{ 0.05f,  0.85f, -0.5f} }
    };
    std::vector<Ray_query_mesh>                                   meshes;
    std::vector<erhe::graphics::Acceleration_structure_triangles> triangle_geometries;
    std::vector<Ray_query_model_triangle>                         triangles;
    for (std::size_t i = 0; i < geometries.size(); ++i) {
        meshes.push_back(make_mesh(geometries[i], c_triangle_indices, "ray query geometry"));
        append_model_triangles(triangles, geometries[i], c_triangle_indices, glm::mat4{1.0f}, silhouette_id(static_cast<uint32_t>(i), 0u));
    }
    for (const Ray_query_mesh& mesh : meshes) {
        triangle_geometries.push_back(get_triangles(mesh, Ray_query_opacity::opaque));
    }
    erhe::graphics::Acceleration_structure blas{
        device(),
        erhe::graphics::Acceleration_structure_create_info{
            .type                = erhe::graphics::Acceleration_structure_type::bottom_level,
            .triangle_geometries = triangle_geometries,
            .debug_label         = erhe::utility::Debug_label{"ray query BLAS"}
        }
    };
    const std::array<erhe::graphics::Acceleration_structure*, 1> blas_list{&blas};
    const std::array<erhe::graphics::Acceleration_structure_instance, 1> instances{
        erhe::graphics::Acceleration_structure_instance{ .transform = glm::mat4{1.0f}, .bottom_level = &blas }
    };
    trace_and_expect("ray_query_triangle_multiple_geometry", blas_list, instances, triangles);
}

// agfx RaytraceTLASOneInstance: one bottom level structure placed by one
// translated instance (0.3, -0.2, -0.25). Golden ray_query_tlas_one_instance.png.
TEST_F(Ray_query_silhouette_test, tlas_one_instance)
{
    const Ray_query_mesh mesh = make_mesh(c_small_triangle, c_triangle_indices, "ray query small triangle");
    erhe::graphics::Acceleration_structure blas{
        device(),
        erhe::graphics::Acceleration_structure_create_info{
            .type                = erhe::graphics::Acceleration_structure_type::bottom_level,
            .triangle_geometries = { get_triangles(mesh, Ray_query_opacity::opaque) },
            .debug_label         = erhe::utility::Debug_label{"ray query BLAS"}
        }
    };
    const glm::mat4 transform = glm::translate(glm::mat4{1.0f}, glm::vec3{0.3f, -0.2f, -0.25f});
    const std::array<erhe::graphics::Acceleration_structure*, 1> blas_list{&blas};
    const std::array<erhe::graphics::Acceleration_structure_instance, 1> instances{
        erhe::graphics::Acceleration_structure_instance{ .transform = transform, .bottom_level = &blas }
    };
    std::vector<Ray_query_model_triangle> triangles;
    append_model_triangles(triangles, c_small_triangle, c_triangle_indices, transform, silhouette_id(0u, 0u));
    trace_and_expect("ray_query_tlas_one_instance", blas_list, instances, triangles);
}

// agfx RaytraceTLASMultipleInstances: two bottom level structures (a
// triangle and a two-triangle square) in three instances: the triangle
// translated to the upper left, the square to the upper right, and the
// triangle again rotated, tilted and scaled below. Blue names the instance.
// Golden ray_query_tlas_multiple_instances.png.
TEST_F(Ray_query_silhouette_test, tlas_multiple_instances)
{
    const Ray_query_mesh triangle_mesh = make_mesh(c_small_triangle, c_triangle_indices, "ray query small triangle");
    const Ray_query_mesh square_mesh   = make_mesh(c_square,         c_square_indices,   "ray query square");
    erhe::graphics::Acceleration_structure triangle_blas{
        device(),
        erhe::graphics::Acceleration_structure_create_info{
            .type                = erhe::graphics::Acceleration_structure_type::bottom_level,
            .triangle_geometries = { get_triangles(triangle_mesh, Ray_query_opacity::opaque) },
            .debug_label         = erhe::utility::Debug_label{"ray query triangle BLAS"}
        }
    };
    erhe::graphics::Acceleration_structure square_blas{
        device(),
        erhe::graphics::Acceleration_structure_create_info{
            .type                = erhe::graphics::Acceleration_structure_type::bottom_level,
            .triangle_geometries = { get_triangles(square_mesh, Ray_query_opacity::opaque) },
            .debug_label         = erhe::utility::Debug_label{"ray query square BLAS"}
        }
    };
    const glm::mat4 transform_0 = glm::translate(glm::mat4{1.0f}, glm::vec3{-0.45f, 0.40f,  0.30f});
    const glm::mat4 transform_1 = glm::translate(glm::mat4{1.0f}, glm::vec3{ 0.46f, 0.43f, -0.20f});
    const glm::mat4 transform_2 = rotated_scaled(glm::vec3{0.05f, -0.45f, 0.0f});
    const std::array<erhe::graphics::Acceleration_structure*, 2> blas_list{&triangle_blas, &square_blas};
    const std::array<erhe::graphics::Acceleration_structure_instance, 3> instances{
        erhe::graphics::Acceleration_structure_instance{ .transform = transform_0, .bottom_level = &triangle_blas },
        erhe::graphics::Acceleration_structure_instance{ .transform = transform_1, .bottom_level = &square_blas   },
        erhe::graphics::Acceleration_structure_instance{ .transform = transform_2, .bottom_level = &triangle_blas }
    };
    std::vector<Ray_query_model_triangle> triangles;
    append_model_triangles(triangles, c_small_triangle, c_triangle_indices, transform_0, silhouette_id(0u, 0u));
    append_model_triangles(triangles, c_square,         c_square_indices,   transform_1, silhouette_id(0u, 1u));
    append_model_triangles(triangles, c_small_triangle, c_triangle_indices, transform_2, silhouette_id(0u, 2u));
    trace_and_expect("ray_query_tlas_multiple_instances", blas_list, instances, triangles);
}

// agfx RaytraceBLASReuseInInstances: one bottom level structure (the square)
// referenced by three instances: two translations at different depths that
// overlap, and a rotated, tilted and scaled one. Where the two overlap the
// nearer (instance 0) wins. Golden ray_query_blas_reuse_in_instances.png.
TEST_F(Ray_query_silhouette_test, blas_reuse_in_instances)
{
    const Ray_query_mesh mesh = make_mesh(c_square, c_square_indices, "ray query square");
    erhe::graphics::Acceleration_structure blas{
        device(),
        erhe::graphics::Acceleration_structure_create_info{
            .type                = erhe::graphics::Acceleration_structure_type::bottom_level,
            .triangle_geometries = { get_triangles(mesh, Ray_query_opacity::opaque) },
            .debug_label         = erhe::utility::Debug_label{"ray query square BLAS"}
        }
    };
    const glm::mat4 transform_0 = glm::translate(glm::mat4{1.0f}, glm::vec3{-0.36f, 0.41f,  0.40f});
    const glm::mat4 transform_1 = glm::translate(glm::mat4{1.0f}, glm::vec3{ 0.05f, 0.50f, -0.10f});
    const glm::mat4 transform_2 = rotated_scaled(glm::vec3{0.0f, -0.45f, -0.30f});
    const std::array<erhe::graphics::Acceleration_structure*, 1> blas_list{&blas};
    const std::array<erhe::graphics::Acceleration_structure_instance, 3> instances{
        erhe::graphics::Acceleration_structure_instance{ .transform = transform_0, .bottom_level = &blas },
        erhe::graphics::Acceleration_structure_instance{ .transform = transform_1, .bottom_level = &blas },
        erhe::graphics::Acceleration_structure_instance{ .transform = transform_2, .bottom_level = &blas }
    };
    std::vector<Ray_query_model_triangle> triangles;
    append_model_triangles(triangles, c_square, c_square_indices, transform_0, silhouette_id(0u, 0u));
    append_model_triangles(triangles, c_square, c_square_indices, transform_1, silhouette_id(0u, 1u));
    append_model_triangles(triangles, c_square, c_square_indices, transform_2, silhouette_id(0u, 2u));
    trace_and_expect("ray_query_blas_reuse_in_instances", blas_list, instances, triangles);
}

} // namespace erhe::graphics::test
