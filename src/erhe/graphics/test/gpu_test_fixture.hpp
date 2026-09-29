#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/acceleration_structure.hpp"
#include "erhe_graphics/enums.hpp"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace erhe::graphics {
    class Bind_group_layout;
    class Buffer;
    class Color_blend_state;
    class Command_buffer;
    class Compute_pipeline;
    class Device;
    class Rasterization_state;
    class Sampler;
    class Shader_resource;
    class Shader_stages;
    class Texture;
}

namespace erhe::graphics::test {

// One combined image sampler bound for render_fullscreen_pass: the texture and
// sampler set with set_sampled_image at binding_point of the pass's layout.
class Sampled_image
{
public:
    uint32_t                       binding_point{0};
    const erhe::graphics::Texture* texture      {nullptr};
    const erhe::graphics::Sampler* sampler      {nullptr};
};

// A compute shader program and its pipeline, built by
// Gpu_test::make_compute_program. The pipeline refers to the shader stages, so
// both live behind stable pointers. Either is null when building failed (the
// helper has already reported the failure).
class Compute_program
{
public:
    Compute_program();
    ~Compute_program() noexcept;
    Compute_program(Compute_program&&) noexcept;
    auto operator=(Compute_program&&) noexcept -> Compute_program&;

    [[nodiscard]] auto is_valid() const -> bool;

    std::unique_ptr<erhe::graphics::Shader_stages>    shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline> pipeline;
};

// Per-test fixture over the process-wide headless Vulkan device. Provides the
// minimal frame sequence and offscreen-readback helpers shared by the GPU
// milestones. SetUp clears the runtime validation-message list; TearDown
// waits the device idle and fails the case for any validation message
// emitted during the test.
class Gpu_test : public ::testing::Test
{
protected:
    void SetUp   () override;
    void TearDown() override;

    [[nodiscard]] auto device() -> erhe::graphics::Device&;

    // Drive one offscreen frame: wait_frame -> get_command_buffer(0) ->
    // begin -> record_fn -> end -> submit -> wait_idle -> end_frame. After
    // it returns all GPU work is complete and any mappable readback buffer
    // is safe to read.
    void submit_and_wait(const std::function<void(erhe::graphics::Command_buffer&)>& record_fn);

    // 2D color render target, usage color_attachment | sampled | transfer_src
    // (transfer_src so read_texture_rgba8 can copy it out). When
    // include_transfer_dst is true the texture additionally gets transfer_dst
    // usage, required for upload_to_texture / clear_texture targets (without it
    // the image lacks VK_IMAGE_USAGE_TRANSFER_DST_BIT and validation rejects the
    // transfer). The fresh texture is pre-transitioned to transfer_src_optimal
    // either way.
    [[nodiscard]] auto make_color_target(
        int                      width,
        int                      height,
        erhe::dataformat::Format format               = erhe::dataformat::Format::format_8_vec4_unorm,
        bool                     include_transfer_dst = false
    ) -> std::shared_ptr<erhe::graphics::Texture>;

    // Host-visible mappable buffer with the requested usage.
    [[nodiscard]] auto make_host_buffer(std::size_t byte_count, erhe::graphics::Buffer_usage usage, const char* debug_label)
        -> std::shared_ptr<erhe::graphics::Buffer>;

    // Host-visible mappable buffer, usage transfer_dst | storage.
    [[nodiscard]] auto make_readback_buffer(std::size_t byte_count, const char* debug_label)
        -> std::shared_ptr<erhe::graphics::Buffer>;

    // Map a host-visible buffer and copy out the first byte_count bytes (or
    // the whole capacity when byte_count == 0). Assumes the GPU is idle.
    [[nodiscard]] auto read_buffer(erhe::graphics::Buffer& buffer, std::size_t byte_count = 0)
        -> std::vector<std::byte>;

    // Copy an RGBA8 texture's level 0 to a mappable buffer and return the
    // bytes, tightly packed at width*4 bytes per row. Records its own blit
    // frame internally, so call it after the frame that produced the texture
    // contents (and with the texture left in transfer_src_optimal).
    [[nodiscard]] auto read_texture_rgba8(const erhe::graphics::Texture& texture)
        -> std::vector<uint8_t>;

    // Copy a texture's level 0 (color aspect) to a mappable buffer and return
    // the raw bytes, tightly packed at width * bytes_per_texel bytes per row.
    // bytes_per_texel must match the texture's pixel format size. Same framing
    // contract as read_texture_rgba8 (texture must be in transfer_src_optimal).
    // The byte-level workhorse the typed color readbacks build on; also used
    // directly when the caller wants to reinterpret texels as signed bytes
    // (snorm8) etc.
    [[nodiscard]] auto read_texture_color_bytes(const erhe::graphics::Texture& texture, std::size_t bytes_per_texel)
        -> std::vector<std::byte>;

    // Copy a single mip level (color aspect) of a texture to a mappable buffer
    // and return the raw bytes, tightly packed at get_width(level) *
    // bytes_per_texel bytes per row. The Blit_command_encoder texture->buffer
    // overload takes source_level + reads the tracked layout, so this works for
    // any level the caller has populated (e.g. after generate_mipmaps, which
    // leaves every level in shader_read_only_optimal). bytes_per_texel must
    // match the texture's pixel format size.
    [[nodiscard]] auto read_texture_level_bytes(const erhe::graphics::Texture& texture, unsigned int level, std::size_t bytes_per_texel)
        -> std::vector<std::byte>;

    // Copy one subresource (color aspect: array layer or cube face `layer`,
    // mip `level`) of a format_8_vec4_unorm texture to a mappable buffer and
    // return the bytes, tightly packed at get_width(level) * 4 bytes per row,
    // row 0 at the device's texture origin. The texture->buffer copy reads and
    // restores the texture's tracked layout, so any populated subresource can
    // be read.
    [[nodiscard]] auto read_subresource_rgba8(const erhe::graphics::Texture& texture, unsigned int layer, unsigned int level)
        -> std::vector<uint8_t>;

    // Upload tightly packed RGBA8 texels (get_width(level) * get_height(level)
    // texels, row 0 at the device's texture origin) over the whole of one
    // subresource through copy_from_buffer (destination_slice = layer,
    // destination_level = level). The copy leaves the texture tracked in
    // shader_read_only_optimal, so the texture needs sampled usage besides
    // transfer_dst.
    void seed_subresource_rgba8(const erhe::graphics::Texture& texture, unsigned int layer, unsigned int level, std::span<const uint8_t> texels);

    // Copy a format_32_vec4_float color texture's level 0 to a mappable buffer
    // and return the texels as floats (4 floats per texel, tightly packed).
    // Same framing contract as read_texture_rgba8.
    [[nodiscard]] auto read_texture_rgba32f(const erhe::graphics::Texture& texture)
        -> std::vector<float>;

    // Copy a depth texture's level 0 (depth aspect) to a mappable buffer and
    // return one 32-bit float depth value per texel, tightly packed at width
    // floats per row. Requires a format_d32_sfloat texture left in
    // transfer_src_optimal (the render pass must set usage_after / layout_after
    // = transfer_src / transfer_src_optimal so the depth aspect is readable).
    // The Blit_command_encoder texture->buffer overload selects the depth aspect
    // for depth formats.
    [[nodiscard]] auto read_texture_depth32f(const erhe::graphics::Texture& texture)
        -> std::vector<float>;

    // Render a fullscreen (oversized) triangle of a constant color into a fresh
    // RGBA8 color target with the given rasterization + blend state, then return
    // the read-back pixels. The color is GLSL injected as the fragment output,
    // e.g. "vec4(0.0, 1.0, 0.0, 1.0)". Shared by the pipeline-state coverage
    // tests (cull, color write mask, blend variants, ...).
    [[nodiscard]] auto draw_fullscreen_triangle(
        const char*                                fragment_color_glsl,
        const erhe::graphics::Rasterization_state& rasterization,
        const erhe::graphics::Color_blend_state&   color_blend,
        std::array<double, 4>                      clear_value,
        int                                        width  = 16,
        int                                        height = 16
    ) -> std::vector<uint8_t>;

    // The first supported, depth-renderable, 32-bit, stencil-free depth format
    // (format_d32_sfloat on the test devices), or format_undefined when the
    // device has none: a depth texture whose depth aspect reads back and
    // samples as one float per texel.
    [[nodiscard]] auto find_depth32f_format() -> erhe::dataformat::Format;

    // format_8_vec4_unorm texture of the given type (texture_2d,
    // texture_2d_array, texture_3d or texture_cube_map; depth only for 3D,
    // array_layer_count 6 for a cube), one level, usage sampled | transfer_dst |
    // transfer_src, left in its initial layout: a sampling source to fill with
    // seed_subresource_rgba8 / copy_from_buffer, which leave it in
    // shader_read_only_optimal.
    [[nodiscard]] auto make_sampled_texture(
        erhe::graphics::Texture_type type,
        int                          width,
        int                          height,
        int                          depth,
        int                          array_layer_count,
        const char*                  debug_label
    ) -> std::shared_ptr<erhe::graphics::Texture>;

    // Build a compute program from one compute shader source against layout.
    // name is also the pipeline's debug name (a string literal: the pipeline
    // keeps the pointer). struct_types and interface_blocks are passed to the
    // shader stages as Shader_stages_create_info documents. On failure the
    // helper adds a test failure and returns a program whose is_valid() is
    // false. extensions name GLSL extensions the shader requires (e.g.
    // "GL_EXT_ray_query"); they are emitted in the preamble ahead of the
    // injected declarations, where an #extension line in compute_source could
    // not go.
    [[nodiscard]] auto make_compute_program(
        const char*                                                name,
        std::string_view                                           compute_source,
        const std::vector<std::pair<std::string, std::string>>&    defines,
        const std::vector<const erhe::graphics::Shader_resource*>& struct_types,
        const std::vector<const erhe::graphics::Shader_resource*>& interface_blocks,
        const erhe::graphics::Bind_group_layout&                   layout,
        const std::vector<std::string>&                            extensions = {}
    ) -> Compute_program;

    // Render one fullscreen (oversized) triangle into a fresh width x height
    // color target of the given format (a make_color_target texture) and return
    // the target, left in transfer_src_optimal for the readback helpers. The
    // fragment shader fragment_source writes out_color; it reads the images
    // through the combined_image_sampler bindings of layout, each bound by
    // set_sampled_image from images (the textures must already be in the layout
    // their binding's sampler_aspect samples from). Besides defines, the shader
    // gets TARGET_WIDTH, TARGET_HEIGHT and IMAGE_POSITION: the fragment's pixel
    // position in image space (row 0 = image top, the row order of the goldens)
    // as a vec2 at the pixel centre, derived from gl_FragCoord and the device's
    // texture_origin. gl_FragCoord itself is in memory rows (row 0 at the
    // device's texture origin). A shader that addresses a texture by
    // IMAGE_POSITION reads texel row r for image row r on every backend; one
    // that addresses a rendered texture of the same size by gl_FragCoord reads
    // the texel that was rendered at that pixel.
    [[nodiscard]] auto render_fullscreen_pass(
        const erhe::graphics::Bind_group_layout&                layout,
        std::string_view                                        fragment_source,
        const std::vector<std::pair<std::string, std::string>>& defines,
        std::span<const Sampled_image>                          images,
        int                                                     width,
        int                                                     height,
        erhe::dataformat::Format                                format = erhe::dataformat::Format::format_8_vec4_unorm
    ) -> std::shared_ptr<erhe::graphics::Texture>;

    // Reorder tightly packed rows between memory order (row 0 at the device's
    // texture origin, as the readback helpers return them) and image order
    // (row 0 = image top). The conversion is its own inverse; bytes_per_row is
    // width * bytes per texel.
    [[nodiscard]] auto memory_rows_to_image_rows(std::span<const uint8_t> rows, std::size_t bytes_per_row, int height)
        -> std::vector<uint8_t>;

    // Compare two tightly packed RGBA8 images of width x height texels, per
    // channel within +-tolerance; on mismatch reports the count of differing
    // texels and the first one (position, actual and expected rgba).
    void expect_rgba8_near(
        std::span<const uint8_t> actual,
        std::span<const uint8_t> expected,
        int                      width,
        int                      height,
        int                      tolerance,
        std::string_view         label
    );

    // Golden assertions (doc/erhe/graphics_test_coverage.md "Golden
    // assertions"). Goldens live in src/erhe/graphics/test/golden/; the run's
    // artifacts (output, golden copy, FLIP error map) go under
    // <results>/artifacts/<suite>.<test>/<name>/ and each assertion is recorded
    // in <results>/results.json. With ERHE_GPU_TEST_UPDATE_GOLDENS=1 both
    // helpers write the golden instead of comparing and the assertion passes
    // with an "updated golden" note. Goldens complement, never replace, a
    // test's analytic assertions: an updated golden records whatever the
    // backend produced.

    // Byte-exact comparison against golden/<name>.bin. A mismatch lists the
    // first differing offsets and the total count of differing bytes.
    void expect_buffer_matches_golden(std::string_view name, std::span<const std::byte> bytes);

    // FLIP comparison against golden/<name>.png (format_8_vec4_unorm, LDR-FLIP)
    // or golden/<name>.pfm (format_32_vec4_float, HDR-FLIP); alpha is dropped.
    // Fails on a size mismatch or when the mean FLIP error exceeds threshold.
    // bytes are texels as read back from a rendered color target
    // (read_texture_rgba8 / read_texture_rgba32f), tightly packed, row 0 at the
    // device's texture origin; the helper normalizes them to top-down rows, so
    // one golden serves every backend. Skips when no PNG writer is built
    // (ERHE_USE_FPNG=OFF).
    void expect_image_matches_golden(
        std::string_view           name,
        int                        width,
        int                        height,
        erhe::dataformat::Format   format,
        std::span<const std::byte> bytes,
        float                      threshold = 0.05f
    );
};

// --- Ray query ports (doc/plans/graphics_tests_agfx_port.md phase 8) ---
//
// Shared by test_ray_query.cpp and test_ray_query_attributes.cpp. A test
// builds bottom and top level acceleration structures over host-visible
// geometry buffers and traces one ray per texel of a 64x64 grid with
// Ray_query_test::trace_image; a CPU model of the same rays
// (trace_model) gives the expected texels.

// Triangle geometry uploaded for a bottom level build: float3 positions and a
// uint32 triangle list, both host-visible with acceleration structure build
// input and shader device address usage.
class Ray_query_mesh
{
public:
    std::shared_ptr<erhe::graphics::Buffer> vertex_buffer;
    std::shared_ptr<erhe::graphics::Buffer> index_buffer;
    std::size_t                             vertex_count{0};
    std::size_t                             index_count {0};
};

// Acceleration_structure_triangles::opaque, spelled out at call sites.
enum class Ray_query_opacity : unsigned int {
    opaque,
    non_opaque
};

// One world-space triangle of the CPU model. primitive_index is the
// triangle's index in its geometry; id is a payload the test chooses (a
// geometry, instance or custom index).
class Ray_query_model_triangle
{
public:
    glm::vec3 p0             {0.0f};
    glm::vec3 p1             {0.0f};
    glm::vec3 p2             {0.0f};
    uint32_t  primitive_index{0};
    uint32_t  id             {0};
};

// Nearest hit of one texel's model ray. barycentrics are the weights of p1
// and p2 (as rayQueryGetIntersectionBarycentricsEXT reports them). ambiguous
// is set when the ray passes within 1e-4 of an edge of a triangle it may hit,
// where the GPU's watertight test and the model may legitimately disagree;
// such texels are not compared.
class Ray_query_model_hit
{
public:
    bool                            hit         {false};
    bool                            ambiguous   {false};
    float                           t           {0.0f};
    glm::vec2                       barycentrics{0.0f};
    const Ray_query_model_triangle* triangle    {nullptr};
};

// What trace_image produced: the SSBO words (one RGBA8 texel each, red in the
// low byte, texel index = y * 64 + x with row 0 = image top) and the 64x64
// format_8_vec4_unorm texture the SSBO was copied into, read back.
class Ray_query_image
{
public:
    std::vector<uint32_t> texels;
    std::vector<uint8_t>  pixels;
};

// Fixture for the ray query ports. SetUp skips the test with "ray query not
// supported by this device" unless Device_info::use_ray_query (never set on
// OpenGL).
class Ray_query_test : public Gpu_test
{
protected:
    static constexpr int   c_image_size   = 64;
    static constexpr float c_ray_origin_z = 1.0f; // rays start at z = 1 ...
    static constexpr float c_ray_t_max    = 2.0f; // ... and travel down -Z up to t = 2

    void SetUp() override;

    // Upload positions and indices into a Ray_query_mesh.
    [[nodiscard]] auto make_mesh(std::span<const glm::vec3> positions, std::span<const uint32_t> indices, const char* debug_label)
        -> Ray_query_mesh;

    // The bottom level geometry description of mesh (format_32_vec3_float
    // positions, no geometry transform).
    [[nodiscard]] static auto get_triangles(const Ray_query_mesh& mesh, Ray_query_opacity opacity)
        -> erhe::graphics::Acceleration_structure_triangles;

    // Trace one ray per texel of the 64x64 grid. The ray of texel (x, y) (y =
    // image row, 0 = top) starts at get_ray_origin(x, y) and points down -Z:
    // an orthographic camera over [-1, 1]^2 with +Y up. trace_source defines
    //   uint trace_texel(uvec2 texel, vec3 origin, vec3 direction)
    // returning the packed RGBA8 texel. The prelude ahead of it declares
    // s_tlas (accelerationStructureEXT), IMAGE_SIZE, RAY_T_MAX, RAY_CULL_MASK
    // (0xFFu unless defines set it), encode_unorm8(float) and
    // pack_rgba8(r, g, b, a). record_builds (may be empty) records the
    // acceleration structure builds into the same command buffer ahead of the
    // dispatch; Acceleration_structure::build ends with the barrier that makes
    // the structure visible to ray queries. The SSBO is then copied into a
    // 64x64 texture (copy_from_buffer after a pixel buffer barrier). The SSBO
    // rows are image rows, and the texture's memory row 0 is its top row on
    // every ray query backend (Vulkan, Metal: texture_origin top_left, which
    // trace_image asserts), so the image golden needs no origin conversion.
    [[nodiscard]] auto trace_image(
        const char*                                                 name,
        std::string_view                                            trace_source,
        const std::vector<std::pair<std::string, std::string>>&     defines,
        const std::function<void(erhe::graphics::Command_buffer&)>& record_builds,
        const erhe::graphics::Acceleration_structure&               tlas
    ) -> Ray_query_image;

    // Append the triangles of positions / indices, transformed by transform,
    // to out with the given id.
    static void append_model_triangles(
        std::vector<Ray_query_model_triangle>& out,
        std::span<const glm::vec3>             positions,
        std::span<const uint32_t>              indices,
        const glm::mat4&                       transform,
        uint32_t                               id
    );

    // World-space origin of the ray of texel (x, y), bit-identical to the
    // shader's.
    [[nodiscard]] static auto get_ray_origin(int x, int y) -> glm::vec3;

    // Nearest hit of the model ray of texel (x, y) among triangles, with t in
    // [0, c_ray_t_max].
    [[nodiscard]] static auto trace_model(std::span<const Ray_query_model_triangle> triangles, int x, int y)
        -> Ray_query_model_hit;

    // uint(clamp(value, 0, 1) * 255 + 0.5), the prelude's encode_unorm8.
    [[nodiscard]] static auto encode_unorm8(float value) -> uint8_t;

    // Compare image against the CPU model: expected holds 64x64 RGBA8 texels
    // in image rows, ambiguous one flag per texel (not compared); channels
    // may differ by tolerance. Also checks that the texture readback equals
    // the SSBO bytes and that at most 3% of the texels are ambiguous, then
    // asserts the image golden golden_name.png.
    void expect_image(
        const Ray_query_image&   image,
        std::span<const uint8_t> expected,
        std::span<const uint8_t> ambiguous,
        int                      tolerance,
        std::string_view         golden_name
    );
};

} // namespace erhe::graphics::test
