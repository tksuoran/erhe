#include "renderers/radiance_cascades_renderer.hpp"

#include "app_context.hpp"
#include "config/generated/radiance_cascades_config.hpp"
#include "editor_log.hpp"
#include "renderers/content_bounds.hpp"
#include "renderers/trace_lights.hpp"
#include "scene/scene_root.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/material_buffer.hpp"
#include "erhe_scene_renderer/material_set.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

namespace editor {

namespace {

// rgb radiance, a transparency beta (doc/plans/radiance_cascades.md section 3).
constexpr erhe::dataformat::Format c_radiance_format = erhe::dataformat::Format::format_16_vec4_float;
// Cascade 0 signed hit distance per raw texel. Full float: one channel per
// cascade 0 texel is small, and the readback is a plain memcpy.
constexpr erhe::dataformat::Format c_distance_format = erhe::dataformat::Format::format_32_scalar_float;

// Raw binding points of the trace bind group layout, as in Ddgi_renderer:
// 0 and 1 are the shared material / light block binding points
// (erhe_scene_renderer), 2 the control block, 3 the instance records, then
// the acceleration structure and the two storage images (raw bindings are
// not offset past the buffer bindings).
constexpr unsigned int c_control_binding_point         = 2;
constexpr unsigned int c_instance_record_binding_point = 3;

// Merge pass layout: the control block, the raw atlas of the cascade and
// the merged atlas of the cascade above as combined image samplers (user
// points 0 / 1; Vulkan offsets samplers past the highest buffer binding,
// 2, so they land at 3 / 4), and the merged atlas written as the storage
// image at raw binding point 5.
constexpr unsigned int c_merge_raw_binding_point    = 0;
constexpr unsigned int c_merge_upper_binding_point  = 1;
constexpr unsigned int c_merge_output_binding_point = 5;
constexpr int          c_merge_workgroup_size       = 8; // rc_merge.comp local size, both axes

// rc_merge.comp params.y flags
constexpr uint32_t c_merge_flag_top            = 1u; // top cascade: merge with the sky
constexpr uint32_t c_merge_flag_mask_radiance  = 2u; // debug_cascade_mask: zero the cascade's radiance

// Preview pass layout: the control block, then the previewed (raw or
// merged) atlas, the distance texture and the preview output as storage
// images.
constexpr unsigned int c_preview_atlas_binding_point    = 3;
constexpr unsigned int c_preview_distance_binding_point = 4;
constexpr unsigned int c_preview_output_binding_point   = 5;
constexpr int          c_preview_workgroup_size         = 8; // rc_preview.comp local size, both axes

// One thread per texel.
constexpr int     c_trace_workgroup_size = 64;
// Largest texel run of one dispatch: the Vulkan-guaranteed minimum of
// maxComputeWorkGroupCount[0] (65535) workgroups.
constexpr int64_t c_max_texels_per_dispatch = int64_t{65535} * c_trace_workgroup_size;

// rc_trace.comp dispatch.w flags.
constexpr uint32_t c_flag_blend = 1u;

// Readback buffer offsets are rounded up to this (a multiple of every texel
// size and of every nonCoherentAtomSize the Vulkan spec allows).
constexpr std::size_t c_readback_alignment = 256;

[[nodiscard]] auto round_up(const std::size_t value, const std::size_t multiple) -> std::size_t
{
    return ((value + multiple - 1) / multiple) * multiple;
}

[[nodiscard]] auto shader_paths() -> std::vector<std::filesystem::path>
{
    return {
        std::filesystem::path{"res"} / std::filesystem::path{"shaders"},
        std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"}
    };
}

} // anonymous namespace

Radiance_cascades_renderer::Radiance_cascades_renderer(
    erhe::graphics::Device&                  graphics_device,
    erhe::graphics::Command_buffer&          init_command_buffer,
    App_context&                             context,
    erhe::scene_renderer::Program_interface& program_interface,
    erhe::scene_renderer::Mesh_memory&       mesh_memory,
    const Radiance_cascades_config&          config,
    const Producer_selection                 selection
)
    : m_graphics_device{graphics_device}
    , m_context        {context}
    , m_config         {config}
    , m_selection      {selection}
    , m_control_block{
        graphics_device,
        "rc_trace",
        static_cast<int>(c_control_binding_point),
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , m_merge_block{
        graphics_device,
        "rc_merge",
        static_cast<int>(c_control_binding_point),
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , m_preview_block{
        graphics_device,
        "rc_preview",
        static_cast<int>(c_control_binding_point),
        erhe::graphics::Shader_resource::Type::uniform_block
    }
{
    using namespace erhe::graphics;

    // Ray query gates the whole feature, as for DDGI: the interval trace has
    // no rasterized fallback (doc/plans/radiance_cascades.md section 11).
    if (!graphics_device.get_info().use_ray_query) {
        log_startup->info("Radiance_cascades_renderer: ray query not available, radiance cascades disabled");
        return;
    }
    const bool use_position_fetch = graphics_device.get_info().use_ray_tracing_position_fetch;

    const std::filesystem::path editor_shaders = std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"};

    m_scene_tlas = std::make_unique<Scene_tlas>(
        graphics_device,
        mesh_memory,
        c_instance_record_binding_point,
        "Radiance_cascades_renderer"
    );

    // Control block (std140), one per dispatch: the cascade and the texel
    // run the dispatch traces.
    // xyz = cascade grid origin, w = interval start t_i
    m_control_offsets.grid_origin  = m_control_block.add_vec4 ("grid_origin" )->get_offset_in_parent();
    // xyz = cascade grid spacing, w = interval end t_{i+1}
    m_control_offsets.grid_spacing = m_control_block.add_vec4 ("grid_spacing")->get_offset_in_parent();
    // xyz = probe counts, w = octahedral tile side q_i
    m_control_offsets.grid_counts  = m_control_block.add_uvec4("grid_counts" )->get_offset_in_parent();
    // x = first texel of the run, y = texel count, z = tiles per atlas row,
    // w = flags (c_flag_blend)
    m_control_offsets.dispatch     = m_control_block.add_uvec4("dispatch"    )->get_offset_in_parent();
    // x = hysteresis
    m_control_offsets.params       = m_control_block.add_vec4 ("params"      )->get_offset_in_parent();

    // Stream-1 attribute offsets (in uints) for the shared hit path's manual
    // vertex fetch, derived from the Mesh_memory vertex format exactly as
    // Ddgi_renderer does.
    const erhe::dataformat::Vertex_format&   vertex_format = mesh_memory.vertex_format_not_skinned;
    const erhe::dataformat::Attribute_stream normal        = vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::normal,    erhe::dataformat::normal_attribute);
    const erhe::dataformat::Attribute_stream tangent       = vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::tangent,   0);
    const erhe::dataformat::Attribute_stream texcoord0     = vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::tex_coord, 0);
    const erhe::dataformat::Attribute_stream color0        = vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::color,     0);
    ERHE_VERIFY((normal   .attribute != nullptr) && (normal   .stream != nullptr));
    ERHE_VERIFY((tangent  .attribute != nullptr) && (tangent  .stream != nullptr));
    ERHE_VERIFY((texcoord0.attribute != nullptr) && (texcoord0.stream != nullptr));
    ERHE_VERIFY((color0   .attribute != nullptr) && (color0   .stream != nullptr));

    auto to_binding_type = [](const Shader_resource& block) -> Binding_type {
        return (block.get_type() == Shader_resource::Type::shader_storage_block)
            ? Binding_type::storage_buffer
            : Binding_type::uniform_buffer;
    };
    m_tlas_binding_point     = c_instance_record_binding_point + 1;
    m_raw_binding_point      = c_instance_record_binding_point + 2;
    m_distance_binding_point = c_instance_record_binding_point + 3;
    m_trace_bind_group_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                {
                    .binding_point = material_buffer_binding_point,
                    .type          = to_binding_type(program_interface.material_interface.material_block),
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = light_buffer_binding_point,
                    .type          = to_binding_type(program_interface.light_interface.light_block),
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = c_control_binding_point,
                    .type          = Binding_type::uniform_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = c_instance_record_binding_point,
                    .type          = Binding_type::storage_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = m_tlas_binding_point,
                    .type          = Binding_type::acceleration_structure,
                    .name          = "s_tlas",
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = m_raw_binding_point,
                    .type          = Binding_type::storage_image,
                    .name          = "i_rc_raw",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba16f",
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = m_distance_binding_point,
                    .type          = Binding_type::storage_image,
                    .name          = "i_rc_distance",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "r32f",
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label       = "RC trace",
            .uses_texture_heap = true
        }
    );

    std::vector<Shader_stage_extension> extensions{
        { Shader_type::compute_shader, "GL_EXT_ray_query" },
        { Shader_type::compute_shader, "GL_EXT_buffer_reference" },
        { Shader_type::compute_shader, "GL_EXT_buffer_reference_uvec2" }
    };
    if (use_position_fetch) {
        extensions.push_back({ Shader_type::compute_shader, "GL_EXT_ray_tracing_position_fetch" });
    }
    // Two variants of rc_trace.comp: cascade 0 dispatches also write the
    // distance texture, the others do not reference it at all. A dispatch
    // that statically uses a storage image counts as writing it, so a shared
    // variant would make every cascade's dispatch a write of the distance
    // texture, and consecutive dispatches of one frame would be
    // write-after-write hazards although only cascade 0 writes it.
    const auto make_trace_pass = [&](Trace_pass& pass, const char* write_distance_define, const char* name) {
        pass.shader_stages = std::make_unique<Reloadable_shader_stages>(
            graphics_device,
            Shader_stages_create_info{
                .name                = name,
                .defines             = {
                    { "ERHE_TLAS_BINDING",            fmt::format("{}", m_tlas_binding_point) },
                    { "ERHE_RT_NORMAL_OFFSET",        fmt::format("{}", normal   .attribute->offset / 4) },
                    { "ERHE_RT_TANGENT_OFFSET",       fmt::format("{}", tangent  .attribute->offset / 4) },
                    { "ERHE_RT_TEXCOORD0_OFFSET",     fmt::format("{}", texcoord0.attribute->offset / 4) },
                    { "ERHE_RT_COLOR0_OFFSET",        fmt::format("{}", color0   .attribute->offset / 4) },
                    { "ERHE_RT_HAS_POSITION_FETCH",   use_position_fetch ? "1" : "0" },
                    { "ERHE_RC_TRACE_GROUP_SIZE",     fmt::format("{}", c_trace_workgroup_size) },
                    { "ERHE_RC_TRACE_WRITE_DISTANCE", write_distance_define }
                },
                .extensions          = extensions,
                .struct_types        = {
                    &program_interface.material_interface.material_struct,
                    &program_interface.light_interface.light_struct,
                    &m_scene_tlas->get_instance_struct()
                },
                .interface_blocks    = {
                    &program_interface.material_interface.material_block,
                    &program_interface.light_interface.light_block,
                    &m_control_block,
                    &m_scene_tlas->get_instance_block()
                },
                .shaders             = { { Shader_type::compute_shader, editor_shaders / "rc_trace.comp" } },
                .extra_include_paths = shader_paths(),
                .bind_group_layout   = m_trace_bind_group_layout.get()
            }
        );
        graphics_device.get_shader_monitor().add(*pass.shader_stages);
        pass.pipeline = std::make_unique<Compute_pipeline>(
            graphics_device,
            Compute_pipeline_data{
                .name              = name,
                .shader_stages     = &pass.shader_stages->shader_stages,
                .bind_group_layout = m_trace_bind_group_layout.get()
            }
        );
    };
    make_trace_pass(m_trace_cascade0, "1", "rc_trace_cascade0");
    make_trace_pass(m_trace_upper,    "0", "rc_trace");

    m_light_buffer = std::make_unique<erhe::scene_renderer::Light_buffer>(
        graphics_device,
        init_command_buffer,
        program_interface.light_interface
    );
    m_light_projections = std::make_unique<erhe::scene_renderer::Light_projections>();
    m_control_buffer = std::make_unique<Ring_buffer_client>(
        graphics_device,
        Buffer_target::uniform,
        "Radiance_cascades_renderer::control",
        c_control_binding_point
    );

    // Merge: grid_counts xyz = cascade probe counts, w = tile side q_i;
    // upper_counts xyz = upper cascade probe counts, w = upper tiles per
    // atlas row; params x = tiles per atlas row, y = flags, z, w = atlas
    // size; sky rgb = sky radiance (zero when masked).
    m_merge_offsets.grid_counts  = m_merge_block.add_uvec4("grid_counts" )->get_offset_in_parent();
    m_merge_offsets.upper_counts = m_merge_block.add_uvec4("upper_counts")->get_offset_in_parent();
    m_merge_offsets.params       = m_merge_block.add_uvec4("params"      )->get_offset_in_parent();
    m_merge_offsets.sky          = m_merge_block.add_vec4 ("sky"         )->get_offset_in_parent();
    // texelFetch only: the filter is never used.
    m_merge_sampler = std::make_unique<Sampler>(
        graphics_device,
        Sampler_create_info{
            .min_filter  = Filter::nearest,
            .mag_filter  = Filter::nearest,
            .mipmap_mode = Sampler_mipmap_mode::not_mipmapped,
            .debug_label = "RC merge sampler"
        }
    );
    m_merge_bind_group_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                {
                    .binding_point = c_control_binding_point,
                    .type          = Binding_type::uniform_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point   = c_merge_raw_binding_point,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_rc_raw",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                {
                    .binding_point   = c_merge_upper_binding_point,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_rc_upper",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                {
                    .binding_point = c_merge_output_binding_point,
                    .type          = Binding_type::storage_image,
                    .name          = "i_rc_merged",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba16f",
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label       = "RC merge",
            .uses_texture_heap = false
        }
    );
    m_merge_shader_stages = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name                = "rc_merge",
            .interface_blocks    = { &m_merge_block },
            .shaders             = { { Shader_type::compute_shader, editor_shaders / "rc_merge.comp" } },
            .extra_include_paths = shader_paths(),
            .bind_group_layout   = m_merge_bind_group_layout.get()
        }
    );
    graphics_device.get_shader_monitor().add(*m_merge_shader_stages);
    m_merge_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "rc_merge",
            .shader_stages     = &m_merge_shader_stages->shader_stages,
            .bind_group_layout = m_merge_bind_group_layout.get()
        }
    );

    // Atlas preview: size x, y = atlas size, z = channel; params x = radiance
    // scale, y = r0 (distance normalization).
    m_preview_size_offset   = m_preview_block.add_uvec4("size"  )->get_offset_in_parent();
    m_preview_params_offset = m_preview_block.add_vec4 ("params")->get_offset_in_parent();
    m_preview_bind_group_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                {
                    .binding_point = c_control_binding_point,
                    .type          = Binding_type::uniform_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = c_preview_atlas_binding_point,
                    .type          = Binding_type::storage_image,
                    .name          = "i_rc_atlas",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba16f",
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = c_preview_distance_binding_point,
                    .type          = Binding_type::storage_image,
                    .name          = "i_rc_distance",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "r32f",
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = c_preview_output_binding_point,
                    .type          = Binding_type::storage_image,
                    .name          = "i_rc_preview",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba16f",
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label = "RC preview"
        }
    );
    m_preview_shader_stages = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name                = "rc_preview",
            .interface_blocks    = { &m_preview_block },
            .shaders             = { { Shader_type::compute_shader, editor_shaders / "rc_preview.comp" } },
            .extra_include_paths = shader_paths(),
            .bind_group_layout   = m_preview_bind_group_layout.get()
        }
    );
    graphics_device.get_shader_monitor().add(*m_preview_shader_stages);
    m_preview_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "rc_preview",
            .shader_stages     = &m_preview_shader_stages->shader_stages,
            .bind_group_layout = m_preview_bind_group_layout.get()
        }
    );

    // The labels double as the Performance window plot names.
    m_pass_timings[static_cast<std::size_t>(Rc_pass::trace)].timer = std::make_unique<Gpu_timer>(graphics_device, "RC trace");
    m_pass_timings[static_cast<std::size_t>(Rc_pass::merge)].timer = std::make_unique<Gpu_timer>(graphics_device, "RC merge");

    m_supported = true;
    log_startup->info("Radiance_cascades_renderer: radiance cascades available");
}

Radiance_cascades_renderer::~Radiance_cascades_renderer() noexcept = default;

auto Radiance_cascades_renderer::is_supported() const -> bool
{
    return
        m_supported &&
        (m_trace_cascade0.pipeline != nullptr) &&
        (m_trace_upper.pipeline != nullptr) &&
        (m_merge_pipeline != nullptr);
}

auto Radiance_cascades_renderer::is_selected() const -> bool
{
    return m_selection == Producer_selection::selected;
}

void Radiance_cascades_renderer::set_selection(const Producer_selection selection)
{
    if (selection == m_selection) {
        return;
    }
    m_selection = selection;
    if (selection == Producer_selection::deselected) {
        // Release the cascade memory while another source is selected; the
        // layout is refitted when the source is selected again.
        release_textures();
        m_layout = Radiance_cascades_layout{};
        m_volume_bounds.reset();
        clear_pass_timings();
    }
}

auto Radiance_cascades_renderer::is_active() const -> bool
{
    return is_supported() && is_selected() && m_layout.is_valid() && static_cast<bool>(m_cascade_textures[0].raw);
}

auto Radiance_cascades_renderer::has_field() const -> bool
{
    return false;
}

auto Radiance_cascades_renderer::get_layout() const -> const Radiance_cascades_layout&
{
    return m_layout;
}

auto Radiance_cascades_renderer::get_cascade_textures(const int cascade) const -> const Cascade_textures&
{
    ERHE_VERIFY((cascade >= 0) && (cascade < c_max_radiance_cascades));
    return m_cascade_textures[static_cast<std::size_t>(cascade)];
}

auto Radiance_cascades_renderer::get_distance_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&
{
    return m_distance_texture;
}

auto Radiance_cascades_renderer::get_cascade_texture_byte_count(const int cascade) const -> std::size_t
{
    ERHE_VERIFY((cascade >= 0) && (cascade < c_max_radiance_cascades));
    return m_cascade_byte_counts[static_cast<std::size_t>(cascade)];
}

auto Radiance_cascades_renderer::get_texture_byte_count() const -> std::size_t
{
    return m_texture_byte_count;
}

auto Radiance_cascades_renderer::get_fit_count() const -> uint64_t
{
    return m_fit_count;
}

void Radiance_cascades_renderer::sample_pass_timings()
{
    // A timer's result is the latest completed measurement of an earlier
    // update; all of them read 0 until the first one has completed. As for
    // DDGI, a pass that overlaps its predecessor and finishes first may be
    // charged nothing (doc/editor/ddgi.md "Performance").
    std::array<uint64_t, c_rc_pass_count> results_ns{};
    uint64_t                              sum_ns = 0;
    for (std::size_t i = 0; i < c_rc_pass_count; ++i) {
        results_ns[i] = m_pass_timings[i].timer->last_result();
        sum_ns += results_ns[i];
    }
    if (sum_ns == 0) {
        return;
    }
    for (std::size_t i = 0; i < c_rc_pass_count; ++i) {
        Pass_timing&   pass_timing = m_pass_timings[i];
        const uint64_t ns          = results_ns[i];
        pass_timing.last_ns = ns;
        pass_timing.history_ns[pass_timing.history_next] = ns;
        pass_timing.history_next  = (pass_timing.history_next + 1) % c_timing_history_size;
        pass_timing.history_count = std::min(pass_timing.history_count + 1, c_timing_history_size);
    }
    ++m_timing_sample_count;
}

void Radiance_cascades_renderer::clear_pass_timings()
{
    for (Pass_timing& pass_timing : m_pass_timings) {
        pass_timing.history_count = 0;
        pass_timing.history_next  = 0;
        pass_timing.last_ns       = 0;
    }
}

auto Radiance_cascades_renderer::get_stats() const -> Stats
{
    Stats stats{};
    const auto pass_time = [this](const Rc_pass pass) -> Pass_time {
        const Pass_timing& pass_timing = m_pass_timings[static_cast<std::size_t>(pass)];
        uint64_t sum_ns = 0;
        for (std::size_t i = 0; i < pass_timing.history_count; ++i) {
            sum_ns += pass_timing.history_ns[i];
        }
        Pass_time result{};
        result.last_ms    = static_cast<double>(pass_timing.last_ns) * 1.0e-6;
        result.average_ms = (pass_timing.history_count > 0)
            ? (static_cast<double>(sum_ns) * 1.0e-6) / static_cast<double>(pass_timing.history_count)
            : 0.0;
        return result;
    };
    stats.trace            = pass_time(Rc_pass::trace);
    stats.merge            = pass_time(Rc_pass::merge);
    stats.total.last_ms    = stats.trace.last_ms    + stats.merge.last_ms;
    stats.total.average_ms = stats.trace.average_ms + stats.merge.average_ms;
    stats.update_count        = m_update_count;
    stats.timing_sample_count = m_timing_sample_count;
    stats.completed_sweeps    = m_completed_sweeps;
    stats.texels_per_update   = m_texels_per_update;
    stats.rays_per_update     = m_texels_per_update;
    const int64_t total_texels = m_layout.get_total_texels();
    stats.updates_per_full_refresh = (m_texels_per_update > 0)
        ? ((total_texels + m_texels_per_update - 1) / m_texels_per_update)
        : 0;
    stats.ms_per_million_rays = (stats.rays_per_update > 0)
        ? (stats.total.average_ms * 1.0e6) / static_cast<double>(stats.rays_per_update)
        : 0.0;
    stats.full_refresh_ms = static_cast<double>(stats.updates_per_full_refresh) * stats.total.average_ms;
    return stats;
}

void Radiance_cascades_renderer::release_textures()
{
    for (Cascade_textures& textures : m_cascade_textures) {
        textures.raw.reset();
        textures.merged.reset();
    }
    m_distance_texture.reset();
    m_preview_texture.reset();
    m_cascade_byte_counts.fill(0);
    m_cascade_texel_offsets.fill(0);
    m_texture_byte_count = 0;
    m_texel_cursor       = 0;
    m_completed_sweeps   = 0;
    m_texels_per_update  = 0;
}

void Radiance_cascades_renderer::allocate_textures(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    release_textures();
    const auto make_texture = [&](const std::string& debug_label, const erhe::dataformat::Format format, const int width, const int height) -> std::shared_ptr<Texture> {
        std::shared_ptr<Texture> texture = std::make_shared<Texture>(
            m_graphics_device,
            Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  = Image_usage_flag_bit_mask::storage      |
                               Image_usage_flag_bit_mask::sampled      |
                               Image_usage_flag_bit_mask::transfer_dst |
                               Image_usage_flag_bit_mask::transfer_src,
                .type        = Texture_type::texture_2d,
                .pixelformat = format,
                .width       = width,
                .height      = height,
                .level_count = 1,
                .debug_label = erhe::utility::Debug_label{debug_label}
            }
        );
        // Zero radiance, zero transparency: every reader sees defined data
        // until the trace and merge passes write the atlases.
        command_buffer.clear_texture(*texture, {0.0, 0.0, 0.0, 0.0});
        command_buffer.transition_texture_layout(*texture, Image_layout::shader_read_only_optimal);
        return texture;
    };

    const std::size_t radiance_texel_bytes = erhe::dataformat::get_format_size_bytes(c_radiance_format);
    int64_t texel_offset = 0;
    for (int i = 0; i < m_layout.cascade_count; ++i) {
        const Radiance_cascade& cascade = m_layout.cascades[static_cast<std::size_t>(i)];
        const int width  = cascade.get_atlas_width();
        const int height = cascade.get_atlas_height();
        Cascade_textures& textures = m_cascade_textures[static_cast<std::size_t>(i)];
        textures.raw    = make_texture(fmt::format("RC cascade {} raw",    i), c_radiance_format, width, height);
        textures.merged = make_texture(fmt::format("RC cascade {} merged", i), c_radiance_format, width, height);
        const std::size_t bytes = 2 * static_cast<std::size_t>(cascade.get_atlas_texel_count()) * radiance_texel_bytes;
        m_cascade_byte_counts[static_cast<std::size_t>(i)] = bytes;
        m_texture_byte_count += bytes;
        m_cascade_texel_offsets[static_cast<std::size_t>(i)] = texel_offset;
        texel_offset += cascade.get_texel_count();
    }
    {
        const Radiance_cascade& cascade0 = m_layout.cascades[0];
        m_distance_texture = make_texture("RC cascade 0 distance", c_distance_format, cascade0.get_atlas_width(), cascade0.get_atlas_height());
        const std::size_t bytes = static_cast<std::size_t>(cascade0.get_atlas_texel_count()) * erhe::dataformat::get_format_size_bytes(c_distance_format);
        m_cascade_byte_counts[0] += bytes;
        m_texture_byte_count     += bytes;
    }

    // Refits happen at runtime (content moved, settings changed), so this is
    // a render-log event, not a startup one.
    const Radiance_cascade& cascade0 = m_layout.cascades[0];
    log_render->info(
        "Radiance_cascades_renderer: {} cascades, cascade 0 {}x{}x{} probes, spacing {:.3f} {:.3f} {:.3f} m, q0 {}, r0 {:.3f} m, {} probes / {} texels total, {:.1f} MB",
        m_layout.cascade_count,
        cascade0.grid.counts.x, cascade0.grid.counts.y, cascade0.grid.counts.z,
        cascade0.grid.spacing.x, cascade0.grid.spacing.y, cascade0.grid.spacing.z,
        cascade0.tile_texels, m_layout.r0,
        m_layout.get_total_probes(), m_layout.get_total_texels(),
        static_cast<double>(m_texture_byte_count) / (1024.0 * 1024.0)
    );
}

auto Radiance_cascades_renderer::update_layout(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root) -> bool
{
    const Radiance_cascades_layout_settings settings{
        .probe_spacing_m      = std::max(0.01f, m_config.probe_spacing_m),
        .max_probes_cascade0  = std::max(8,     m_config.max_probes_cascade0),
        .max_cascades         = std::clamp(m_config.max_cascades, 1, c_max_radiance_cascades),
        .cascade0_tile_texels = std::clamp(m_config.cascade0_tile_texels, 1, 64),
        .interval_scale       = std::max(1.0f,  m_config.interval_scale),
        .max_texture_size     = m_graphics_device.get_info().max_texture_size
    };
    const float padding_m = std::max(0.0f, m_config.volume_padding_m);

    const erhe::math::Aabb bounds = compute_padded_content_bounds(scene_root, padding_m);
    if (!bounds.is_valid()) {
        return false;
    }

    const bool settings_changed =
        (settings  != m_fit_settings ) ||
        (padding_m != m_fit_padding_m) ||
        !m_layout.is_valid() ||
        !m_cascade_textures[0].raw;
    const bool bounds_changed = m_volume_bounds.content_changed(bounds);
    if (!settings_changed && !bounds_changed) {
        return true;
    }

    const Volume_refit_cause       cause      = settings_changed ? Volume_refit_cause::settings : Volume_refit_cause::content;
    const erhe::math::Aabb         fit_bounds = m_volume_bounds.get_fit_bounds(bounds, cause);
    const Radiance_cascades_layout layout     = fit_radiance_cascades(fit_bounds, settings);
    if (!layout.is_valid()) {
        return false;
    }

    m_volume_bounds.set(fit_bounds);
    m_fit_settings  = settings;
    m_fit_padding_m = padding_m;
    m_layout        = layout;
    ++m_fit_count;
    allocate_textures(command_buffer);
    return true;
}

void Radiance_cascades_renderer::record_trace(
    erhe::graphics::Command_buffer&          command_buffer,
    const Scene_tlas::Frame&                 tlas_frame,
    const erhe::graphics::Ring_buffer_range& light_range,
    erhe::scene_renderer::Material_set&      material_set
)
{
    using namespace erhe::graphics;

    const int64_t total_texels = m_layout.get_total_texels();
    // The budget is read every tick, so a change takes effect on the next
    // frame without a refit; clamped so one update never traces a texel
    // twice.
    m_texels_per_update = std::clamp(static_cast<int64_t>(m_config.texels_per_frame), int64_t{1}, total_texels);
    const float hysteresis = std::clamp(m_config.hysteresis, 0.0f, 0.999f);

    for (int i = 0; i < m_layout.cascade_count; ++i) {
        command_buffer.transition_texture_layout(*m_cascade_textures[static_cast<std::size_t>(i)].raw, Image_layout::general);
    }
    command_buffer.transition_texture_layout(*m_distance_texture, Image_layout::general);

    {
        const Scoped_gpu_timer trace_timer{*m_pass_timings[static_cast<std::size_t>(Rc_pass::trace)].timer, command_buffer};

        // Walk the cursor over the global texel order (cascade 0 first),
        // one dispatch per contiguous run of one cascade. Runs are disjoint,
        // but a dispatch declares a write of its whole raw atlas (and, for
        // cascade 0, of the distance texture), so a second run of a cascade
        // in the same frame - the cursor wrapping into the cascade it started
        // in, or a run split at the dispatch size limit - is ordered after
        // the first by a barrier. Runs of different cascades write different
        // images and need none.
        std::array<bool, c_max_radiance_cascades> dispatched{};
        int64_t remaining = m_texels_per_update;
        while (remaining > 0) {
            int cascade_index = m_layout.cascade_count - 1;
            while ((cascade_index > 0) && (m_cascade_texel_offsets[static_cast<std::size_t>(cascade_index)] > m_texel_cursor)) {
                --cascade_index;
            }
            const Radiance_cascade& cascade     = m_layout.cascades[static_cast<std::size_t>(cascade_index)];
            const int64_t           first_texel = m_texel_cursor - m_cascade_texel_offsets[static_cast<std::size_t>(cascade_index)];
            const int64_t           count       = std::min({remaining, cascade.get_texel_count() - first_texel, c_max_texels_per_dispatch});

            if (dispatched[static_cast<std::size_t>(cascade_index)]) {
                command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
            }
            dispatched[static_cast<std::size_t>(cascade_index)] = true;

            // First sweep since the allocation: every texel is new, and its
            // history is the allocation clear, not a trace.
            const uint32_t flags = (m_completed_sweeps > 0) ? c_flag_blend : 0u;

            const std::size_t byte_count = m_control_block.get_size_bytes();
            Ring_buffer_range control_range = m_control_buffer->acquire(Ring_buffer_usage::CPU_write, byte_count);
            {
                std::span<std::byte> gpu_data = control_range.get_span();
                std::memset(gpu_data.data(), 0, byte_count);
                const glm::vec4  grid_origin {cascade.grid.origin,  cascade.interval_start};
                const glm::vec4  grid_spacing{cascade.grid.spacing, cascade.interval_end};
                const glm::uvec4 grid_counts{
                    static_cast<uint32_t>(cascade.grid.counts.x),
                    static_cast<uint32_t>(cascade.grid.counts.y),
                    static_cast<uint32_t>(cascade.grid.counts.z),
                    static_cast<uint32_t>(cascade.tile_texels)
                };
                const glm::uvec4 dispatch{
                    static_cast<uint32_t>(first_texel),
                    static_cast<uint32_t>(count),
                    static_cast<uint32_t>(cascade.tiles_per_row),
                    flags
                };
                const glm::vec4 params{hysteresis, 0.0f, 0.0f, 0.0f};
                write(gpu_data, m_control_offsets.grid_origin,  as_span(grid_origin ));
                write(gpu_data, m_control_offsets.grid_spacing, as_span(grid_spacing));
                write(gpu_data, m_control_offsets.grid_counts,  as_span(grid_counts ));
                write(gpu_data, m_control_offsets.dispatch,     as_span(dispatch    ));
                write(gpu_data, m_control_offsets.params,       as_span(params      ));
                control_range.bytes_written(byte_count);
                control_range.close();
            }
            {
                const Trace_pass&       pass    = (cascade_index == 0) ? m_trace_cascade0 : m_trace_upper;
                Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
                encoder.set_bind_group_layout(m_trace_bind_group_layout.get());
                encoder.set_compute_pipeline(*pass.pipeline);
                m_light_buffer->bind_light_buffer(encoder, light_range);
                m_scene_tlas->bind_instance_records(encoder, tlas_frame);
                encoder.set_acceleration_structure(m_tlas_binding_point, *tlas_frame.acceleration_structure);
                m_control_buffer->bind(encoder, control_range);
                encoder.set_storage_image(m_raw_binding_point, *m_cascade_textures[static_cast<std::size_t>(cascade_index)].raw);
                // Bound for every dispatch (the layout has the binding); only
                // the cascade 0 variant references it.
                encoder.set_storage_image(m_distance_binding_point, *m_distance_texture);
                material_set.bind(encoder);
                encoder.dispatch_compute(
                    static_cast<std::uintptr_t>((count + c_trace_workgroup_size - 1) / c_trace_workgroup_size),
                    1,
                    1
                );
            }
            control_range.release();

            remaining      -= count;
            m_texel_cursor += count;
            if (m_texel_cursor >= total_texels) {
                m_texel_cursor = 0;
                ++m_completed_sweeps;
            }
        }
    }

    // The raw atlases become sampled textures for the merge pass.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    for (int i = 0; i < m_layout.cascade_count; ++i) {
        command_buffer.transition_texture_layout(*m_cascade_textures[static_cast<std::size_t>(i)].raw, Image_layout::shader_read_only_optimal);
    }
    command_buffer.transition_texture_layout(*m_distance_texture, Image_layout::shader_read_only_optimal);
}

void Radiance_cascades_renderer::record_merge(erhe::graphics::Command_buffer& command_buffer, const glm::vec3& sky_radiance)
{
    using namespace erhe::graphics;

    // Masked cascades contribute their transparency but no radiance
    // (doc/editor/radiance_cascades.md "Merge"). Read every tick like the
    // trace budget, so an edit takes effect on the next frame.
    const uint32_t  mask = static_cast<uint32_t>(std::max(0, m_config.debug_cascade_mask));
    const glm::vec3 sky  = ((mask & (1u << c_sky_mask_bit)) != 0u) ? glm::vec3{0.0f} : sky_radiance;

    const Scoped_gpu_timer merge_timer{*m_pass_timings[static_cast<std::size_t>(Rc_pass::merge)].timer, command_buffer};

    // Top cascade first: cascade i reads the merged atlas of cascade i + 1
    // written by the previous dispatch. Every dispatch has exactly one
    // write (its own merged atlas, in general layout); the raw atlases (left
    // shader_read_only_optimal by the trace) and the upper merged atlas
    // (moved to shader_read_only_optimal after its dispatch, which orders
    // the write before the next dispatch's reads) are sampled read-only.
    for (int i = m_layout.cascade_count - 1; i >= 0; --i) {
        const std::size_t       index   = static_cast<std::size_t>(i);
        const Radiance_cascade& cascade = m_layout.cascades[index];
        const bool              is_top  = (i == (m_layout.cascade_count - 1));
        // The top cascade has no upper atlas; its raw atlas stands in for
        // the unused upper binding.
        const Radiance_cascade& upper   = is_top ? cascade : m_layout.cascades[index + 1];
        Texture&                raw     = *m_cascade_textures[index].raw;
        Texture&                merged  = *m_cascade_textures[index].merged;
        const Texture&          upper_merged = is_top ? raw : *m_cascade_textures[index + 1].merged;

        uint32_t flags = 0u;
        if (is_top) {
            flags |= c_merge_flag_top;
        }
        if ((mask & (1u << static_cast<uint32_t>(i))) != 0u) {
            flags |= c_merge_flag_mask_radiance;
        }
        const int width  = cascade.get_atlas_width();
        const int height = cascade.get_atlas_height();

        const std::size_t byte_count = m_merge_block.get_size_bytes();
        Ring_buffer_range control_range = m_control_buffer->acquire(Ring_buffer_usage::CPU_write, byte_count);
        {
            std::span<std::byte> gpu_data = control_range.get_span();
            std::memset(gpu_data.data(), 0, byte_count);
            const glm::uvec4 grid_counts{
                static_cast<uint32_t>(cascade.grid.counts.x),
                static_cast<uint32_t>(cascade.grid.counts.y),
                static_cast<uint32_t>(cascade.grid.counts.z),
                static_cast<uint32_t>(cascade.tile_texels)
            };
            const glm::uvec4 upper_counts{
                static_cast<uint32_t>(upper.grid.counts.x),
                static_cast<uint32_t>(upper.grid.counts.y),
                static_cast<uint32_t>(upper.grid.counts.z),
                static_cast<uint32_t>(upper.tiles_per_row)
            };
            const glm::uvec4 params{
                static_cast<uint32_t>(cascade.tiles_per_row),
                flags,
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)
            };
            const glm::vec4 sky_value{sky, 0.0f};
            write(gpu_data, m_merge_offsets.grid_counts,  as_span(grid_counts ));
            write(gpu_data, m_merge_offsets.upper_counts, as_span(upper_counts));
            write(gpu_data, m_merge_offsets.params,       as_span(params      ));
            write(gpu_data, m_merge_offsets.sky,          as_span(sky_value   ));
            control_range.bytes_written(byte_count);
            control_range.close();
        }

        command_buffer.transition_texture_layout(merged, Image_layout::general);
        {
            Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(m_merge_bind_group_layout.get());
            encoder.set_compute_pipeline(*m_merge_pipeline);
            m_control_buffer->bind(encoder, control_range);
            encoder.set_sampled_image(c_merge_raw_binding_point,   raw,          *m_merge_sampler);
            encoder.set_sampled_image(c_merge_upper_binding_point, upper_merged, *m_merge_sampler);
            encoder.set_storage_image(c_merge_output_binding_point, merged);
            encoder.dispatch_compute(
                static_cast<std::uintptr_t>((width  + c_merge_workgroup_size - 1) / c_merge_workgroup_size),
                static_cast<std::uintptr_t>((height + c_merge_workgroup_size - 1) / c_merge_workgroup_size),
                1
            );
        }
        control_range.release();
        command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
        command_buffer.transition_texture_layout(merged, Image_layout::shader_read_only_optimal);
    }
}

void Radiance_cascades_renderer::request_preview(
    const int                cascade,
    const Rc_preview_source  source,
    const Rc_preview_channel channel,
    const float              radiance_scale
)
{
    m_preview_requested      = true;
    m_preview_cascade        = cascade;
    m_preview_source         = source;
    m_preview_channel        = channel;
    m_preview_radiance_scale = radiance_scale;
}

auto Radiance_cascades_renderer::get_preview_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&
{
    return m_preview_texture;
}

void Radiance_cascades_renderer::record_preview(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    const int               cascade_index = std::clamp(m_preview_cascade, 0, m_layout.cascade_count - 1);
    const Radiance_cascade& cascade       = m_layout.cascades[static_cast<std::size_t>(cascade_index)];
    const int               width         = cascade.get_atlas_width();
    const int               height        = cascade.get_atlas_height();
    // Sized to the previewed atlas; reallocated only when the window
    // switches to a cascade of another size, or after a refit.
    if (!m_preview_texture || (m_preview_texture->get_width() != width) || (m_preview_texture->get_height() != height)) {
        m_preview_texture = std::make_shared<Texture>(
            m_graphics_device,
            Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  = Image_usage_flag_bit_mask::storage | Image_usage_flag_bit_mask::sampled,
                .type        = Texture_type::texture_2d,
                .pixelformat = c_radiance_format,
                .width       = width,
                .height      = height,
                .level_count = 1,
                .debug_label = erhe::utility::Debug_label{"RC preview"}
            }
        );
    }

    const std::size_t byte_count = m_preview_block.get_size_bytes();
    Ring_buffer_range control_range = m_control_buffer->acquire(Ring_buffer_usage::CPU_write, byte_count);
    {
        std::span<std::byte> gpu_data = control_range.get_span();
        std::memset(gpu_data.data(), 0, byte_count);
        const glm::uvec4 size{
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
            static_cast<uint32_t>(m_preview_channel),
            0u
        };
        const glm::vec4 params{m_preview_radiance_scale, m_layout.r0, 0.0f, 0.0f};
        write(gpu_data, m_preview_size_offset,   as_span(size  ));
        write(gpu_data, m_preview_params_offset, as_span(params));
        control_range.bytes_written(byte_count);
        control_range.close();
    }

    const Cascade_textures& textures = m_cascade_textures[static_cast<std::size_t>(cascade_index)];
    Texture& atlas = (m_preview_source == Rc_preview_source::merged) ? *textures.merged : *textures.raw;
    command_buffer.transition_texture_layout(atlas,               Image_layout::general);
    command_buffer.transition_texture_layout(*m_distance_texture, Image_layout::general);
    command_buffer.transition_texture_layout(*m_preview_texture,  Image_layout::general);
    {
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_preview_bind_group_layout.get());
        encoder.set_compute_pipeline(*m_preview_pipeline);
        m_control_buffer->bind(encoder, control_range);
        encoder.set_storage_image(c_preview_atlas_binding_point,    atlas);
        encoder.set_storage_image(c_preview_distance_binding_point, *m_distance_texture);
        encoder.set_storage_image(c_preview_output_binding_point,   *m_preview_texture);
        encoder.dispatch_compute(
            static_cast<std::uintptr_t>((width  + c_preview_workgroup_size - 1) / c_preview_workgroup_size),
            static_cast<std::uintptr_t>((height + c_preview_workgroup_size - 1) / c_preview_workgroup_size),
            1
        );
    }
    control_range.release();
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    command_buffer.transition_texture_layout(atlas,               Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_distance_texture, Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_preview_texture,  Image_layout::shader_read_only_optimal);
}

void Radiance_cascades_renderer::request_texel_readback()
{
    if (m_readback_state != Rc_readback_state::in_flight) {
        m_readback_state = Rc_readback_state::requested;
    }
}

void Radiance_cascades_renderer::record_texel_readback(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    // Layout of the copy: every raw atlas, every merged atlas, then the
    // distance texture, each at an aligned offset, tightly packed rows.
    const std::size_t radiance_texel_bytes = erhe::dataformat::get_format_size_bytes(c_radiance_format);
    const std::size_t distance_texel_bytes = erhe::dataformat::get_format_size_bytes(c_distance_format);
    std::size_t offset = 0;
    for (int i = 0; i < m_layout.cascade_count; ++i) {
        const std::size_t atlas_bytes = static_cast<std::size_t>(m_layout.cascades[static_cast<std::size_t>(i)].get_atlas_texel_count()) * radiance_texel_bytes;
        m_readback_raw_offsets[static_cast<std::size_t>(i)] = offset;
        offset = round_up(offset + atlas_bytes, c_readback_alignment);
        m_readback_merged_offsets[static_cast<std::size_t>(i)] = offset;
        offset = round_up(offset + atlas_bytes, c_readback_alignment);
    }
    m_readback_distance_offset = offset;
    offset = round_up(offset + (static_cast<std::size_t>(m_layout.cascades[0].get_atlas_texel_count()) * distance_texel_bytes), c_readback_alignment);
    m_readback_byte_count = offset;

    // Allocated on request only (the MCP path), grown to the largest layout
    // asked for.
    if (!m_readback_buffer || (m_readback_buffer->get_capacity_byte_count() < m_readback_byte_count)) {
        m_readback_buffer = std::make_unique<Buffer>(
            m_graphics_device,
            Buffer_create_info{
                .capacity_byte_count                    = m_readback_byte_count,
                .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
                .usage                                  = Buffer_usage::transfer_dst | Buffer_usage::storage,
                .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read | Memory_property_flag_bit_mask::host_write,
                .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_coherent | Memory_property_flag_bit_mask::host_persistent,
                .debug_label                            = erhe::utility::Debug_label{"RC texel readback"}
            }
        );
    }

    // copy_from_texture() moves each image from its tracked layout
    // (shader_read_only_optimal after the trace and merge) to transfer_src
    // and back.
    const auto copy = [&](Texture& texture, const std::size_t texel_bytes, const std::size_t destination_offset) {
        const int         width         = texture.get_width();
        const int         height        = texture.get_height();
        const std::size_t bytes_per_row = static_cast<std::size_t>(width) * texel_bytes;
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(
            &texture,
            0,                             // source_slice
            0,                             // source_level
            glm::ivec3{0, 0, 0},           // source_origin
            glm::ivec3{width, height, 1},  // source_size
            m_readback_buffer.get(),       // destination_buffer
            static_cast<std::uintptr_t>(destination_offset),
            static_cast<std::uintptr_t>(bytes_per_row),
            static_cast<std::uintptr_t>(bytes_per_row * static_cast<std::size_t>(height))
        );
    };
    for (int i = 0; i < m_layout.cascade_count; ++i) {
        copy(*m_cascade_textures[static_cast<std::size_t>(i)].raw,    radiance_texel_bytes, m_readback_raw_offsets   [static_cast<std::size_t>(i)]);
        copy(*m_cascade_textures[static_cast<std::size_t>(i)].merged, radiance_texel_bytes, m_readback_merged_offsets[static_cast<std::size_t>(i)]);
    }
    copy(*m_distance_texture, distance_texel_bytes, m_readback_distance_offset);

    m_readback_layout       = m_layout;
    m_readback_frame        = m_graphics_device.get_frame_index();
    m_readback_update_count = m_update_count;
    m_readback_sweep_count  = m_completed_sweeps;
    m_readback_state        = Rc_readback_state::in_flight;
}

auto Radiance_cascades_renderer::poll_texel_readback() -> Rc_readback_state
{
    if ((m_readback_state != Rc_readback_state::in_flight) || !m_graphics_device.is_frame_completed(m_readback_frame)) {
        return m_readback_state;
    }

    const std::span<std::byte> mapped = m_readback_buffer->map_bytes(0, m_readback_byte_count);
    m_readback_buffer->invalidate(0, m_readback_byte_count);
    m_readback_snapshot.assign(mapped.begin(), mapped.end());
    m_readback_buffer->unmap();

    // Per-cascade summaries over the probe texels.
    for (int i = 0; i < m_readback_layout.cascade_count; ++i) {
        const Radiance_cascade& cascade = m_readback_layout.cascades[static_cast<std::size_t>(i)];
        const int               q       = cascade.tile_texels;
        Cascade_summary         summary{};
        glm::dvec3              radiance_sum{0.0};
        glm::dvec3              merged_radiance_sum{0.0};
        double                  merged_beta_sum{0.0};
        int64_t                 beta_one_count = 0;
        int64_t                 backface_count = 0;
        for (int z = 0; z < cascade.grid.counts.z; ++z) {
            for (int y = 0; y < cascade.grid.counts.y; ++y) {
                for (int x = 0; x < cascade.grid.counts.x; ++x) {
                    bool probe_has_backface = false;
                    for (int v = 0; v < q; ++v) {
                        for (int u = 0; u < q; ++u) {
                            const glm::vec4 texel = read_raw_texel(i, glm::ivec3{x, y, z}, glm::ivec2{u, v});
                            radiance_sum += glm::dvec3{texel};
                            const glm::vec4 merged = read_merged_texel(i, glm::ivec3{x, y, z}, glm::ivec2{u, v});
                            merged_radiance_sum += glm::dvec3{merged};
                            merged_beta_sum     += static_cast<double>(merged.a);
                            if (texel.a > 0.5f) {
                                ++beta_one_count;
                            }
                            if ((i == 0) && (read_distance_texel(glm::ivec3{x, y, z}, glm::ivec2{u, v}) < 0.0f)) {
                                ++backface_count;
                                probe_has_backface = true;
                            }
                        }
                    }
                    if (probe_has_backface) {
                        ++summary.backface_probe_count;
                    }
                }
            }
        }
        summary.texel_count = cascade.get_texel_count();
        const double texel_count = std::max(1.0, static_cast<double>(summary.texel_count));
        summary.mean_radiance     = glm::vec3{radiance_sum / texel_count};
        summary.beta_one_fraction = static_cast<float>(static_cast<double>(beta_one_count) / texel_count);
        summary.mean_merged_radiance = glm::vec3{merged_radiance_sum / texel_count};
        summary.mean_merged_beta     = static_cast<float>(merged_beta_sum / texel_count);
        summary.backface_fraction = static_cast<float>(static_cast<double>(backface_count) / texel_count);
        m_readback_summaries[static_cast<std::size_t>(i)] = summary;
    }
    m_readback_state = Rc_readback_state::complete;
    return m_readback_state;
}

auto Radiance_cascades_renderer::get_readback_layout() const -> const Radiance_cascades_layout&
{
    return m_readback_layout;
}

auto Radiance_cascades_renderer::get_readback_update_count() const -> uint64_t
{
    return m_readback_update_count;
}

auto Radiance_cascades_renderer::get_readback_sweep_count() const -> uint64_t
{
    return m_readback_sweep_count;
}

auto Radiance_cascades_renderer::get_readback_summary(const int cascade) const -> const Cascade_summary&
{
    ERHE_VERIFY((cascade >= 0) && (cascade < m_readback_layout.cascade_count));
    return m_readback_summaries[static_cast<std::size_t>(cascade)];
}

auto Radiance_cascades_renderer::read_raw_texel(const int cascade_index, const glm::ivec3& probe, const glm::ivec2& texel) const -> glm::vec4
{
    ERHE_VERIFY((cascade_index >= 0) && (cascade_index < m_readback_layout.cascade_count));
    return read_radiance_texel(m_readback_raw_offsets[static_cast<std::size_t>(cascade_index)], cascade_index, probe, texel);
}

auto Radiance_cascades_renderer::read_merged_texel(const int cascade_index, const glm::ivec3& probe, const glm::ivec2& texel) const -> glm::vec4
{
    ERHE_VERIFY((cascade_index >= 0) && (cascade_index < m_readback_layout.cascade_count));
    return read_radiance_texel(m_readback_merged_offsets[static_cast<std::size_t>(cascade_index)], cascade_index, probe, texel);
}

auto Radiance_cascades_renderer::read_radiance_texel(
    const std::size_t  atlas_offset,
    const int          cascade_index,
    const glm::ivec3&  probe,
    const glm::ivec2&  texel
) const -> glm::vec4
{
    const Radiance_cascade& cascade     = m_readback_layout.cascades[static_cast<std::size_t>(cascade_index)];
    const int               probe_index = probe.x + (cascade.grid.counts.x * (probe.y + (cascade.grid.counts.y * probe.z)));
    const glm::ivec2        atlas_texel = cascade.get_tile_origin(probe_index) + texel;
    const std::size_t       texel_bytes = erhe::dataformat::get_format_size_bytes(c_radiance_format);
    const std::size_t       offset      =
        atlas_offset +
        (((static_cast<std::size_t>(atlas_texel.y) * static_cast<std::size_t>(cascade.get_atlas_width())) + static_cast<std::size_t>(atlas_texel.x)) * texel_bytes);
    ERHE_VERIFY((offset + texel_bytes) <= m_readback_snapshot.size());
    std::array<uint16_t, 4> halves{};
    std::memcpy(halves.data(), m_readback_snapshot.data() + offset, sizeof(halves));
    return glm::vec4{
        glm::unpackHalf1x16(halves[0]),
        glm::unpackHalf1x16(halves[1]),
        glm::unpackHalf1x16(halves[2]),
        glm::unpackHalf1x16(halves[3])
    };
}

auto Radiance_cascades_renderer::read_distance_texel(const glm::ivec3& probe, const glm::ivec2& texel) const -> float
{
    const Radiance_cascade& cascade     = m_readback_layout.cascades[0];
    const int               probe_index = probe.x + (cascade.grid.counts.x * (probe.y + (cascade.grid.counts.y * probe.z)));
    const glm::ivec2        atlas_texel = cascade.get_tile_origin(probe_index) + texel;
    const std::size_t       offset      =
        m_readback_distance_offset +
        (((static_cast<std::size_t>(atlas_texel.y) * static_cast<std::size_t>(cascade.get_atlas_width())) + static_cast<std::size_t>(atlas_texel.x)) * sizeof(float));
    ERHE_VERIFY((offset + sizeof(float)) <= m_readback_snapshot.size());
    float distance = 0.0f;
    std::memcpy(&distance, m_readback_snapshot.data() + offset, sizeof(float));
    return distance;
}

void Radiance_cascades_renderer::tick(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root)
{
    using namespace erhe::graphics;

    if (!is_supported() || !is_selected()) {
        return;
    }
    if (!update_layout(command_buffer, scene_root)) {
        return;
    }

    // Trace inputs, as Ddgi_renderer::tick() builds them: the scene root's
    // FORWARD material set (the TLAS instance records name slots in it,
    // doc/erhe/draw_list_material_set.md D5), the light block with its
    // projections, and this frame's TLAS.
    erhe::scene_renderer::Material_set& material_set = scene_root.get_material_set();
    if (material_set.get_live_count() == 0) {
        return;
    }
    if (!fit_trace_light_projections(m_context, m_graphics_device, scene_root, *m_light_projections)) {
        return;
    }
    // The scene ambient is the trace's ambient term and the merge's sky
    // radiance, as for DDGI's probe rays.
    const glm::vec3   ambient     = scene_root.get_scene().get_ambient_light();
    Scene_tlas::Frame tlas_frame  = m_scene_tlas->update(command_buffer, *scene_root.layers().content(), &material_set);
    ERHE_VERIFY(tlas_frame.is_valid());
    Ring_buffer_range light_range = m_light_buffer->update(m_light_projections.get(), ambient);

    sample_pass_timings();
    ++m_update_count;
    record_trace(command_buffer, tlas_frame, light_range, material_set);

    light_range.release();
    tlas_frame.instance_records.release();
    material_set.unbind(command_buffer);

    // Every merged texel depends on raw texels of its own and every higher
    // cascade, so each update that traced re-merges all cascades.
    record_merge(command_buffer, ambient);

    if (m_preview_requested) {
        m_preview_requested = false;
        record_preview(command_buffer);
    }
    if (m_readback_state == Rc_readback_state::requested) {
        record_texel_readback(command_buffer);
        // Transfer writes -> host reads once the frame's fence has signalled.
        command_buffer.memory_barrier(Memory_barrier_mask::client_mapped_buffer_barrier_bit);
    }
}

} // namespace editor
