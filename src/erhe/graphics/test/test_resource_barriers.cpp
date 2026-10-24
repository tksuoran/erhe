#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/fragment_output.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/texture.hpp"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Command_buffer::texture_barrier / buffer_barrier (doc/erhe/graphics.md
// "Resource barriers"). Each case records a slow producer (every invocation
// or fragment first spins on a loop whose length comes from a uniform block)
// and a fast consumer of one resource with the explicit barrier between them,
// and asserts the consumer saw the producer's result. Texture consumers are
// compute probes (texelFetch at a 64 x 64 grid of texels, the last texel of
// each grid cell) that start at once when nothing orders them. On Metal 4,
// which orders nothing it is not told to, only the explicit barrier orders
// the work (doc/erhe/metal4_backend.md "Encoders and barriers").

namespace erhe::graphics::test {

namespace {

using Image_usage  = erhe::graphics::Image_usage_flag_bit_mask;
using Buffer_usage = erhe::graphics::Buffer_usage_flag_bit_mask;

constexpr int         c_heavy_size     = 1024;  // producer render / storage targets
constexpr int         c_transfer_size  = 2048;  // copy_from_buffer destination
constexpr int         c_probe_grid     = 64;
constexpr std::size_t c_probe_count    = static_cast<std::size_t>(c_probe_grid) * static_cast<std::size_t>(c_probe_grid);
constexpr uint32_t    c_sentinel       = 0xffffffffu;
constexpr uint32_t    c_fragment_spin  = 512u;
constexpr uint32_t    c_vertex_spin    = 8192u;
constexpr uint32_t    c_indirect_spin  = 1u << 22;
constexpr uint32_t    c_element_spin   = 4096u;
constexpr int         c_grid_cells     = 64;     // vertex writer: 64 x 64 quads
constexpr std::size_t c_vertex_count   = static_cast<std::size_t>(c_grid_cells) * static_cast<std::size_t>(c_grid_cells) * 6u;
constexpr std::size_t c_vertex_bytes   = c_vertex_count * 2u * sizeof(float);
constexpr int         c_draw_size      = 64;
constexpr uint32_t    c_element_count  = 4096u;

// The spin loop of the shaders, bit-identical on the CPU.
[[nodiscard]] auto cpu_spin(const uint32_t seed, const uint32_t count) -> uint32_t
{
    uint32_t h = seed;
    for (uint32_t i = 0; i < count; ++i) {
        h = (h * 1664525u) + 1013904223u;
    }
    return h;
}

constexpr const char* c_spin_glsl = R"glsl(
uint spin(uint seed)
{
    uint h = seed;
    for (uint i = 0u; i < Spin.count; ++i) {
        h = (h * 1664525u) + 1013904223u;
    }
    return h;
}
)glsl";

constexpr const char* c_fullscreen_vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl";

// Texel (x, y) of the slow color producers: red / green from the position,
// blue 64 unless the spin of the texel hit the sentinel.
constexpr const char* c_pattern_fragment_body = R"glsl(
void main()
{
    uvec2 p = uvec2(floor(gl_FragCoord.xy));
    uint  h = spin(p.x + (p.y * uint(TARGET_SIZE)));
    float b = (h == Spin.sentinel) ? 0.0 : (64.0 / 255.0);
    out_color = vec4(float(p.x & 255u) / 255.0, float(p.y & 255u) / 255.0, b, 1.0);
}
)glsl";

// The slow depth producer: depth (x + y * size) / 2^20, exact in float.
constexpr const char* c_depth_fragment_body = R"glsl(
void main()
{
    uvec2 p = uvec2(floor(gl_FragCoord.xy));
    uint  h = spin(p.x + (p.y * uint(TARGET_SIZE)));
    gl_FragDepth = (h == Spin.sentinel) ? 1.0 : (float(p.x + (p.y * uint(TARGET_SIZE))) / 1048576.0);
}
)glsl";

constexpr const char* c_storage_image_writer_body = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
    uvec2 p = gl_GlobalInvocationID.xy;
    uint  h = spin(p.x + (p.y * uint(TARGET_SIZE)));
    float b = (h == Spin.sentinel) ? 0.0 : (64.0 / 255.0);
    imageStore(i_image, ivec2(p), vec4(float(p.x & 255u) / 255.0, float(p.y & 255u) / 255.0, b, 1.0));
}
)glsl";

constexpr const char* c_probe_source = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
    uvec2 g = gl_GlobalInvocationID.xy;
    ivec2 p = (ivec2(g) * PROBE_STRIDE) + ivec2(PROBE_STRIDE - 1);
    vec4  v = FETCH(p);
    Probe.data[(g.y * uint(PROBE_GRID)) + g.x] = ENCODE(v);
}
)glsl";

// 64 x 64 quads covering the viewport, as c_vertex_count vec2 positions.
constexpr const char* c_vertex_writer_body = R"glsl(
layout(local_size_x = 64) in;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(VERTEX_COUNT)) {
        return;
    }
    uint  cell    = i / 6u;
    uint  corner  = i % 6u;
    uvec2 c       = uvec2(cell % uint(GRID_CELLS), cell / uint(GRID_CELLS));
    uvec2 corners[6] = uvec2[6](uvec2(0u, 0u), uvec2(1u, 0u), uvec2(1u, 1u), uvec2(0u, 0u), uvec2(1u, 1u), uvec2(0u, 1u));
    vec2  position = ((vec2(c + corners[corner]) / float(GRID_CELLS)) * 2.0) - 1.0;
    if (spin(i) == Spin.sentinel) {
        position = vec2(100.0);
    }
    Dst.data[(2u * i) + 0u] = position.x;
    Dst.data[(2u * i) + 1u] = position.y;
}
)glsl";

// One indexed draw command (6 indices) after a long single-thread spin.
constexpr const char* c_indirect_writer_body = R"glsl(
layout(local_size_x = 1) in;
void main()
{
    uint index_count = (spin(1u) == Spin.sentinel) ? 0u : 6u;
    Dst.data[0] = index_count;
    Dst.data[1] = 1u;
    Dst.data[2] = 0u;
    Dst.data[3] = 0u;
    Dst.data[4] = 0u;
}
)glsl";

// Element i = 3 i + 1 after the spin of i (0 when the spin hit the sentinel).
constexpr const char* c_element_writer_body = R"glsl(
layout(local_size_x = 64) in;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(ELEMENT_COUNT)) {
        return;
    }
    Dst.data[i] = (spin(i) == Spin.sentinel) ? 0u : ((3u * i) + 1u);
}
)glsl";

constexpr const char* c_element_reader_body = R"glsl(
layout(local_size_x = 64) in;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(ELEMENT_COUNT)) {
        return;
    }
    Dst.data[i] = Src.data[i] + 1u;
}
)glsl";

constexpr const char* c_position_vertex_source = R"glsl(
void main()
{
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)glsl";

constexpr const char* c_green_fragment_source = R"glsl(
void main()
{
    out_color = vec4(0.0, 1.0, 0.0, 1.0);
}
)glsl";

// One point per probe texel: the vertex stage fetches the texel and the
// point lands on pixel (gx, gy) of a PROBE_GRID x PROBE_GRID target.
constexpr const char* c_vertex_fetch_vertex_source = R"glsl(
out gl_PerVertex {
    vec4  gl_Position;
    float gl_PointSize;
};
layout(location = 0) flat out vec4 v_color;
float pixel_center_ndc(float pixel)
{
    return ((pixel + 0.5) / float(PROBE_GRID)) * 2.0 - 1.0;
}
void main()
{
    int   gx = gl_VertexID % PROBE_GRID;
    int   gy = gl_VertexID / PROBE_GRID;
    ivec2 p  = (ivec2(gx, gy) * PROBE_STRIDE) + ivec2(PROBE_STRIDE - 1);
#if FETCH_DEPTH
    // The depth value's bits, exact through the RGBA8 target.
    v_color      = unpackUnorm4x8(floatBitsToUint(texelFetch(s_source, p, 0).r));
#else
    v_color      = texelFetch(s_source, p, 0);
#endif
    gl_Position  = vec4(pixel_center_ndc(float(gx)), float(Y_SIGN) * pixel_center_ndc(float(gy)), 0.0, 1.0);
    gl_PointSize = 1.0;
}
)glsl";

constexpr const char* c_vertex_fetch_fragment_source = R"glsl(
layout(location = 0) flat in vec4 v_color;
void main()
{
    out_color = v_color;
}
)glsl";

[[nodiscard]] auto with_spin(const char* body) -> std::string
{
    return std::string{c_spin_glsl} + body;
}

[[nodiscard]] auto pack_rgba8(const uint32_t r, const uint32_t g, const uint32_t b, const uint32_t a) -> uint32_t
{
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

enum class Probe_kind : unsigned int { color_2d, color_2d_array_layer_1, depth_2d };

// The element type of the Dst storage block of the compute layout.
enum class Dst_data : unsigned int { float_data, uint_data };

enum class Slow_target : unsigned int { color, depth };
// What the vertex-stage fetch samples: a color texture (the texel) or a
// depth texture (the depth value's bits).
enum class Fetch_source : unsigned int { color, depth };

// The first dispatch of the in-encoder cases' consumer encoder.
enum class Barrier_scope_case : unsigned int { writer, unrelated };

} // namespace

class Resource_barrier_test : public Gpu_test
{
protected:
    // Binding points shared by every layout of these cases.
    static constexpr uint32_t c_spin_binding   = 0;
    static constexpr uint32_t c_src_binding    = 1;
    static constexpr uint32_t c_dst_binding    = 2;
    static constexpr uint32_t c_image_binding  = 3;
    static constexpr std::size_t c_spin_bytes  = 16;

    class Probe
    {
    public:
        std::unique_ptr<erhe::graphics::Bind_group_layout> layout;
        std::unique_ptr<erhe::graphics::Sampler>           sampler;
        Compute_program                                    program;
        std::shared_ptr<erhe::graphics::Buffer>            output;
        int                                                stride{1};
    };

    class Vertex_draw
    {
    public:
        erhe::dataformat::Vertex_format                     vertex_format;
        std::unique_ptr<erhe::graphics::Vertex_input_state> vertex_input;
        std::unique_ptr<erhe::graphics::Bind_group_layout>  layout;
        std::unique_ptr<erhe::graphics::Shader_stages>      shader_stages;
        std::unique_ptr<erhe::graphics::Render_pipeline>    pipeline;
        std::shared_ptr<erhe::graphics::Texture>            output;
        erhe::graphics::Render_pass_descriptor              descriptor{};
    };

    class Vertex_fetch
    {
    public:
        std::unique_ptr<erhe::graphics::Bind_group_layout> layout;
        std::unique_ptr<erhe::graphics::Sampler>           sampler;
        std::unique_ptr<erhe::graphics::Shader_stages>     shader_stages;
        std::unique_ptr<erhe::graphics::Render_pipeline>   pipeline;
        std::shared_ptr<erhe::graphics::Texture>           output;
        erhe::graphics::Render_pass_descriptor             descriptor{};
    };

    class Slow_pass
    {
    public:
        std::unique_ptr<erhe::graphics::Bind_group_layout> layout;
        std::unique_ptr<erhe::graphics::Shader_stages>     shader_stages;
        std::unique_ptr<erhe::graphics::Render_pipeline>   pipeline;
        std::shared_ptr<erhe::graphics::Buffer>            spin;
        erhe::graphics::Render_pass_descriptor             descriptor{};
    };

    void SetUp() override
    {
        Gpu_test::SetUp();
        m_spin_block = std::make_unique<erhe::graphics::Shader_resource>(
            device(),
            erhe::graphics::Shader_resource::Block_create_info{
                .name          = "Spin",
                .binding_point = c_spin_binding,
                .type          = erhe::graphics::Shader_resource::Type::uniform_block
            }
        );
        m_spin_block->add_uint("count");
        m_spin_block->add_uint("sentinel");
        m_spin_block->add_uint("pad0");
        m_spin_block->add_uint("pad1");
    }

    void TearDown() override
    {
        m_probe.reset();
        m_draw.reset();
        m_slow_pass.reset();
        m_vertex_fetch.reset();
        m_compute_layout.reset();
        m_src_block.reset();
        m_dst_float_block.reset();
        m_dst_uint_block.reset();
        m_spin_block.reset();
        Gpu_test::TearDown();
    }

    [[nodiscard]] auto make_spin_buffer(const uint32_t count) -> std::shared_ptr<erhe::graphics::Buffer>
    {
        std::shared_ptr<erhe::graphics::Buffer> buffer = make_host_buffer(c_spin_bytes, erhe::graphics::Buffer_usage::uniform, "barrier spin");
        const std::array<uint32_t, 4> values{ count, c_sentinel, 0u, 0u };
        const std::span<std::byte> mapped = buffer->map_bytes(0, c_spin_bytes);
        std::memcpy(mapped.data(), values.data(), c_spin_bytes);
        buffer->unmap();
        return buffer;
    }

    // --- compute: uniform Spin, storage Src / Dst --------------------------

    void make_compute_layout(const Dst_data dst_data)
    {
        erhe::graphics::Device& graphics_device = device();
        m_compute_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
            graphics_device,
            erhe::graphics::Bind_group_layout_create_info{
                .bindings = {
                    erhe::graphics::Bind_group_layout_binding{ .binding_point = c_spin_binding, .type = erhe::graphics::Binding_type::uniform_buffer, .stage_flags = erhe::graphics::Shader_stage_flags::compute },
                    erhe::graphics::Bind_group_layout_binding{ .binding_point = c_src_binding,  .type = erhe::graphics::Binding_type::storage_buffer, .stage_flags = erhe::graphics::Shader_stage_flags::compute },
                    erhe::graphics::Bind_group_layout_binding{ .binding_point = c_dst_binding,  .type = erhe::graphics::Binding_type::storage_buffer, .stage_flags = erhe::graphics::Shader_stage_flags::compute }
                },
                .debug_label       = erhe::utility::Debug_label{"barrier compute layout"},
                .uses_texture_heap = false
            }
        );
        m_src_block = std::make_unique<erhe::graphics::Shader_resource>(
            graphics_device,
            erhe::graphics::Shader_resource::Block_create_info{ .name = "Src", .binding_point = c_src_binding, .type = erhe::graphics::Shader_resource::Type::shader_storage_block }
        );
        m_src_block->add_uint("data", erhe::graphics::Shader_resource::unsized_array);
        m_dst_float_block = std::make_unique<erhe::graphics::Shader_resource>(
            graphics_device,
            erhe::graphics::Shader_resource::Block_create_info{ .name = "Dst", .binding_point = c_dst_binding, .type = erhe::graphics::Shader_resource::Type::shader_storage_block }
        );
        m_dst_float_block->add_float("data", erhe::graphics::Shader_resource::unsized_array);
        m_dst_uint_block = std::make_unique<erhe::graphics::Shader_resource>(
            graphics_device,
            erhe::graphics::Shader_resource::Block_create_info{ .name = "Dst", .binding_point = c_dst_binding, .type = erhe::graphics::Shader_resource::Type::shader_storage_block }
        );
        m_dst_uint_block->add_uint("data", erhe::graphics::Shader_resource::unsized_array);
        m_dst_data = dst_data;
    }

    [[nodiscard]] auto make_compute(const char* name, const std::string& source, const std::vector<std::pair<std::string, std::string>>& defines) -> Compute_program
    {
        const erhe::graphics::Shader_resource* dst = (m_dst_data == Dst_data::float_data) ? m_dst_float_block.get() : m_dst_uint_block.get();
        return make_compute_program(name, source, defines, {}, { m_spin_block.get(), m_src_block.get(), dst }, *m_compute_layout);
    }

    void bind_compute(
        erhe::graphics::Compute_command_encoder& encoder,
        const Compute_program&                   program,
        const erhe::graphics::Buffer&            spin,
        const erhe::graphics::Buffer&            src,
        const erhe::graphics::Buffer&            dst
    )
    {
        encoder.set_bind_group_layout(m_compute_layout.get());
        encoder.set_compute_pipeline(*program.pipeline);
        encoder.set_buffer(erhe::graphics::Buffer_target::uniform, &spin, 0, c_spin_bytes, c_spin_binding);
        encoder.set_buffer(erhe::graphics::Buffer_target::storage, &src,  0, src.get_capacity_byte_count(), c_src_binding);
        encoder.set_buffer(erhe::graphics::Buffer_target::storage, &dst,  0, dst.get_capacity_byte_count(), c_dst_binding);
    }

    // --- probe: texelFetch a 64 x 64 grid of texels in compute -------------

    [[nodiscard]] auto make_probe(const Probe_kind kind, const int texture_size) -> bool
    {
        erhe::graphics::Device& graphics_device = device();
        m_probe = std::make_unique<Probe>();
        Probe& probe = *m_probe;
        probe.stride = texture_size / c_probe_grid;
        probe.sampler = std::make_unique<erhe::graphics::Sampler>(
            graphics_device,
            erhe::graphics::Sampler_create_info{
                .min_filter   = erhe::graphics::Filter::nearest,
                .mag_filter   = erhe::graphics::Filter::nearest,
                .mipmap_mode  = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
                .address_mode = {
                    erhe::graphics::Sampler_address_mode::clamp_to_edge,
                    erhe::graphics::Sampler_address_mode::clamp_to_edge,
                    erhe::graphics::Sampler_address_mode::clamp_to_edge
                },
                .debug_label  = erhe::utility::Debug_label{"barrier probe sampler"}
            }
        );
        const bool is_array = (kind == Probe_kind::color_2d_array_layer_1);
        const bool is_depth = (kind == Probe_kind::depth_2d);
        probe.layout = std::make_unique<erhe::graphics::Bind_group_layout>(
            graphics_device,
            erhe::graphics::Bind_group_layout_create_info{
                .bindings = {
                    erhe::graphics::Bind_group_layout_binding{
                        .binding_point  = 0,
                        .type           = erhe::graphics::Binding_type::combined_image_sampler,
                        .sampler_aspect = is_depth ? erhe::graphics::Sampler_aspect::depth : erhe::graphics::Sampler_aspect::color,
                        .name           = "s_source",
                        .glsl_type      = is_array ? erhe::graphics::Glsl_type::sampler_2d_array : erhe::graphics::Glsl_type::sampler_2d,
                        .stage_flags    = erhe::graphics::Shader_stage_flags::compute
                    },
                    erhe::graphics::Bind_group_layout_binding{
                        .binding_point = 1,
                        .type          = erhe::graphics::Binding_type::storage_buffer,
                        .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                    }
                },
                .debug_label       = erhe::utility::Debug_label{"barrier probe layout"},
                .uses_texture_heap = false
            }
        );
        m_probe_block = std::make_unique<erhe::graphics::Shader_resource>(
            graphics_device,
            erhe::graphics::Shader_resource::Block_create_info{ .name = "Probe", .binding_point = 1, .type = erhe::graphics::Shader_resource::Type::shader_storage_block }
        );
        m_probe_block->add_uint("data", erhe::graphics::Shader_resource::unsized_array);
        const std::vector<std::pair<std::string, std::string>> defines{
            { "PROBE_GRID",   std::to_string(c_probe_grid) },
            { "PROBE_STRIDE", std::to_string(probe.stride) },
            { "FETCH(p)",     is_array ? std::string{"texelFetch(s_source, ivec3(p, 1), 0)"} : std::string{"texelFetch(s_source, p, 0)"} },
            { "ENCODE(v)",    is_depth ? std::string{"floatBitsToUint(v.r)"} : std::string{"packUnorm4x8(v)"} }
        };
        probe.program = make_compute_program("barrier_probe", c_probe_source, defines, {}, { m_probe_block.get() }, *probe.layout);
        probe.output  = make_readback_buffer(c_probe_count * sizeof(uint32_t), "barrier probe output");
        return probe.program.is_valid();
    }

    void record_probe(erhe::graphics::Command_buffer& command_buffer, const erhe::graphics::Texture& texture)
    {
        Probe& probe = *m_probe;
        {
            erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(probe.layout.get());
            encoder.set_compute_pipeline(*probe.program.pipeline);
            encoder.set_sampled_image(0, texture, *probe.sampler);
            encoder.set_buffer(erhe::graphics::Buffer_target::storage, probe.output.get(), 0, c_probe_count * sizeof(uint32_t), 1);
            encoder.dispatch_compute(c_probe_grid / 8, c_probe_grid / 8, 1);
        }
        command_buffer.buffer_barrier(*probe.output, Buffer_usage::storage_write | Buffer_usage::compute_shader, Buffer_usage::host_read);
    }

    [[nodiscard]] auto read_probe() -> std::vector<uint32_t>
    {
        const std::vector<std::byte> raw = read_buffer(*m_probe->output, c_probe_count * sizeof(uint32_t));
        std::vector<uint32_t> values(c_probe_count);
        std::memcpy(values.data(), raw.data(), c_probe_count * sizeof(uint32_t));
        return values;
    }

    // The probe values of the slow color producers (spin count c_fragment_spin
    // of each texel, the texel at x + y * size).
    [[nodiscard]] auto expected_pattern_probe(const int texture_size) const -> std::vector<uint32_t>
    {
        const int stride = texture_size / c_probe_grid;
        std::vector<uint32_t> expected(c_probe_count);
        for (int gy = 0; gy < c_probe_grid; ++gy) {
            for (int gx = 0; gx < c_probe_grid; ++gx) {
                const uint32_t x   = static_cast<uint32_t>((gx * stride) + stride - 1);
                const uint32_t y   = static_cast<uint32_t>((gy * stride) + stride - 1);
                const bool     hit = cpu_spin(x + (y * static_cast<uint32_t>(texture_size)), c_fragment_spin) == c_sentinel;
                expected[(static_cast<std::size_t>(gy) * c_probe_grid) + static_cast<std::size_t>(gx)] = pack_rgba8(x & 255u, y & 255u, hit ? 0u : 64u, 255u);
            }
        }
        return expected;
    }

    void expect_probe(const std::vector<uint32_t>& expected, const std::string_view label)
    {
        const std::vector<uint32_t> actual = read_probe();
        ASSERT_EQ(actual.size(), expected.size());
        std::size_t mismatches = 0;
        std::size_t first      = 0;
        for (std::size_t i = 0; i < actual.size(); ++i) {
            if (actual[i] != expected[i]) {
                if (mismatches == 0) {
                    first = i;
                }
                ++mismatches;
            }
        }
        EXPECT_EQ(mismatches, 0u)
            << label << ": " << mismatches << " of " << actual.size() << " probed texels differ; first at grid index " << first
            << " (0x" << std::hex << actual[first] << " instead of 0x" << expected[first] << std::dec << ")";
    }

    // --- slow fullscreen producer passes -------------------------------------

    [[nodiscard]] auto make_fullscreen_shader(
        const char*                                             name,
        const std::string&                                      fragment_source,
        const std::vector<std::pair<std::string, std::string>>& defines,
        const erhe::graphics::Bind_group_layout&                layout,
        const erhe::graphics::Fragment_outputs*                 fragment_outputs
    ) -> std::unique_ptr<erhe::graphics::Shader_stages>
    {
        erhe::graphics::Shader_stages_create_info create_info{
            .name             = name,
            .defines          = defines,
            .interface_blocks = { m_spin_block.get() },
            .fragment_outputs = fragment_outputs,
            .no_vertex_input  = true,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_fullscreen_vertex_source} },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{fragment_source}            }
            },
            .bind_group_layout = &layout
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), create_info);
        if (!prototype.is_valid()) {
            ADD_FAILURE() << name << ": shader failed to compile/link";
            return {};
        }
        return std::make_unique<erhe::graphics::Shader_stages>(device(), std::move(prototype));
    }

    [[nodiscard]] auto make_pipeline(
        const erhe::graphics::Render_pass_descriptor& descriptor,
        erhe::graphics::Shader_stages&                shader_stages,
        const erhe::graphics::Bind_group_layout&      layout,
        const erhe::graphics::Vertex_input_state*     vertex_input,
        const erhe::dataformat::Vertex_format*        vertex_format,
        const Slow_target                             depth_use
    ) -> std::unique_ptr<erhe::graphics::Render_pipeline>
    {
        const bool depth = (depth_use == Slow_target::depth);
        erhe::graphics::Render_pipeline_create_info create_info;
        create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
        create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
        create_info.base.depth_stencil.depth_test_enable   = depth;
        create_info.base.depth_stencil.depth_write_enable  = depth;
        create_info.base.depth_stencil.depth_compare_op    = erhe::graphics::Compare_operation::always;
        create_info.base.depth_stencil.stencil_test_enable = false;
        create_info.base.bind_group_layout                 = &layout;
        create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
        create_info.shader_stages                          = &shader_stages;
        create_info.vertex_input                           = vertex_input;
        create_info.vertex_format                          = vertex_format;
        create_info.set_format_from_render_pass(descriptor);
        std::unique_ptr<erhe::graphics::Render_pipeline> pipeline = std::make_unique<erhe::graphics::Render_pipeline>(device(), create_info);
        if (!pipeline->is_valid()) {
            ADD_FAILURE() << "pipeline is not valid";
        }
        return pipeline;
    }

    // A slow fullscreen pass into a color attachment (layer `layer`) or into
    // a depth attachment, left in its attachment layout (user_synchronized:
    // the explicit barrier after it is the only synchronization).
    [[nodiscard]] auto make_slow_pass(const erhe::graphics::Texture& target, const unsigned int layer, const Slow_target slow_target) -> bool
    {
        const bool depth = (slow_target == Slow_target::depth);
        erhe::graphics::Device& graphics_device = device();
        m_slow_pass = std::make_unique<Slow_pass>();
        Slow_pass& pass = *m_slow_pass;
        pass.spin = make_spin_buffer(c_fragment_spin);
        pass.layout = std::make_unique<erhe::graphics::Bind_group_layout>(
            graphics_device,
            erhe::graphics::Bind_group_layout_create_info{
                .bindings = {
                    erhe::graphics::Bind_group_layout_binding{ .binding_point = c_spin_binding, .type = erhe::graphics::Binding_type::uniform_buffer, .stage_flags = erhe::graphics::Shader_stage_flags::fragment }
                },
                .debug_label       = erhe::utility::Debug_label{"barrier slow pass layout"},
                .uses_texture_heap = false
            }
        );
        const std::vector<std::pair<std::string, std::string>> defines{ { "TARGET_SIZE", std::to_string(c_heavy_size) } };
        pass.shader_stages = make_fullscreen_shader(
            depth ? "barrier_slow_depth" : "barrier_slow_color",
            with_spin(depth ? c_depth_fragment_body : c_pattern_fragment_body),
            defines,
            *pass.layout,
            depth ? nullptr : &m_fragment_outputs
        );
        if (!pass.shader_stages) {
            return false;
        }
        erhe::graphics::Render_pass_attachment_descriptor& attachment = depth ? pass.descriptor.depth_attachment : pass.descriptor.color_attachments[0];
        attachment.texture       = &target;
        attachment.texture_layer = layer;
        attachment.clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
        attachment.load_action   = erhe::graphics::Load_action::Clear;
        attachment.store_action  = erhe::graphics::Store_action::Store;
        attachment.usage_before  = Image_usage::none;
        attachment.layout_before = erhe::graphics::Image_layout::undefined;
        attachment.usage_after   = (depth ? Image_usage::depth_stencil_attachment : Image_usage::color_attachment) | Image_usage::user_synchronized;
        attachment.layout_after  = depth ? erhe::graphics::Image_layout::depth_stencil_attachment_optimal : erhe::graphics::Image_layout::color_attachment_optimal;
        pass.descriptor.render_target_width  = c_heavy_size;
        pass.descriptor.render_target_height = c_heavy_size;
        pass.descriptor.debug_label = erhe::utility::Debug_label{"barrier slow pass"};
        pass.pipeline = make_pipeline(pass.descriptor, *pass.shader_stages, *pass.layout, nullptr, nullptr, slow_target);
        return pass.pipeline && pass.pipeline->is_valid();
    }

    void record_slow_pass(erhe::graphics::Command_buffer& command_buffer)
    {
        Slow_pass& pass = *m_slow_pass;
        erhe::graphics::Render_pass              render_pass{device(), pass.descriptor};
        erhe::graphics::Render_command_encoder   encoder = device().make_render_command_encoder(command_buffer);
        const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
        encoder.set_viewport_rect(0, 0, c_heavy_size, c_heavy_size);
        encoder.set_scissor_rect (0, 0, c_heavy_size, c_heavy_size);
        encoder.set_bind_group_layout(pass.layout.get());
        encoder.set_render_pipeline(*pass.pipeline);
        encoder.set_buffer(erhe::graphics::Buffer_target::uniform, pass.spin.get(), 0, c_spin_bytes, c_spin_binding);
        encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
    }

    [[nodiscard]] auto make_texture(
        const uint64_t                     usage_mask,
        const erhe::graphics::Texture_type type,
        const erhe::dataformat::Format     format,
        const int                          size,
        const int                          array_layer_count,
        const char*                        label
    ) -> std::shared_ptr<erhe::graphics::Texture>
    {
        erhe::graphics::Device& graphics_device = device();
        return std::make_shared<erhe::graphics::Texture>(
            graphics_device,
            erhe::graphics::Texture_create_info{
                .device            = graphics_device,
                .usage_mask        = usage_mask,
                .type              = type,
                .pixelformat       = format,
                .width             = size,
                .height            = size,
                .array_layer_count = array_layer_count,
                .debug_label       = erhe::utility::Debug_label{label}
            }
        );
    }

    // --- vertex-stage fetch: a render pass whose vertex shader samples ------

    [[nodiscard]] auto make_vertex_fetch(const int texture_size, const Fetch_source source = Fetch_source::color) -> bool
    {
        const bool fetch_depth = (source == Fetch_source::depth);
        erhe::graphics::Device& graphics_device = device();
        m_vertex_fetch = std::make_unique<Vertex_fetch>();
        Vertex_fetch& fetch = *m_vertex_fetch;
        fetch.sampler = std::make_unique<erhe::graphics::Sampler>(
            graphics_device,
            erhe::graphics::Sampler_create_info{
                .min_filter   = erhe::graphics::Filter::nearest,
                .mag_filter   = erhe::graphics::Filter::nearest,
                .mipmap_mode  = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
                .address_mode = {
                    erhe::graphics::Sampler_address_mode::clamp_to_edge,
                    erhe::graphics::Sampler_address_mode::clamp_to_edge,
                    erhe::graphics::Sampler_address_mode::clamp_to_edge
                },
                .debug_label  = erhe::utility::Debug_label{"barrier vertex fetch sampler"}
            }
        );
        fetch.layout = std::make_unique<erhe::graphics::Bind_group_layout>(
            graphics_device,
            erhe::graphics::Bind_group_layout_create_info{
                .bindings = {
                    erhe::graphics::Bind_group_layout_binding{
                        .binding_point  = 0,
                        .type           = erhe::graphics::Binding_type::combined_image_sampler,
                        .sampler_aspect = fetch_depth ? erhe::graphics::Sampler_aspect::depth : erhe::graphics::Sampler_aspect::color,
                        .name           = "s_source",
                        .glsl_type      = erhe::graphics::Glsl_type::sampler_2d,
                        .stage_flags    = erhe::graphics::Shader_stage_flags::vertex
                    }
                },
                .debug_label       = erhe::utility::Debug_label{"barrier vertex fetch layout"},
                .uses_texture_heap = false
            }
        );
        const erhe::math::Coordinate_conventions& conventions = graphics_device.get_info().coordinate_conventions;
        const char* const y_sign = (conventions.texture_origin == erhe::math::Texture_origin::top_left) ? "-1.0" : "1.0";
        erhe::graphics::Shader_stages_create_info create_info{
            .name             = "barrier_vertex_fetch",
            .defines          = {
                { "PROBE_GRID",   std::to_string(c_probe_grid)                },
                { "PROBE_STRIDE", std::to_string(texture_size / c_probe_grid) },
                { "Y_SIGN",       y_sign                                      },
                { "FETCH_DEPTH",  fetch_depth ? "1" : "0"                     }
            },
            .fragment_outputs = &m_fragment_outputs,
            .no_vertex_input  = true,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_fetch_vertex_source}   },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_vertex_fetch_fragment_source} }
            },
            .bind_group_layout = fetch.layout.get()
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, create_info);
        if (!prototype.is_valid()) {
            ADD_FAILURE() << "barrier_vertex_fetch: shader failed to compile/link";
            return false;
        }
        fetch.shader_stages = std::make_unique<erhe::graphics::Shader_stages>(graphics_device, std::move(prototype));
        fetch.output        = make_color_target(c_probe_grid, c_probe_grid);

        erhe::graphics::Render_pass_descriptor& descriptor = fetch.descriptor;
        descriptor.color_attachments[0].texture       = fetch.output.get();
        descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 0.0 };
        descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = Image_usage::transfer_src;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[0].usage_after   = Image_usage::transfer_src;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.render_target_width  = c_probe_grid;
        descriptor.render_target_height = c_probe_grid;
        descriptor.debug_label = erhe::utility::Debug_label{"barrier vertex fetch pass"};

        erhe::graphics::Render_pipeline_create_info pipeline_info;
        pipeline_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::point;
        pipeline_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
        pipeline_info.base.depth_stencil.depth_test_enable   = false;
        pipeline_info.base.depth_stencil.depth_write_enable  = false;
        pipeline_info.base.depth_stencil.stencil_test_enable = false;
        pipeline_info.base.bind_group_layout                 = fetch.layout.get();
        pipeline_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
        pipeline_info.shader_stages                          = fetch.shader_stages.get();
        pipeline_info.set_format_from_render_pass(descriptor);
        fetch.pipeline = std::make_unique<erhe::graphics::Render_pipeline>(graphics_device, pipeline_info);
        return fetch.pipeline->is_valid();
    }

    void record_vertex_fetch(erhe::graphics::Command_buffer& command_buffer, const erhe::graphics::Texture& texture)
    {
        Vertex_fetch& fetch = *m_vertex_fetch;
        erhe::graphics::Render_pass              render_pass{device(), fetch.descriptor};
        erhe::graphics::Render_command_encoder   encoder = device().make_render_command_encoder(command_buffer);
        const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
        encoder.set_viewport_rect(0, 0, c_probe_grid, c_probe_grid);
        encoder.set_scissor_rect (0, 0, c_probe_grid, c_probe_grid);
        encoder.set_bind_group_layout(fetch.layout.get());
        encoder.set_render_pipeline(*fetch.pipeline);
        encoder.set_sampled_image(0, texture, *fetch.sampler);
        encoder.draw_primitives(erhe::graphics::Primitive_type::point, 0, c_probe_count);
    }

    // The vertex fetch target against the probe values (pixel (gx, gy) holds
    // grid index gx + gy * PROBE_GRID).
    void expect_vertex_fetch(const std::vector<uint32_t>& expected, const std::string_view label)
    {
        const std::vector<uint8_t> pixels = read_texture_rgba8(*m_vertex_fetch->output);
        ASSERT_EQ(pixels.size(), c_probe_count * 4u);
        std::size_t mismatches = 0;
        std::size_t first      = 0;
        for (std::size_t i = 0; i < c_probe_count; ++i) {
            const uint32_t actual = pack_rgba8(pixels[(i * 4) + 0], pixels[(i * 4) + 1], pixels[(i * 4) + 2], pixels[(i * 4) + 3]);
            if (actual != expected[i]) {
                if (mismatches == 0) {
                    first = i;
                }
                ++mismatches;
            }
        }
        EXPECT_EQ(mismatches, 0u) << label << ": " << mismatches << " of " << c_probe_count << " vertex-fetched texels differ; first at grid index " << first;
    }

    // An empty encoder of one kind: the first encoder after a barrier, so the
    // consumer is the second encoder of its kind.
    void record_empty_compute_encoder(erhe::graphics::Command_buffer& command_buffer)
    {
        erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
        static_cast<void>(encoder);
    }

    void record_empty_render_pass(erhe::graphics::Command_buffer& command_buffer, const erhe::graphics::Texture& target)
    {
        erhe::graphics::Render_pass_descriptor descriptor{};
        descriptor.color_attachments[0].texture       = &target;
        descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 0.0 };
        descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = Image_usage::none;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::undefined;
        descriptor.color_attachments[0].usage_after   = Image_usage::color_attachment | Image_usage::user_synchronized;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::color_attachment_optimal;
        descriptor.render_target_width  = target.get_width();
        descriptor.render_target_height = target.get_height();
        descriptor.debug_label = erhe::utility::Debug_label{"barrier empty pass"};
        erhe::graphics::Render_pass              render_pass{device(), descriptor};
        erhe::graphics::Render_command_encoder   encoder = device().make_render_command_encoder(command_buffer);
        const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
    }

    // --- vertex input draw ----------------------------------------------------

    [[nodiscard]] auto make_vertex_draw() -> bool
    {
        erhe::graphics::Device& graphics_device = device();
        m_draw = std::make_unique<Vertex_draw>();
        Vertex_draw& draw = *m_draw;
        draw.vertex_format = erhe::dataformat::Vertex_format{
            erhe::dataformat::Vertex_stream{
                0,
                { erhe::dataformat::Vertex_attribute{
                      .format      = erhe::dataformat::Format::format_32_vec2_float,
                      .usage_type  = erhe::dataformat::Vertex_attribute_usage::position,
                      .usage_index = 0
                  } }
            }
        };
        draw.vertex_input = std::make_unique<erhe::graphics::Vertex_input_state>(
            graphics_device,
            erhe::graphics::Vertex_input_state_data::make(draw.vertex_format)
        );
        draw.layout = std::make_unique<erhe::graphics::Bind_group_layout>(
            graphics_device,
            erhe::graphics::Bind_group_layout_create_info{
                .bindings          = {},
                .debug_label       = erhe::utility::Debug_label{"barrier vertex draw layout"},
                .uses_texture_heap = false
            }
        );
        erhe::graphics::Shader_stages_create_info create_info{
            .name             = "barrier_vertex_draw",
            .fragment_outputs = &m_fragment_outputs,
            .vertex_format    = &draw.vertex_format,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_position_vertex_source} },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_green_fragment_source}  }
            },
            .bind_group_layout = draw.layout.get()
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, create_info);
        if (!prototype.is_valid()) {
            ADD_FAILURE() << "barrier_vertex_draw: shader failed to compile/link";
            return false;
        }
        draw.shader_stages = std::make_unique<erhe::graphics::Shader_stages>(graphics_device, std::move(prototype));
        draw.output        = make_color_target(c_draw_size, c_draw_size);

        erhe::graphics::Render_pass_descriptor& descriptor = draw.descriptor;
        descriptor.color_attachments[0].texture       = draw.output.get();
        descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
        descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = Image_usage::transfer_src;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[0].usage_after   = Image_usage::transfer_src;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.render_target_width  = c_draw_size;
        descriptor.render_target_height = c_draw_size;
        descriptor.debug_label = erhe::utility::Debug_label{"barrier vertex draw pass"};
        draw.pipeline = make_pipeline(descriptor, *draw.shader_stages, *draw.layout, draw.vertex_input.get(), &draw.vertex_format, Slow_target::color);
        return draw.pipeline && draw.pipeline->is_valid();
    }

    template <typename Draw_fn>
    void record_draw(erhe::graphics::Command_buffer& command_buffer, const erhe::graphics::Buffer& vertex_buffer, Draw_fn&& draw_fn)
    {
        Vertex_draw& draw = *m_draw;
        erhe::graphics::Render_pass              render_pass{device(), draw.descriptor};
        erhe::graphics::Render_command_encoder   encoder = device().make_render_command_encoder(command_buffer);
        const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
        encoder.set_viewport_rect(0, 0, c_draw_size, c_draw_size);
        encoder.set_scissor_rect (0, 0, c_draw_size, c_draw_size);
        encoder.set_bind_group_layout(draw.layout.get());
        encoder.set_render_pipeline(*draw.pipeline);
        encoder.set_vertex_buffer(&vertex_buffer, 0, 0);
        draw_fn(encoder);
    }

    void expect_draw_is_green()
    {
        const std::vector<uint8_t> pixels = read_texture_rgba8(*m_draw->output);
        ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_draw_size) * static_cast<std::size_t>(c_draw_size) * 4u);
        int green = 0;
        for (std::size_t i = 0; (i + 3) < pixels.size(); i += 4) {
            if ((pixels[i + 1] >= 250) && (pixels[i + 0] <= 5) && (pixels[i + 2] <= 5)) {
                ++green;
            }
        }
        EXPECT_EQ(green, c_draw_size * c_draw_size) << "the GPU-written draw should cover the whole target";
    }

    // The compute-written vertex buffer case, producer and consumer in one
    // command buffer or (earlier_submissions) in two.
    void check_compute_vertices(const erhe::graphics::Barrier_scope scope)
    {
        make_compute_layout(Dst_data::float_data);
        ASSERT_TRUE(make_vertex_draw());
        const Compute_program writer = make_compute(
            "barrier_vertex_writer",
            with_spin(c_vertex_writer_body),
            { { "VERTEX_COUNT", std::to_string(c_vertex_count) }, { "GRID_CELLS", std::to_string(c_grid_cells) } }
        );
        ASSERT_TRUE(writer.is_valid());
        const std::shared_ptr<erhe::graphics::Buffer> spin     = make_spin_buffer(c_vertex_spin);
        const std::shared_ptr<erhe::graphics::Buffer> unused   = make_host_buffer(16, erhe::graphics::Buffer_usage::storage, "barrier unused");
        const std::shared_ptr<erhe::graphics::Buffer> vertices = make_host_buffer(
            c_vertex_bytes, erhe::graphics::Buffer_usage::vertex | erhe::graphics::Buffer_usage::storage, "barrier vertex buffer"
        );
        {
            // Zeros: a draw that reads them rasterizes nothing.
            const std::span<std::byte> mapped = vertices->map_bytes(0, c_vertex_bytes);
            std::memset(mapped.data(), 0, c_vertex_bytes);
            vertices->unmap();
        }
        const auto record_writer = [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
            bind_compute(encoder, writer, *spin, *unused, *vertices);
            encoder.dispatch_compute((c_vertex_count + 63u) / 64u, 1, 1);
        };
        const auto record_consumer = [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.buffer_barrier(*vertices, Buffer_usage::storage_write | Buffer_usage::compute_shader, Buffer_usage::vertex, scope);
            record_draw(command_buffer, *vertices, [](erhe::graphics::Render_command_encoder& encoder) {
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, c_vertex_count);
            });
        };
        if (scope == erhe::graphics::Barrier_scope::command_buffer) {
            submit_and_wait([&](erhe::graphics::Command_buffer& command_buffer) {
                record_writer(command_buffer);
                record_consumer(command_buffer);
            });
        } else {
            submit_two(record_writer, record_consumer);
        }
        expect_draw_is_green();
    }

    // Producer and consumer in two command buffers of one frame, submitted
    // back to back without a CPU wait between them.
    template <typename Producer_fn, typename Consumer_fn>
    void submit_two(Producer_fn&& producer_fn, Consumer_fn&& consumer_fn)
    {
        erhe::graphics::Device& graphics_device = device();
        ASSERT_TRUE(graphics_device.wait_frame());
        erhe::graphics::Command_buffer& producer = graphics_device.get_command_buffer(0);
        producer.begin();
        producer_fn(producer);
        producer.end();
        erhe::graphics::Command_buffer* producers[] = { &producer };
        graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{producers});
        erhe::graphics::Command_buffer& consumer = graphics_device.get_command_buffer(0);
        consumer.begin();
        consumer_fn(consumer);
        consumer.end();
        erhe::graphics::Command_buffer* consumers[] = { &consumer };
        graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{consumers});
        graphics_device.wait_idle();
        ASSERT_TRUE(graphics_device.end_frame());
    }

    // Element writer -> barrier with the reader's compute encoder already
    // open -> reader dispatch in the same encoder.
    void check_in_encoder(const erhe::graphics::Barrier_scope scope)
    {
        make_compute_layout(Dst_data::uint_data);
        const std::vector<std::pair<std::string, std::string>> defines{ { "ELEMENT_COUNT", std::to_string(c_element_count) } };
        const Compute_program writer = make_compute("barrier_element_writer", with_spin(c_element_writer_body), defines);
        const Compute_program reader = make_compute("barrier_element_reader", with_spin(c_element_reader_body), defines);
        ASSERT_TRUE(writer.is_valid());
        ASSERT_TRUE(reader.is_valid());
        const std::size_t bytes = static_cast<std::size_t>(c_element_count) * sizeof(uint32_t);
        const std::shared_ptr<erhe::graphics::Buffer> spin     = make_spin_buffer(c_element_spin);
        const std::shared_ptr<erhe::graphics::Buffer> elements = make_host_buffer(bytes, erhe::graphics::Buffer_usage::storage, "barrier elements");
        const std::shared_ptr<erhe::graphics::Buffer> scratch  = make_host_buffer(bytes, erhe::graphics::Buffer_usage::storage, "barrier scratch");
        const std::shared_ptr<erhe::graphics::Buffer> output   = make_readback_buffer(bytes, "barrier element output");
        for (const std::shared_ptr<erhe::graphics::Buffer>& buffer : { elements, scratch, output }) {
            const std::span<std::byte> mapped = buffer->map_bytes(0, bytes);
            std::memset(mapped.data(), 0, bytes);
            buffer->unmap();
        }
        const std::uintptr_t groups = (c_element_count + 63u) / 64u;
        const auto record_reader_encoder = [&](erhe::graphics::Command_buffer& command_buffer, const Barrier_scope_case first_dispatch) {
            const bool with_writer = (first_dispatch == Barrier_scope_case::writer);
            {
                erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
                if (with_writer) {
                    bind_compute(encoder, writer, *spin, *scratch, *elements);
                    encoder.dispatch_compute(groups, 1, 1);
                } else {
                    // An unrelated dispatch first, so the barrier below sits
                    // between two dispatches of one open encoder.
                    bind_compute(encoder, reader, *spin, *scratch, *scratch);
                    encoder.dispatch_compute(groups, 1, 1);
                }
                command_buffer.buffer_barrier(
                    *elements,
                    Buffer_usage::storage_write | Buffer_usage::compute_shader,
                    Buffer_usage::storage_read  | Buffer_usage::compute_shader,
                    scope
                );
                bind_compute(encoder, reader, *spin, *elements, *output);
                encoder.dispatch_compute(groups, 1, 1);
            }
            command_buffer.buffer_barrier(*output, Buffer_usage::storage_write | Buffer_usage::compute_shader, Buffer_usage::host_read);
        };
        if (scope == erhe::graphics::Barrier_scope::command_buffer) {
            submit_and_wait([&](erhe::graphics::Command_buffer& command_buffer) {
                record_reader_encoder(command_buffer, Barrier_scope_case::writer);
            });
        } else {
            submit_two(
                [&](erhe::graphics::Command_buffer& command_buffer) {
                    erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
                    bind_compute(encoder, writer, *spin, *scratch, *elements);
                    encoder.dispatch_compute(groups, 1, 1);
                },
                [&](erhe::graphics::Command_buffer& command_buffer) {
                    record_reader_encoder(command_buffer, Barrier_scope_case::unrelated);
                }
            );
        }
        const std::vector<std::byte> raw = read_buffer(*output, bytes);
        std::vector<uint32_t> values(c_element_count);
        std::memcpy(values.data(), raw.data(), bytes);
        uint32_t mismatches = 0;
        uint32_t first      = 0;
        for (uint32_t i = 0; i < c_element_count; ++i) {
            const uint32_t written  = (cpu_spin(i, c_element_spin) == c_sentinel) ? 0u : ((3u * i) + 1u);
            if (values[i] != (written + 1u)) {
                if (mismatches == 0) {
                    first = i;
                }
                ++mismatches;
            }
        }
        EXPECT_EQ(mismatches, 0u) << mismatches << " of " << c_element_count << " elements wrong; first " << first << " = " << values[first];
    }

    const erhe::graphics::Fragment_outputs m_fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };
    std::unique_ptr<erhe::graphics::Shader_resource>   m_spin_block;
    std::unique_ptr<erhe::graphics::Shader_resource>   m_src_block;
    std::unique_ptr<erhe::graphics::Shader_resource>   m_dst_float_block;
    std::unique_ptr<erhe::graphics::Shader_resource>   m_dst_uint_block;
    std::unique_ptr<erhe::graphics::Shader_resource>   m_probe_block;
    std::unique_ptr<erhe::graphics::Bind_group_layout> m_compute_layout;
    Dst_data                                           m_dst_data{Dst_data::uint_data};
    std::unique_ptr<Probe>                             m_probe;
    std::unique_ptr<Vertex_draw>                       m_draw;
    std::unique_ptr<Slow_pass>                         m_slow_pass;
    std::unique_ptr<Vertex_fetch>                      m_vertex_fetch;
};

// Compute storage write -> buffer_barrier -> vertex attribute fetch, in one
// command buffer.
TEST_F(Resource_barrier_test, compute_storage_buffer_to_vertex_input)
{
    check_compute_vertices(erhe::graphics::Barrier_scope::command_buffer);
}

// The same, the producer in an earlier command buffer of the frame:
// Barrier_scope::earlier_submissions.
TEST_F(Resource_barrier_test, earlier_submission_storage_buffer_to_vertex_input)
{
    check_compute_vertices(erhe::graphics::Barrier_scope::earlier_submissions);
}

// Compute storage write of draw arguments -> buffer_barrier -> indirect draw.
TEST_F(Resource_barrier_test, compute_storage_buffer_to_indirect)
{
    make_compute_layout(Dst_data::uint_data);
    ASSERT_TRUE(make_vertex_draw());
    const Compute_program writer = make_compute("barrier_indirect_writer", with_spin(c_indirect_writer_body), {});
    ASSERT_TRUE(writer.is_valid());
    constexpr std::size_t command_bytes = sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command);
    const std::shared_ptr<erhe::graphics::Buffer> spin     = make_spin_buffer(c_indirect_spin);
    const std::shared_ptr<erhe::graphics::Buffer> unused   = make_host_buffer(16, erhe::graphics::Buffer_usage::storage, "barrier unused");
    const std::shared_ptr<erhe::graphics::Buffer> commands = make_host_buffer(
        command_bytes, erhe::graphics::Buffer_usage::indirect | erhe::graphics::Buffer_usage::storage, "barrier indirect commands"
    );
    const std::array<float, 8>    positions{ -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f };
    const std::array<uint16_t, 6> indices  { 0, 1, 2, 0, 2, 3 };
    const std::shared_ptr<erhe::graphics::Buffer> vertices = make_host_buffer(sizeof(positions), erhe::graphics::Buffer_usage::vertex, "barrier quad vertices");
    const std::shared_ptr<erhe::graphics::Buffer> index_buffer = make_host_buffer(sizeof(indices), erhe::graphics::Buffer_usage::index, "barrier quad indices");
    {
        std::span<std::byte> mapped = vertices->map_bytes(0, sizeof(positions));
        std::memcpy(mapped.data(), positions.data(), sizeof(positions));
        vertices->unmap();
        mapped = index_buffer->map_bytes(0, sizeof(indices));
        std::memcpy(mapped.data(), indices.data(), sizeof(indices));
        index_buffer->unmap();
        mapped = commands->map_bytes(0, command_bytes);
        std::memset(mapped.data(), 0, command_bytes); // index_count 0: draws nothing
        commands->unmap();
    }
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            {
                erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
                bind_compute(encoder, writer, *spin, *unused, *commands);
                encoder.dispatch_compute(1, 1, 1);
            }
            command_buffer.buffer_barrier(*commands, Buffer_usage::storage_write | Buffer_usage::compute_shader, Buffer_usage::indirect);
            record_draw(command_buffer, *vertices, [&](erhe::graphics::Render_command_encoder& encoder) {
                encoder.set_index_buffer(index_buffer.get());
                encoder.set_buffer(erhe::graphics::Buffer_target::draw_indirect, commands.get());
                encoder.multi_draw_indexed_primitives_indirect(
                    erhe::graphics::Primitive_type::triangle,
                    erhe::dataformat::Format::format_16_scalar_uint,
                    0,
                    1,
                    command_bytes
                );
            });
        }
    );
    expect_draw_is_green();
}

// Compute imageStore -> texture_barrier -> sampled read in compute. The first
// barrier (none -> storage) moves the never-accessed image into the storage
// layout.
TEST_F(Resource_barrier_test, compute_storage_image_to_sampled)
{
    ASSERT_TRUE(make_probe(Probe_kind::color_2d, c_heavy_size));
    erhe::graphics::Device& graphics_device = device();
    const std::shared_ptr<erhe::graphics::Texture> image = make_texture(
        Image_usage::storage | Image_usage::sampled, erhe::graphics::Texture_type::texture_2d,
        erhe::dataformat::Format::format_8_vec4_unorm, c_heavy_size, 0, "barrier storage image"
    );
    const erhe::graphics::Bind_group_layout layout{
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                erhe::graphics::Bind_group_layout_binding{ .binding_point = c_spin_binding, .type = erhe::graphics::Binding_type::uniform_buffer, .stage_flags = erhe::graphics::Shader_stage_flags::compute },
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = c_image_binding,
                    .type          = erhe::graphics::Binding_type::storage_image,
                    .name          = "i_image",
                    .glsl_type     = erhe::graphics::Glsl_type::image_2d,
                    .image_format  = "rgba8",
                    .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                }
            },
            .debug_label       = erhe::utility::Debug_label{"barrier storage image layout"},
            .uses_texture_heap = false
        }
    };
    const Compute_program writer = make_compute_program(
        "barrier_storage_image_writer", with_spin(c_storage_image_writer_body),
        { { "TARGET_SIZE", std::to_string(c_heavy_size) } }, {}, { m_spin_block.get() }, layout
    );
    ASSERT_TRUE(writer.is_valid());
    const std::shared_ptr<erhe::graphics::Buffer> spin = make_spin_buffer(c_fragment_spin);

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.texture_barrier(*image, Image_usage::none, Image_usage::storage | Image_usage::compute_shader);
            {
                erhe::graphics::Compute_command_encoder encoder = graphics_device.make_compute_command_encoder(command_buffer);
                encoder.set_bind_group_layout(&layout);
                encoder.set_compute_pipeline(*writer.pipeline);
                encoder.set_buffer(erhe::graphics::Buffer_target::uniform, spin.get(), 0, c_spin_bytes, c_spin_binding);
                encoder.set_storage_image(c_image_binding, *image);
                encoder.dispatch_compute(c_heavy_size / 8, c_heavy_size / 8, 1);
            }
            command_buffer.texture_barrier(
                *image,
                Image_usage::storage | Image_usage::compute_shader,
                Image_usage::sampled | Image_usage::compute_shader
            );
            record_probe(command_buffer, *image);
        }
    );
    expect_probe(expected_pattern_probe(c_heavy_size), "storage image -> sampled");
}

// Render pass color attachment write -> texture_barrier -> sampled read in
// compute.
TEST_F(Resource_barrier_test, color_attachment_to_sampled)
{
    ASSERT_TRUE(make_probe(Probe_kind::color_2d, c_heavy_size));
    const std::shared_ptr<erhe::graphics::Texture> target = make_texture(
        Image_usage::color_attachment | Image_usage::sampled, erhe::graphics::Texture_type::texture_2d,
        erhe::dataformat::Format::format_8_vec4_unorm, c_heavy_size, 0, "barrier color attachment"
    );
    ASSERT_TRUE(make_slow_pass(*target, 0, Slow_target::color));
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            record_slow_pass(command_buffer);
            command_buffer.texture_barrier(*target, Image_usage::color_attachment, Image_usage::sampled | Image_usage::compute_shader);
            record_probe(command_buffer, *target);
        }
    );
    expect_probe(expected_pattern_probe(c_heavy_size), "color attachment -> sampled");
}

// Render pass color attachment write -> the pass's own usage_after
// (sampled | compute_shader, no explicit barrier) -> sampled read in compute:
// the render pass end barrier reaches the stage its qualifier names.
TEST_F(Resource_barrier_test, color_attachment_usage_after_to_compute_sampled)
{
    ASSERT_TRUE(make_probe(Probe_kind::color_2d, c_heavy_size));
    const std::shared_ptr<erhe::graphics::Texture> target = make_texture(
        Image_usage::color_attachment | Image_usage::sampled, erhe::graphics::Texture_type::texture_2d,
        erhe::dataformat::Format::format_8_vec4_unorm, c_heavy_size, 0, "barrier color attachment"
    );
    ASSERT_TRUE(make_slow_pass(*target, 0, Slow_target::color));
    erhe::graphics::Render_pass_attachment_descriptor& attachment = m_slow_pass->descriptor.color_attachments[0];
    attachment.usage_after  = Image_usage::sampled | Image_usage::compute_shader;
    attachment.layout_after = erhe::graphics::Image_layout::shader_read_only_optimal;
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            record_slow_pass(command_buffer);
            record_probe(command_buffer, *target);
        }
    );
    expect_probe(expected_pattern_probe(c_heavy_size), "color attachment usage_after -> compute sampled");
}

// The subresource range overload: the producer renders into layer 1 of a
// two-layer array; layer 0 (never accessed) and layer 1 get their own
// barriers.
TEST_F(Resource_barrier_test, color_attachment_layer_to_sampled)
{
    ASSERT_TRUE(make_probe(Probe_kind::color_2d_array_layer_1, c_heavy_size));
    const std::shared_ptr<erhe::graphics::Texture> target = make_texture(
        Image_usage::color_attachment | Image_usage::sampled, erhe::graphics::Texture_type::texture_2d_array,
        erhe::dataformat::Format::format_8_vec4_unorm, c_heavy_size, 2, "barrier color attachment array"
    );
    ASSERT_TRUE(make_slow_pass(*target, 1, Slow_target::color));
    const erhe::graphics::Texture_subresource_range layer_0{ .base_layer = 0, .layer_count = 1 };
    const erhe::graphics::Texture_subresource_range layer_1{ .base_layer = 1, .layer_count = erhe::graphics::Texture_subresource_range::remaining };
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.texture_barrier(*target, layer_0, Image_usage::none, Image_usage::sampled | Image_usage::compute_shader);
            record_slow_pass(command_buffer);
            command_buffer.texture_barrier(*target, layer_1, Image_usage::color_attachment, Image_usage::sampled | Image_usage::compute_shader);
            record_probe(command_buffer, *target);
        }
    );
    expect_probe(expected_pattern_probe(c_heavy_size), "color attachment layer 1 -> sampled");
}

// Depth attachment write (gl_FragDepth) -> texture_barrier -> sampled depth
// read in compute.
TEST_F(Resource_barrier_test, depth_attachment_to_sampled)
{
    const erhe::dataformat::Format depth_format = find_depth32f_format();
    if (depth_format == erhe::dataformat::Format::format_undefined) {
        GTEST_SKIP() << "no 32-bit float depth format";
    }
    ASSERT_TRUE(make_probe(Probe_kind::depth_2d, c_heavy_size));
    const std::shared_ptr<erhe::graphics::Texture> depth = make_texture(
        Image_usage::depth_stencil_attachment | Image_usage::sampled, erhe::graphics::Texture_type::texture_2d,
        depth_format, c_heavy_size, 0, "barrier depth attachment"
    );
    ASSERT_TRUE(make_slow_pass(*depth, 0, Slow_target::depth));
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            record_slow_pass(command_buffer);
            command_buffer.texture_barrier(*depth, Image_usage::depth_stencil_attachment, Image_usage::sampled | Image_usage::compute_shader);
            record_probe(command_buffer, *depth);
        }
    );
    const int stride = c_heavy_size / c_probe_grid;
    std::vector<uint32_t> expected(c_probe_count);
    for (int gy = 0; gy < c_probe_grid; ++gy) {
        for (int gx = 0; gx < c_probe_grid; ++gx) {
            const uint32_t x     = static_cast<uint32_t>((gx * stride) + stride - 1);
            const uint32_t y     = static_cast<uint32_t>((gy * stride) + stride - 1);
            const uint32_t seed  = x + (y * static_cast<uint32_t>(c_heavy_size));
            const float    value = (cpu_spin(seed, c_fragment_spin) == c_sentinel) ? 1.0f : (static_cast<float>(seed) / 1048576.0f);
            expected[(static_cast<std::size_t>(gy) * c_probe_grid) + static_cast<std::size_t>(gx)] = std::bit_cast<uint32_t>(value);
        }
    }
    expect_probe(expected, "depth attachment -> sampled");
}

// Depth attachment write (gl_FragDepth) in one command buffer ->
// texture_barrier(depth_stencil_attachment, sampled | vertex_shader,
// earlier_submissions) -> sampled depth read in the VERTEX stage in the next
// command buffer (the shape of the editor's shadow texel visualization,
// doc/erhe/shadows.md "Texel visualization barriers").
TEST_F(Resource_barrier_test, earlier_submission_depth_attachment_to_vertex_stage)
{
    const erhe::dataformat::Format depth_format = find_depth32f_format();
    if (depth_format == erhe::dataformat::Format::format_undefined) {
        GTEST_SKIP() << "no 32-bit float depth format";
    }
    ASSERT_TRUE(make_vertex_fetch(c_heavy_size, Fetch_source::depth));
    const std::shared_ptr<erhe::graphics::Texture> depth = make_texture(
        Image_usage::depth_stencil_attachment | Image_usage::sampled, erhe::graphics::Texture_type::texture_2d,
        depth_format, c_heavy_size, 0, "barrier depth attachment"
    );
    ASSERT_TRUE(make_slow_pass(*depth, 0, Slow_target::depth));
    submit_two(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            record_slow_pass(command_buffer);
        },
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.texture_barrier(
                *depth,
                Image_usage::depth_stencil_attachment,
                Image_usage::sampled | Image_usage::vertex_shader,
                erhe::graphics::Barrier_scope::earlier_submissions
            );
            record_vertex_fetch(command_buffer, *depth);
        }
    );
    const int stride = c_heavy_size / c_probe_grid;
    std::vector<uint32_t> expected(c_probe_count);
    for (int gy = 0; gy < c_probe_grid; ++gy) {
        for (int gx = 0; gx < c_probe_grid; ++gx) {
            const uint32_t x     = static_cast<uint32_t>((gx * stride) + stride - 1);
            const uint32_t y     = static_cast<uint32_t>((gy * stride) + stride - 1);
            const uint32_t seed  = x + (y * static_cast<uint32_t>(c_heavy_size));
            const float    value = (cpu_spin(seed, c_fragment_spin) == c_sentinel) ? 1.0f : (static_cast<float>(seed) / 1048576.0f);
            expected[(static_cast<std::size_t>(gy) * c_probe_grid) + static_cast<std::size_t>(gx)] = std::bit_cast<uint32_t>(value);
        }
    }
    expect_vertex_fetch(expected, "earlier submission depth attachment -> vertex stage");
}

// Transfer (copy_from_buffer) -> texture_barrier -> sampled read in compute.
TEST_F(Resource_barrier_test, transfer_to_sampled)
{
    ASSERT_TRUE(make_probe(Probe_kind::color_2d, c_transfer_size));
    erhe::graphics::Device& graphics_device = device();
    const std::shared_ptr<erhe::graphics::Texture> texture = make_texture(
        Image_usage::transfer_dst | Image_usage::sampled, erhe::graphics::Texture_type::texture_2d,
        erhe::dataformat::Format::format_8_vec4_unorm, c_transfer_size, 0, "barrier transfer destination"
    );
    const std::size_t byte_count = static_cast<std::size_t>(c_transfer_size) * static_cast<std::size_t>(c_transfer_size) * 4u;
    const std::shared_ptr<erhe::graphics::Buffer> staging =
        make_host_buffer(byte_count, erhe::graphics::Buffer_usage::transfer_src, "barrier transfer source");
    {
        const std::span<std::byte> mapped = staging->map_bytes(0, byte_count);
        for (int y = 0; y < c_transfer_size; ++y) {
            for (int x = 0; x < c_transfer_size; ++x) {
                const std::size_t offset = ((static_cast<std::size_t>(y) * c_transfer_size) + static_cast<std::size_t>(x)) * 4u;
                mapped[offset + 0] = static_cast<std::byte>(x & 255);
                mapped[offset + 1] = static_cast<std::byte>(y & 255);
                mapped[offset + 2] = static_cast<std::byte>(64);
                mapped[offset + 3] = static_cast<std::byte>(255);
            }
        }
        staging->unmap();
    }

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.texture_barrier(*texture, Image_usage::none, Image_usage::transfer_dst);
            {
                erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);
                blit.copy_from_buffer(
                    erhe::graphics::Buffer_texel_location{
                        .buffer          = staging.get(),
                        .offset          = 0,
                        .bytes_per_row   = static_cast<std::uintptr_t>(c_transfer_size) * 4u,
                        .bytes_per_image = static_cast<std::uintptr_t>(byte_count)
                    },
                    glm::ivec3{c_transfer_size, c_transfer_size, 1},
                    erhe::graphics::Texture_location{.texture = texture.get(), .slice = 0, .level = 0, .origin = glm::ivec3{0, 0, 0}}
                );
            }
            command_buffer.texture_barrier(*texture, Image_usage::transfer_dst, Image_usage::sampled | Image_usage::compute_shader);
            record_probe(command_buffer, *texture);
        }
    );
    const int stride = c_transfer_size / c_probe_grid;
    std::vector<uint32_t> expected(c_probe_count);
    for (int gy = 0; gy < c_probe_grid; ++gy) {
        for (int gx = 0; gx < c_probe_grid; ++gx) {
            const uint32_t x = static_cast<uint32_t>((gx * stride) + stride - 1);
            const uint32_t y = static_cast<uint32_t>((gy * stride) + stride - 1);
            expected[(static_cast<std::size_t>(gy) * c_probe_grid) + static_cast<std::size_t>(gx)] = pack_rgba8(x & 255u, y & 255u, 64u, 255u);
        }
    }
    expect_probe(expected, "transfer -> sampled");
}

// A barrier recorded while the consumer's compute encoder is open, ordering a
// dispatch after an earlier dispatch of the same encoder (Metal 4:
// barrierAfterEncoderStages).
TEST_F(Resource_barrier_test, in_encoder_dispatch_to_dispatch)
{
    check_in_encoder(erhe::graphics::Barrier_scope::command_buffer);
}

// A barrier recorded while the consumer's compute encoder is open, the
// producer in an earlier command buffer (Metal 4: barrierAfterEncoderStages
// and barrierAfterQueueStages).
TEST_F(Resource_barrier_test, in_encoder_earlier_submission)
{
    check_in_encoder(erhe::graphics::Barrier_scope::earlier_submissions);
}

// The consumer is the second encoder of its kind after the barrier: an empty
// compute encoder comes first (Metal 4: a consumer barrier orders only its own
// encoder, so the barrier must also order the later ones).
TEST_F(Resource_barrier_test, color_attachment_to_sampled_second_compute_encoder)
{
    ASSERT_TRUE(make_probe(Probe_kind::color_2d, c_heavy_size));
    const std::shared_ptr<erhe::graphics::Texture> target = make_texture(
        Image_usage::color_attachment | Image_usage::sampled, erhe::graphics::Texture_type::texture_2d,
        erhe::dataformat::Format::format_8_vec4_unorm, c_heavy_size, 0, "barrier color attachment"
    );
    ASSERT_TRUE(make_slow_pass(*target, 0, Slow_target::color));
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            record_slow_pass(command_buffer);
            command_buffer.texture_barrier(*target, Image_usage::color_attachment, Image_usage::sampled | Image_usage::compute_shader);
            record_empty_compute_encoder(command_buffer);
            record_probe(command_buffer, *target);
        }
    );
    expect_probe(expected_pattern_probe(c_heavy_size), "color attachment -> second compute encoder");
}

// Render pass color attachment write -> texture_barrier -> sampled in the
// VERTEX stage of a later render pass, which is the second render encoder
// after the barrier (an empty pass comes first).
TEST_F(Resource_barrier_test, color_attachment_to_vertex_stage_second_render_pass)
{
    ASSERT_TRUE(make_vertex_fetch(c_heavy_size));
    const std::shared_ptr<erhe::graphics::Texture> target = make_texture(
        Image_usage::color_attachment | Image_usage::sampled, erhe::graphics::Texture_type::texture_2d,
        erhe::dataformat::Format::format_8_vec4_unorm, c_heavy_size, 0, "barrier color attachment"
    );
    const std::shared_ptr<erhe::graphics::Texture> unrelated = make_texture(
        Image_usage::color_attachment, erhe::graphics::Texture_type::texture_2d,
        erhe::dataformat::Format::format_8_vec4_unorm, 16, 0, "barrier unrelated attachment"
    );
    ASSERT_TRUE(make_slow_pass(*target, 0, Slow_target::color));
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            record_slow_pass(command_buffer);
            command_buffer.texture_barrier(*target, Image_usage::color_attachment, Image_usage::sampled | Image_usage::vertex_shader);
            record_empty_render_pass(command_buffer, *unrelated);
            record_vertex_fetch(command_buffer, *target);
        }
    );
    expect_vertex_fetch(expected_pattern_probe(c_heavy_size), "color attachment -> vertex stage of the second render pass");
}

// Command_buffer::upload_to_buffer into a range an earlier upload of the same
// command buffer wrote (a live vertex edit writing one vertex twice before a
// flush): the backend's own transfers order their output for the next copy
// into the buffer as well as for its readers, and the last write lands. The
// buffer is a device-local vertex | storage buffer (the mesh vertex pools),
// whose usage names no transfer access; on Vulkan an unordered pair is a
// SYNC-HAZARD-WRITE-AFTER-WRITE the fixture fails the test for.
TEST_F(Resource_barrier_test, upload_after_upload_same_range)
{
    erhe::graphics::Device& graphics_device = device();
    constexpr std::size_t c_word_count  = 16384u;
    constexpr std::size_t c_byte_count  = c_word_count * sizeof(uint32_t);
    constexpr std::size_t c_edit_first  = 4096u;
    constexpr std::size_t c_edit_count  = 3u;     // one float3 position
    const erhe::graphics::Buffer_create_info create_info{
        .capacity_byte_count                = c_byte_count,
        .usage                              = erhe::graphics::Buffer_usage::vertex | erhe::graphics::Buffer_usage::storage,
        .required_memory_property_bit_mask  = erhe::graphics::Memory_property_flag_bit_mask::device_local,
        .preferred_memory_property_bit_mask = erhe::graphics::Memory_property_flag_bit_mask::none,
        .debug_label                        = erhe::utility::Debug_label{"barrier upload destination"}
    };
    const std::shared_ptr<erhe::graphics::Buffer> destination = std::make_shared<erhe::graphics::Buffer>(graphics_device, create_info);
    const std::shared_ptr<erhe::graphics::Buffer> readback    = make_readback_buffer(c_byte_count, "barrier upload readback");

    std::vector<uint32_t> base(c_word_count);
    for (std::size_t i = 0; i < c_word_count; ++i) {
        base[i] = static_cast<uint32_t>(i);
    }
    const std::array<uint32_t, c_edit_count> first_edit {0xa0000001u, 0xa0000002u, 0xa0000003u};
    const std::array<uint32_t, c_edit_count> second_edit{0xb0000001u, 0xb0000002u, 0xb0000003u};
    const std::size_t edit_offset = c_edit_first * sizeof(uint32_t);
    const std::size_t edit_bytes  = c_edit_count * sizeof(uint32_t);

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.upload_to_buffer(*destination, 0, base.data(), c_byte_count);
            command_buffer.upload_to_buffer(*destination, edit_offset, first_edit.data(), edit_bytes);
            command_buffer.upload_to_buffer(*destination, edit_offset, second_edit.data(), edit_bytes);
            command_buffer.buffer_barrier(*destination, Buffer_usage::transfer_dst, Buffer_usage::transfer_src);
            erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);
            blit.copy_from_buffer(destination.get(), 0, readback.get(), 0, c_byte_count);
        }
    );

    const std::vector<std::byte> bytes = read_buffer(*readback, c_byte_count);
    std::vector<uint32_t> words(c_word_count);
    std::memcpy(words.data(), bytes.data(), c_byte_count);
    std::vector<uint32_t> expected = base;
    for (std::size_t i = 0; i < c_edit_count; ++i) {
        expected[c_edit_first + i] = second_edit[i];
    }
    std::size_t mismatch_count = 0;
    for (std::size_t i = 0; i < c_word_count; ++i) {
        if (words[i] != expected[i]) {
            ++mismatch_count;
        }
    }
    EXPECT_EQ(mismatch_count, 0u) << "upload -> upload of the same range";
}

} // namespace erhe::graphics::test
