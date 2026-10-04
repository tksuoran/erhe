#include "renderers/radiance_cascades_renderer.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "config/generated/ddgi_config.hpp"
#include "config/generated/indirect_diffuse_bounces.hpp"
#include "config/generated/radiance_cascades_config.hpp"
#include "config/generated/radiance_cascades_direction_jitter.hpp"
#include "config/generated/radiance_cascades_merge_mode.hpp"
#include "config/generated/radiance_cascades_probe_overlay.hpp"
#include "editor_log.hpp"
#include "renderers/content_bounds.hpp"
#include "renderers/render_context.hpp"
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
#include "erhe_profile/profile.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
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
#include <bit>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

namespace editor {

namespace {

// rgb radiance, a transparency beta (doc/plans/radiance_cascades.md section 3).
constexpr erhe::dataformat::Format c_radiance_format = erhe::dataformat::Format::format_16_vec4_float;
// Cascade 0 hit distance statistics per raw texel: mean distance, mean
// squared distance, backface fraction (blended like the raw texel). Full
// float: the readback is a plain memcpy and the distances keep millimetres.
constexpr erhe::dataformat::Format c_distance_format = erhe::dataformat::Format::format_32_vec4_float;

// Raw binding points of the trace bind group layout, as in Ddgi_renderer:
// 0 and 1 are the shared material / light block binding points
// (erhe_scene_renderer), 2 the control block, 3 the instance records, then
// the acceleration structure and the two storage images (raw bindings are
// not offset past the buffer bindings).
constexpr unsigned int c_control_binding_point         = 2;
constexpr unsigned int c_instance_record_binding_point = 3;

// Trace pass: the previous probe field as combined image samplers (bounces
// multi), user points 4 - 6; Vulkan offsets samplers past the highest
// buffer binding, 3, so they land at 8 - 10, after the acceleration
// structure (4) and the storage images (5 - 7).
constexpr unsigned int c_trace_field_irradiance_binding_point = 4;
constexpr unsigned int c_trace_field_distance_binding_point   = 5;
constexpr unsigned int c_trace_field_probe_data_binding_point = 6;

// Merge pass layout: the control block, then as combined image samplers
// the raw atlas and probe state of the cascade, the merged atlas and probe
// state of the cascade above and the cascade's neighbour atlas (user
// points 0 - 4; Vulkan offsets samplers past the highest buffer binding,
// 2, so they land at 3 - 7), and the merged atlas written as the storage
// image at raw binding point 8.
constexpr unsigned int c_merge_raw_binding_point         = 0;
constexpr unsigned int c_merge_upper_binding_point       = 1;
constexpr unsigned int c_merge_state_binding_point       = 2;
constexpr unsigned int c_merge_upper_state_binding_point = 3;
constexpr unsigned int c_merge_neighbours_binding_point  = 4;
constexpr unsigned int c_merge_output_binding_point      = 8;

// Probe state texture (Cascade_textures::state): an integer-valued float
// per probe.
constexpr erhe::dataformat::Format c_state_format = erhe::dataformat::Format::format_32_scalar_float;
constexpr int                      c_visibility_workgroup_size = 64; // rc_visibility.comp local size
constexpr int          c_merge_workgroup_size       = 8; // rc_merge.comp local size, both axes

// rc_merge.comp params.y flags
constexpr uint32_t c_merge_flag_top            = 1u; // top cascade: merge with the sky
constexpr uint32_t c_merge_flag_mask_radiance  = 2u; // debug_cascade_mask: zero the cascade's radiance
constexpr uint32_t c_merge_flag_child_resolution = 4u; // cascade 0: merged at cascade 1's angular resolution

// Reduce pass layout (rc_reduce.comp): the control block, the weight
// storage buffer, then as combined image samplers merged cascade 0 and the
// cascade 0 distance texture (user points 0 and 1; Vulkan offsets them past
// the highest buffer binding, 3, to 4 and 5), and the written atlas and
// probe data as storage images at raw binding points 6 and 7.
constexpr unsigned int c_reduce_weights_binding_point    = 3;
constexpr unsigned int c_reduce_merged_binding_point     = 0;
constexpr unsigned int c_reduce_distance_binding_point   = 1;
constexpr unsigned int c_reduce_atlas_binding_point      = 6;
constexpr unsigned int c_reduce_probe_data_binding_point = 7;
constexpr int          c_reduce_workgroup_size           = 64; // one workgroup per probe, striding over its tile
// Reduce lobe weights below this fraction of an output texel's largest one
// are left out of its sparse list (their share of the sum is below 1e-4
// times the input count).
constexpr float        c_reduce_weight_threshold         = 1.0e-4f;
// Workgroups per row of the 2D reduce dispatch (within the Vulkan-guaranteed
// maxComputeWorkGroupCount of 65535).
constexpr int          c_reduce_dispatch_row             = 32768;

// Probe field atlas formats: exactly DDGI's (doc/editor/ddgi.md "Data
// layout"), so erhe_ddgi.glsl samples either producer.
constexpr erhe::dataformat::Format c_field_irradiance_format = erhe::dataformat::Format::format_16_vec4_float;
constexpr erhe::dataformat::Format c_field_distance_format   = erhe::dataformat::Format::format_16_vec2_float;
constexpr erhe::dataformat::Format c_field_probe_data_format = erhe::dataformat::Format::format_32_vec4_float;
constexpr int                      c_field_border_texels     = 1;

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

// Readback buffer offsets are rounded up to this (a multiple of every texel
// size and of every nonCoherentAtomSize the Vulkan spec allows).
constexpr std::size_t c_readback_alignment = 256;

// Probe overlay: frames between two overlay copies (the copy of a large
// cascade's merged atlas and its CPU reduction are not free, and the
// overlay is a debug aid that need not follow every update).
constexpr uint64_t c_probe_overlay_interval_frames = 10;

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
    App_message_bus&                         app_message_bus,
    erhe::scene_renderer::Program_interface& program_interface,
    erhe::scene_renderer::Mesh_memory&       mesh_memory,
    const Radiance_cascades_config&          config,
    const Ddgi_config&                       ddgi_config,
    const Producer_selection                 selection
)
    : m_graphics_device{graphics_device}
    , m_context        {context}
    , m_config         {config}
    , m_ddgi_config    {ddgi_config}
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
    , m_merge_mode{config.merge_mode}
    , m_visibility_block{
        graphics_device,
        "rc_visibility",
        static_cast<int>(c_control_binding_point),
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , m_reduce_block{
        graphics_device,
        "rc_reduce",
        static_cast<int>(c_control_binding_point),
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , m_reduce_weights_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "rc_reduce_weights",
            .binding_point = static_cast<int>(c_reduce_weights_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
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
    // w unused
    m_control_offsets.dispatch     = m_control_block.add_uvec4("dispatch"    )->get_offset_in_parent();
    // x = hysteresis
    m_control_offsets.params       = m_control_block.add_vec4 ("params"      )->get_offset_in_parent();
    // Neighbour variant only: the upper cascade's grid origin, spacing and
    // probe counts (the connecting segments end at its interval starts).
    m_control_offsets.upper_origin  = m_control_block.add_vec4 ("upper_origin" )->get_offset_in_parent();
    m_control_offsets.upper_spacing = m_control_block.add_vec4 ("upper_spacing")->get_offset_in_parent();
    m_control_offsets.upper_counts  = m_control_block.add_uvec4("upper_counts" )->get_offset_in_parent();
    // Temporal history of the run (Temporal_history::get_shader_parameters(),
    // erhe_temporal_history.glsl).
    m_control_offsets.history       = m_control_block.add_uvec4("history"      )->get_offset_in_parent();
    // x = the run's first texel in the global order of all cascades, y =
    // the update's jitter seed, z = Radiance_cascades_direction_jitter
    m_control_offsets.run           = m_control_block.add_uvec4("run"          )->get_offset_in_parent();

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
    m_neighbours_binding_point = c_instance_record_binding_point + 4;
    m_field_irradiance_binding_point = c_trace_field_irradiance_binding_point;
    m_field_distance_binding_point   = c_trace_field_distance_binding_point;
    m_field_probe_data_binding_point = c_trace_field_probe_data_binding_point;
    m_field_sampler                  = &program_interface.light_interface.ddgi_sampler;
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
                    .image_format  = "rgba32f",
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = m_neighbours_binding_point,
                    .type          = Binding_type::storage_image,
                    .name          = "i_rc_neighbours",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba16f",
                    .stage_flags   = Shader_stage_flags::compute
                },
                // The previous probe field (erhe_ray_hit.glsl
                // ERHE_RT_INDIRECT_FIELD): sampled at hits when bounces is
                // multi, unreferenced at run time otherwise (the light block
                // then carries no field).
                {
                    .binding_point   = m_field_irradiance_binding_point,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_ddgi_irradiance",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                {
                    .binding_point   = m_field_distance_binding_point,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_ddgi_distance",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                {
                    .binding_point   = m_field_probe_data_binding_point,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_ddgi_probe_data",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
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
    // Three variants of rc_trace.comp: cascade 0 dispatches also write the
    // distance texture, the other raw dispatches do not reference it at
    // all, and the neighbour variant (merge mode per_neighbour_trace)
    // writes only the neighbour atlas. A dispatch that statically uses a
    // storage image counts as writing it, so a shared variant would make
    // every cascade's dispatch a write of the distance texture, and
    // consecutive dispatches of one frame would be write-after-write
    // hazards although only cascade 0 writes it.
    const auto make_trace_pass = [&](Trace_pass& pass, const char* write_distance_define, const char* neighbours_define, const char* name) {
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
                    { "ERHE_RC_TRACE_WRITE_DISTANCE", write_distance_define },
                    { "ERHE_RC_TRACE_NEIGHBOURS",     neighbours_define },
                    { "ERHE_RT_INDIRECT_FIELD",       "1" }
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
    make_trace_pass(m_trace_cascade0,   "1", "0", "rc_trace_cascade0");
    make_trace_pass(m_trace_upper,      "0", "0", "rc_trace");
    make_trace_pass(m_trace_neighbours, "0", "1", "rc_trace_neighbours");

    // Visibility pass: the trace's buffers and TLAS (the shared hit path
    // references the material and light blocks), and the cascade's probe
    // state as the only storage image. Control block: grid_origin xyz, w =
    // classification ray length; grid_spacing xyz; grid_counts xyz, w =
    // tiles per atlas row; upper_origin / upper_spacing xyz; upper_counts
    // xyz, w = 1 when an upper cascade exists.
    m_visibility_offsets.grid_origin   = m_visibility_block.add_vec4 ("grid_origin"  )->get_offset_in_parent();
    m_visibility_offsets.grid_spacing  = m_visibility_block.add_vec4 ("grid_spacing" )->get_offset_in_parent();
    m_visibility_offsets.grid_counts   = m_visibility_block.add_uvec4("grid_counts"  )->get_offset_in_parent();
    m_visibility_offsets.upper_origin  = m_visibility_block.add_vec4 ("upper_origin" )->get_offset_in_parent();
    m_visibility_offsets.upper_spacing = m_visibility_block.add_vec4 ("upper_spacing")->get_offset_in_parent();
    m_visibility_offsets.upper_counts  = m_visibility_block.add_uvec4("upper_counts" )->get_offset_in_parent();
    m_state_binding_point = c_instance_record_binding_point + 2;
    m_visibility_bind_group_layout = std::make_unique<Bind_group_layout>(
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
                    .binding_point = m_state_binding_point,
                    .type          = Binding_type::storage_image,
                    .name          = "i_rc_state",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "r32f",
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label       = "RC visibility",
            .uses_texture_heap = true
        }
    );
    m_visibility_shader_stages = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name                = "rc_visibility",
            .defines             = {
                { "ERHE_TLAS_BINDING",          fmt::format("{}", m_tlas_binding_point) },
                { "ERHE_RT_NORMAL_OFFSET",      fmt::format("{}", normal   .attribute->offset / 4) },
                { "ERHE_RT_TANGENT_OFFSET",     fmt::format("{}", tangent  .attribute->offset / 4) },
                { "ERHE_RT_TEXCOORD0_OFFSET",   fmt::format("{}", texcoord0.attribute->offset / 4) },
                { "ERHE_RT_COLOR0_OFFSET",      fmt::format("{}", color0   .attribute->offset / 4) },
                { "ERHE_RT_HAS_POSITION_FETCH", use_position_fetch ? "1" : "0" }
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
                &m_visibility_block,
                &m_scene_tlas->get_instance_block()
            },
            .shaders             = { { Shader_type::compute_shader, editor_shaders / "rc_visibility.comp" } },
            .extra_include_paths = shader_paths(),
            .bind_group_layout   = m_visibility_bind_group_layout.get()
        }
    );
    graphics_device.get_shader_monitor().add(*m_visibility_shader_stages);
    m_visibility_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "rc_visibility",
            .shader_stages     = &m_visibility_shader_stages->shader_stages,
            .bind_group_layout = m_visibility_bind_group_layout.get()
        }
    );
    m_visibility_timer = std::make_unique<Gpu_timer>(graphics_device, "RC visibility");

    // The visibility depends on the scene geometry: recompute it when an
    // edit changes a transform or a mesh's geometry, or removes content.
    // Committed changes of the light transport reset the temporal history
    // of the traced texels (doc/editor/ddgi.md "History reset"): geometry
    // edits, removals and Scene_lighting_changed_message (committed node
    // transforms, content or lights added or removed, light and material
    // edits). A live transform touch does not: a drag keeps blending.
    m_node_touched_subscription = app_message_bus.node_touched.subscribe(
        [this](Node_touched_message&) { m_visibility_dirty = true; }
    );
    m_mesh_geometry_changed_subscription = app_message_bus.mesh_geometry_changed.subscribe(
        [this](Mesh_geometry_changed_message&) {
            m_visibility_dirty = true;
            m_history.request_reset();
        }
    );
    m_items_removed_subscription = app_message_bus.items_removed.subscribe(
        [this](Items_removed_message&) {
            m_visibility_dirty = true;
            m_history.request_reset();
        }
    );
    m_scene_lighting_changed_subscription = app_message_bus.scene_lighting_changed.subscribe(
        [this](Scene_lighting_changed_message&) { m_history.request_reset(); }
    );

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
                    .binding_point   = c_merge_state_binding_point,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_rc_state",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                {
                    .binding_point   = c_merge_upper_state_binding_point,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_rc_upper_state",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                {
                    .binding_point   = c_merge_neighbours_binding_point,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_rc_neighbours",
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
    // One variant per merge mode (ERHE_RC_MERGE_MODE = the enum value);
    // only visibility_masked references the probe state samplers, only
    // per_neighbour_trace the neighbour atlas.
    const auto make_merge_pass = [&](
        std::unique_ptr<Reloadable_shader_stages>& stages,
        std::unique_ptr<Compute_pipeline>&         pipeline,
        const char*                                mode_define,
        const char*                                name
    ) {
        stages = std::make_unique<Reloadable_shader_stages>(
            graphics_device,
            Shader_stages_create_info{
                .name                = name,
                .defines             = { { "ERHE_RC_MERGE_MODE", mode_define } },
                .interface_blocks    = { &m_merge_block },
                .shaders             = { { Shader_type::compute_shader, editor_shaders / "rc_merge.comp" } },
                .extra_include_paths = shader_paths(),
                .bind_group_layout   = m_merge_bind_group_layout.get()
            }
        );
        graphics_device.get_shader_monitor().add(*stages);
        pipeline = std::make_unique<Compute_pipeline>(
            graphics_device,
            Compute_pipeline_data{
                .name              = name,
                .shader_stages     = &stages->shader_stages,
                .bind_group_layout = m_merge_bind_group_layout.get()
            }
        );
    };
    make_merge_pass(m_merge_shader_stages,            m_merge_pipeline,            "0", "rc_merge");
    make_merge_pass(m_merge_visibility_shader_stages, m_merge_visibility_pipeline, "1", "rc_merge_visibility");
    make_merge_pass(m_merge_neighbours_shader_stages, m_merge_neighbours_pipeline, "2", "rc_merge_neighbours");

    // Reduce: grid_counts xyz = cascade 0 probe counts, w = q0; params x =
    // cascade 0 tiles per atlas row, y = field tiles per atlas row, z =
    // irradiance texels, w = distance texels; weights x / y / z = offsets
    // (in floats) of the irradiance weights, the distance weights and the
    // cascade 0 texel solid angles in the weight buffer; limits x = r0.
    m_reduce_offsets.grid_counts = m_reduce_block.add_uvec4("grid_counts")->get_offset_in_parent();
    m_reduce_offsets.params      = m_reduce_block.add_uvec4("params"     )->get_offset_in_parent();
    m_reduce_offsets.weights     = m_reduce_block.add_uvec4("weights"    )->get_offset_in_parent();
    m_reduce_offsets.limits      = m_reduce_block.add_vec4 ("limits"     )->get_offset_in_parent();
    m_reduce_weights_block.add_vec4("weights", Shader_resource::unsized_array);
    const auto make_reduce_pass = [&](Reduce_pass& pass, const bool distance) {
        std::vector<Bind_group_layout_binding> bindings{
            {
                .binding_point = c_control_binding_point,
                .type          = Binding_type::uniform_buffer,
                .stage_flags   = Shader_stage_flags::compute
            },
            {
                .binding_point = c_reduce_weights_binding_point,
                .type          = Binding_type::storage_buffer,
                .stage_flags   = Shader_stage_flags::compute
            },
            {
                .binding_point   = c_reduce_merged_binding_point,
                .type            = Binding_type::combined_image_sampler,
                .sampler_aspect  = Sampler_aspect::color,
                .name            = "s_rc_merged",
                .glsl_type       = Glsl_type::sampler_2d,
                .is_texture_heap = false,
                .stage_flags     = Shader_stage_flags::compute
            },
            {
                .binding_point   = c_reduce_distance_binding_point,
                .type            = Binding_type::combined_image_sampler,
                .sampler_aspect  = Sampler_aspect::color,
                .name            = "s_rc_distance",
                .glsl_type       = Glsl_type::sampler_2d,
                .is_texture_heap = false,
                .stage_flags     = Shader_stage_flags::compute
            },
            {
                .binding_point = c_reduce_atlas_binding_point,
                .type          = Binding_type::storage_image,
                .name          = "i_probe_atlas",
                .glsl_type     = Glsl_type::image_2d,
                .image_format  = distance ? "rg16f" : "rgba16f",
                .stage_flags   = Shader_stage_flags::compute
            }
        };
        // Only the irradiance variant writes the probe data, so only it
        // references the image.
        if (!distance) {
            bindings.push_back(
                Bind_group_layout_binding{
                    .binding_point = c_reduce_probe_data_binding_point,
                    .type          = Binding_type::storage_image,
                    .name          = "i_probe_data",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba32f",
                    .stage_flags   = Shader_stage_flags::compute
                }
            );
        }
        const char* name = distance ? "rc_reduce_distance" : "rc_reduce_irradiance";
        pass.bind_group_layout = std::make_unique<Bind_group_layout>(
            graphics_device,
            Bind_group_layout_create_info{
                .bindings    = bindings,
                .debug_label = distance ? "RC reduce distance" : "RC reduce irradiance"
            }
        );
        pass.shader_stages = std::make_unique<Reloadable_shader_stages>(
            graphics_device,
            Shader_stages_create_info{
                .name                = name,
                .defines             = {
                    { "ERHE_RC_REDUCE_GROUP_SIZE", fmt::format("{}", c_reduce_workgroup_size) },
                    { "ERHE_RC_REDUCE_DISTANCE",   distance ? "1" : "0" }
                },
                .interface_blocks    = { &m_reduce_block, &m_reduce_weights_block },
                .shaders             = { { Shader_type::compute_shader, editor_shaders / "rc_reduce.comp" } },
                .extra_include_paths = shader_paths(),
                .bind_group_layout   = pass.bind_group_layout.get()
            }
        );
        graphics_device.get_shader_monitor().add(*pass.shader_stages);
        pass.pipeline = std::make_unique<Compute_pipeline>(
            graphics_device,
            Compute_pipeline_data{
                .name              = name,
                .shader_stages     = &pass.shader_stages->shader_stages,
                .bind_group_layout = pass.bind_group_layout.get()
            }
        );
    };
    make_reduce_pass(m_reduce_irradiance, false);
    make_reduce_pass(m_reduce_distance,   true);

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
                    .image_format  = "rgba32f",
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
    m_pass_timings[static_cast<std::size_t>(Rc_pass::reduce)].timer = std::make_unique<Gpu_timer>(graphics_device, "RC reduce");
    m_pass_timings[static_cast<std::size_t>(Rc_pass::neighbour_trace)].timer = std::make_unique<Gpu_timer>(graphics_device, "RC neighbour trace");

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
        (m_trace_neighbours.pipeline != nullptr) &&
        (m_merge_pipeline != nullptr) &&
        (m_merge_visibility_pipeline != nullptr) &&
        (m_merge_neighbours_pipeline != nullptr) &&
        (m_visibility_pipeline != nullptr) &&
        (m_reduce_irradiance.pipeline != nullptr) &&
        (m_reduce_distance.pipeline != nullptr);
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
    return is_active() && static_cast<bool>(m_field_irradiance);
}

auto Radiance_cascades_renderer::get_forward_parameters() const -> erhe::scene_renderer::Ddgi_parameters
{
    erhe::scene_renderer::Ddgi_parameters parameters{};
    if (!has_field()) {
        return parameters;
    }
    // Cascade 0's grid, and the DDGI sampling settings: the field is
    // sampled exactly like DDGI's.
    const Radiance_cascade& cascade0 = m_layout.cascades[0];
    parameters.grid_origin       = cascade0.grid.origin;
    parameters.grid_spacing      = cascade0.grid.spacing;
    parameters.grid_counts       = cascade0.grid.counts;
    parameters.irradiance_texels = m_field_settings.irradiance_texels;
    parameters.distance_texels   = m_field_settings.distance_texels;
    parameters.tiles_per_row     = m_field_tiles_per_row;
    parameters.normal_bias       = m_ddgi_config.normal_bias;
    parameters.view_bias         = m_ddgi_config.view_bias;
    parameters.depth_sharpness   = m_field_settings.depth_sharpness; // the value the distance atlas was reduced with
    parameters.intensity         = m_ddgi_config.intensity;
    return parameters;
}

auto Radiance_cascades_renderer::get_field() const -> Probe_field
{
    if (!has_field()) {
        return Probe_field{};
    }
    return Probe_field{
        .parameters   = get_forward_parameters(),
        .irradiance   = m_field_irradiance,
        .distance     = m_field_distance,
        .probe_data   = m_field_probe_data,
        .update_count = m_update_count
    };
}

void Radiance_cascades_renderer::set_merge_mode(const Radiance_cascades_merge_mode mode)
{
    if (mode == m_merge_mode) {
        return;
    }
    m_merge_mode = mode;
    if (mode == Radiance_cascades_merge_mode::visibility_masked) {
        // Allocated and computed on the next tick (needs its command buffer).
        m_visibility_dirty = true;
    } else {
        release_state_textures();
    }
    // Into or out of per_neighbour_trace: the next tick's update_layout()
    // sees the atlas block of the fit settings change and refits, which
    // (re)allocates or releases the neighbour atlases with a first fill.
    // The pass timings of the other mode would mislead.
    clear_pass_timings();
}

auto Radiance_cascades_renderer::get_merge_mode() const -> Radiance_cascades_merge_mode
{
    return m_merge_mode;
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
    // The neighbour trace runs only in its merge mode; outside it the timer
    // still holds its last measurement.
    if (m_merge_mode != Radiance_cascades_merge_mode::per_neighbour_trace) {
        const std::size_t neighbour_index = static_cast<std::size_t>(Rc_pass::neighbour_trace);
        sum_ns -= results_ns[neighbour_index];
        results_ns[neighbour_index] = 0;
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
    stats.neighbour_trace  = pass_time(Rc_pass::neighbour_trace);
    stats.merge            = pass_time(Rc_pass::merge);
    stats.reduce           = pass_time(Rc_pass::reduce);
    stats.total.last_ms    = stats.trace.last_ms    + stats.neighbour_trace.last_ms    + stats.merge.last_ms    + stats.reduce.last_ms;
    stats.total.average_ms = stats.trace.average_ms + stats.neighbour_trace.average_ms + stats.merge.average_ms + stats.reduce.average_ms;
    stats.update_count        = m_update_count;
    stats.timing_sample_count = m_timing_sample_count;
    stats.completed_sweeps    = m_completed_sweeps;
    stats.texels_per_update   = m_texels_per_update;
    const int64_t total_texels = m_layout.get_total_texels();
    // The connecting segments per update: the sweep's segments spread over
    // the updates of a sweep (the cursor's runs cross cascades, whose
    // texels carry different segment counts).
    stats.neighbour_rays_per_update = (total_texels > 0)
        ? ((m_neighbour_rays_per_sweep * m_texels_per_update) + (total_texels / 2)) / total_texels
        : 0;
    stats.rays_per_update = m_texels_per_update + stats.neighbour_rays_per_update;
    stats.updates_per_full_refresh = (m_texels_per_update > 0)
        ? ((total_texels + m_texels_per_update - 1) / m_texels_per_update)
        : 0;
    stats.ms_per_million_rays = (stats.rays_per_update > 0)
        ? (stats.total.average_ms * 1.0e6) / static_cast<double>(stats.rays_per_update)
        : 0.0;
    stats.full_refresh_ms = static_cast<double>(stats.updates_per_full_refresh) * stats.total.average_ms;
    stats.visibility_last_ms      = static_cast<double>(m_visibility_last_ns) * 1.0e-6;
    stats.visibility_update_count = m_visibility_update_count;
    stats.history_reset_count     = m_history.get_reset_count();
    stats.probe_overlay_readback_count = m_overlay_readback_count;
    return stats;
}

void Radiance_cascades_renderer::release_textures()
{
    for (Cascade_textures& textures : m_cascade_textures) {
        textures.raw.reset();
        textures.merged.reset();
        textures.neighbours.reset();
    }
    m_neighbour_rays_per_sweep = 0;
    release_state_textures();
    release_field();
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
        // Cascade 0 is merged at cascade 1's angular resolution
        // (c_merged_cascade0_block^2 texels per texel), the reduce's input.
        const int merged_block = (i == 0) ? c_merged_cascade0_block : 1;
        textures.raw    = make_texture(fmt::format("RC cascade {} raw",    i), c_radiance_format, width, height);
        textures.merged = make_texture(fmt::format("RC cascade {} merged", i), c_radiance_format, width * merged_block, height * merged_block);
        const std::size_t bytes =
            static_cast<std::size_t>(1 + (merged_block * merged_block)) *
            static_cast<std::size_t>(cascade.get_atlas_texel_count()) * radiance_texel_bytes;
        m_cascade_byte_counts[static_cast<std::size_t>(i)] = bytes;
        m_texture_byte_count += bytes;
        m_cascade_texel_offsets[static_cast<std::size_t>(i)] = texel_offset;
        texel_offset += cascade.get_texel_count();
    }
    // Merge mode per_neighbour_trace (the layout was fitted with its atlas
    // block): a neighbour atlas for every cascade but the top one, and the
    // connecting segments one full sweep traces - the upper probes of
    // nonzero weight of each probe, for each of its q_i^2 texels. Cold path:
    // runs once per allocation.
    if (m_fit_settings.atlas_block_width == c_neighbour_block_width) {
        for (int i = 0; (i + 1) < m_layout.cascade_count; ++i) {
            const Radiance_cascade& cascade = m_layout.cascades[static_cast<std::size_t>(i)];
            const Radiance_cascade& upper   = m_layout.cascades[static_cast<std::size_t>(i + 1)];
            Cascade_textures& textures = m_cascade_textures[static_cast<std::size_t>(i)];
            textures.neighbours = make_texture(
                fmt::format("RC cascade {} neighbours", i),
                c_radiance_format,
                cascade.get_atlas_width () * c_neighbour_block_width,
                cascade.get_atlas_height() * c_neighbour_block_height
            );
            const std::size_t bytes =
                static_cast<std::size_t>(c_neighbour_block_width * c_neighbour_block_height) *
                static_cast<std::size_t>(cascade.get_atlas_texel_count()) * radiance_texel_bytes;
            m_cascade_byte_counts[static_cast<std::size_t>(i)] += bytes;
            m_texture_byte_count += bytes;

            int64_t segments_per_texel_sum = 0;
            for (int z = 0; z < cascade.grid.counts.z; ++z) {
                for (int y = 0; y < cascade.grid.counts.y; ++y) {
                    for (int x = 0; x < cascade.grid.counts.x; ++x) {
                        const Upper_probes upper_probes = get_upper_probes(glm::ivec3{x, y, z}, cascade.grid.counts, upper.grid.counts);
                        for (const float weight : upper_probes.weights) {
                            if (weight > 0.0f) {
                                ++segments_per_texel_sum;
                            }
                        }
                    }
                }
            }
            const int64_t q = static_cast<int64_t>(cascade.tile_texels);
            m_neighbour_rays_per_sweep += segments_per_texel_sum * q * q;
        }
    }
    {
        const Radiance_cascade& cascade0 = m_layout.cascades[0];
        m_distance_texture = make_texture("RC cascade 0 distance", c_distance_format, cascade0.get_atlas_width(), cascade0.get_atlas_height());
        const std::size_t bytes = static_cast<std::size_t>(cascade0.get_atlas_texel_count()) * erhe::dataformat::get_format_size_bytes(c_distance_format);
        m_cascade_byte_counts[0] += bytes;
        m_texture_byte_count     += bytes;
    }

    // New probe positions: in the visibility_masked mode the probe states
    // are reallocated and recomputed before the next merge reads them.
    m_visibility_dirty = true;

    // Every texel is new: its history is the allocation clear, so its first
    // trace is written unblended (the first fill).
    m_history.reset(m_layout.get_total_texels(), m_texel_cursor);

    allocate_field(command_buffer);

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
    const Field_settings field_settings{
        .irradiance_texels = std::clamp(m_ddgi_config.irradiance_texels, 2, 32),
        .distance_texels   = std::clamp(m_ddgi_config.distance_texels,   2, 64),
        .depth_sharpness   = std::max(1.0f, m_ddgi_config.depth_sharpness)
    };
    const int max_texture_size = m_graphics_device.get_info().max_texture_size;
    // The probe field atlases of cascade 0's grid must fit the texture size
    // limit too, which bounds the cascade 0 probe budget.
    const int field_max_probes = get_probe_field_max_probes(
        std::max(field_settings.irradiance_texels, field_settings.distance_texels) + (2 * c_field_border_texels),
        max_texture_size
    );
    const Radiance_cascades_layout_settings settings{
        .probe_spacing_m      = std::max(0.01f, m_config.probe_spacing_m),
        .max_probes_cascade0  = std::min(std::max(8, m_config.max_probes_cascade0), field_max_probes),
        .max_cascades         = std::clamp(m_config.max_cascades, 1, c_max_radiance_cascades),
        .cascade0_tile_texels = std::clamp(m_config.cascade0_tile_texels, 1, 64),
        .interval_scale       = std::max(1.0f,  m_config.interval_scale),
        .max_texture_size     = max_texture_size,
        // The per_neighbour_trace merge mode's neighbour atlases are part
        // of the layout: switching into or out of the mode refits.
        .atlas_block_width    = (m_merge_mode == Radiance_cascades_merge_mode::per_neighbour_trace) ? c_neighbour_block_width  : 1,
        .atlas_block_height   = (m_merge_mode == Radiance_cascades_merge_mode::per_neighbour_trace) ? c_neighbour_block_height : 1
    };
    const float padding_m = std::max(0.0f, m_config.volume_padding_m);

    const erhe::math::Aabb bounds = compute_padded_content_bounds(scene_root, padding_m);
    if (!bounds.is_valid_3d()) {
        return false;
    }

    const bool settings_changed =
        (settings  != m_fit_settings ) ||
        (padding_m != m_fit_padding_m) ||
        !m_layout.is_valid() ||
        !m_cascade_textures[0].raw;
    const bool bounds_changed = m_volume_bounds.content_changed(bounds);
    if (!settings_changed && !bounds_changed) {
        // The field's sampling settings live in Ddgi_config, edited from
        // the Settings window (reflected) and MCP set_ddgi without a change
        // notification, so the settings the field was allocated with are
        // compared here, like the fit settings above.
        if ((field_settings != m_field_settings) || !m_field_irradiance) {
            m_field_settings = field_settings;
            allocate_field(command_buffer);
        }
        return true;
    }

    const Volume_refit_cause       cause      = settings_changed ? Volume_refit_cause::settings : Volume_refit_cause::content;
    const erhe::math::Aabb         fit_bounds = m_volume_bounds.get_fit_bounds(bounds, cause);
    const Radiance_cascades_layout layout     = fit_radiance_cascades(fit_bounds, settings);
    if (!layout.is_valid()) {
        return false;
    }

    m_volume_bounds.set(fit_bounds);
    m_fit_settings   = settings;
    m_fit_padding_m  = padding_m;
    m_field_settings = field_settings;
    m_layout         = layout;
    ++m_fit_count;
    allocate_textures(command_buffer);
    return true;
}

void Radiance_cascades_renderer::allocate_state_textures(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    release_state_textures();
    const std::size_t state_texel_bytes = erhe::dataformat::get_format_size_bytes(c_state_format);
    for (int i = 0; i < m_layout.cascade_count; ++i) {
        const Radiance_cascade& cascade = m_layout.cascades[static_cast<std::size_t>(i)];
        std::shared_ptr<Texture> texture = std::make_shared<Texture>(
            m_graphics_device,
            Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  = Image_usage_flag_bit_mask::storage      |
                               Image_usage_flag_bit_mask::sampled      |
                               Image_usage_flag_bit_mask::transfer_dst |
                               Image_usage_flag_bit_mask::transfer_src,
                .type        = Texture_type::texture_2d,
                .pixelformat = c_state_format,
                .width       = cascade.tiles_per_row,
                .height      = cascade.tile_rows,
                .level_count = 1,
                .debug_label = erhe::utility::Debug_label{fmt::format("RC cascade {} probe state", i)}
            }
        );
        command_buffer.clear_texture(*texture, {0.0, 0.0, 0.0, 0.0});
        command_buffer.transition_texture_layout(*texture, Image_layout::shader_read_only_optimal);
        m_cascade_textures[static_cast<std::size_t>(i)].state = texture;
        const std::size_t bytes = static_cast<std::size_t>(cascade.tiles_per_row) * static_cast<std::size_t>(cascade.tile_rows) * state_texel_bytes;
        m_cascade_byte_counts[static_cast<std::size_t>(i)] += bytes;
        m_texture_byte_count += bytes;
    }
}

void Radiance_cascades_renderer::release_state_textures()
{
    const std::size_t state_texel_bytes = erhe::dataformat::get_format_size_bytes(c_state_format);
    for (int i = 0; i < c_max_radiance_cascades; ++i) {
        Cascade_textures& textures = m_cascade_textures[static_cast<std::size_t>(i)];
        if (!textures.state) {
            continue;
        }
        const std::size_t bytes = static_cast<std::size_t>(textures.state->get_width()) * static_cast<std::size_t>(textures.state->get_height()) * state_texel_bytes;
        m_cascade_byte_counts[static_cast<std::size_t>(i)] -= bytes;
        m_texture_byte_count -= bytes;
        textures.state.reset();
    }
}

void Radiance_cascades_renderer::release_field()
{
    m_field_irradiance.reset();
    m_field_distance.reset();
    m_field_probe_data.reset();
    m_reduce_weights_buffer.reset();
    m_texture_byte_count -= m_field_byte_count;
    m_field_byte_count          = 0;
    m_reduce_weights_byte_count = 0;
    m_field_tiles_per_row       = 0;
}

void Radiance_cascades_renderer::allocate_field(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    release_field();

    const Radiance_cascade& cascade0        = m_layout.cascades[0];
    const int               irradiance_tile = m_field_settings.irradiance_texels + (2 * c_field_border_texels);
    const int               distance_tile   = m_field_settings.distance_texels   + (2 * c_field_border_texels);
    // The DDGI atlas tiling (get_probe_field_tile()), sized by the larger
    // tile so both atlases stay within the texture size limit.
    m_field_tiles_per_row = get_probe_field_tiles_per_row(cascade0.grid.counts, std::max(irradiance_tile, distance_tile), m_graphics_device.get_info().max_texture_size);
    const int tile_rows   = get_probe_field_tile_rows(cascade0.grid.counts, m_field_tiles_per_row);

    const auto make_texture = [&](const char* debug_label, const erhe::dataformat::Format format, const int width, const int height) -> std::shared_ptr<Texture> {
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
        m_field_byte_count +=
            static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height) *
            erhe::dataformat::get_format_size_bytes(format);
        // Black irradiance, zero distance, inactive probes until the first
        // reduce writes them.
        command_buffer.clear_texture(*texture, {0.0, 0.0, 0.0, 0.0});
        command_buffer.transition_texture_layout(*texture, Image_layout::shader_read_only_optimal);
        return texture;
    };
    m_field_irradiance = make_texture("RC field irradiance", c_field_irradiance_format, m_field_tiles_per_row * irradiance_tile, tile_rows * irradiance_tile);
    m_field_distance   = make_texture("RC field distance",   c_field_distance_format,   m_field_tiles_per_row * distance_tile,   tile_rows * distance_tile  );
    m_field_probe_data = make_texture("RC field probe data", c_field_probe_data_format, m_field_tiles_per_row,                   tile_rows                  );
    m_texture_byte_count += m_field_byte_count;

    // Reduce weights: they depend only on the tile sizes and the depth
    // sharpness, so they are integrated here, on change, never per update.
    // Cold path: the temporaries allocate once per layout or settings edit.
    // The irradiance reduce reads merged cascade 0 at cascade 1's angular
    // resolution (tile side c_merged_cascade0_block x q0), the distance
    // reduce and the classification the cascade 0 distance statistics.
    const int          q0       = cascade0.tile_texels;
    const int          merged_q = c_merged_cascade0_block * q0;
    std::vector<float> irradiance_weights;
    std::vector<float> distance_weights;
    std::vector<float> solid_angles;
    compute_octahedral_lobe_weights(m_field_settings.irradiance_texels, merged_q, 1.0f, irradiance_weights);
    compute_octahedral_lobe_weights(m_field_settings.distance_texels,   q0,       m_field_settings.depth_sharpness, distance_weights);
    compute_octahedral_texel_solid_angles(q0, solid_angles);
    // Sparse per output texel: the lobes cover only part of the sphere (the
    // cosine lobe a hemisphere, the pow(cos, depth_sharpness) lobe a narrow
    // cone), so each output texel lists only the input texels whose weight
    // is above c_reduce_weight_threshold of its largest one - the dense loop
    // spent most of the reduce on zero weights. Layout per variant: a
    // header of (first pair, pair count) per output texel, then the
    // (input texel, weight) pairs, all as floats (indices are exact).
    std::vector<float> packed;
    const auto append_sparse = [&](const std::vector<float>& weights, const int tile_texels) -> uint32_t {
        const std::size_t input_count  = static_cast<std::size_t>(tile_texels) * static_cast<std::size_t>(tile_texels);
        const std::size_t output_count = weights.size() / input_count;
        const uint32_t    base         = static_cast<uint32_t>(packed.size());
        const std::size_t header       = packed.size();
        packed.resize(packed.size() + (2 * output_count), 0.0f);
        const std::size_t pairs_base   = packed.size();
        for (std::size_t n = 0; n < output_count; ++n) {
            float largest = 0.0f;
            for (std::size_t j = 0; j < input_count; ++j) {
                largest = std::max(largest, weights[(n * input_count) + j]);
            }
            const std::size_t first = (packed.size() - pairs_base) / 2;
            std::size_t       count = 0;
            for (std::size_t j = 0; j < input_count; ++j) {
                const float weight = weights[(n * input_count) + j];
                if (weight > (c_reduce_weight_threshold * largest)) {
                    packed.push_back(static_cast<float>(j));
                    packed.push_back(weight);
                    ++count;
                }
            }
            packed[header + (2 * n)]     = static_cast<float>(first);
            packed[header + (2 * n) + 1] = static_cast<float>(count);
        }
        return base;
    };
    m_reduce_irradiance_weights_offset = append_sparse(irradiance_weights, merged_q);
    m_reduce_distance_weights_offset   = append_sparse(distance_weights,   q0);
    m_reduce_solid_angle_offset        = static_cast<uint32_t>(packed.size());
    packed.insert(packed.end(), solid_angles.begin(), solid_angles.end());
    // Whole vec4s: the shader reads four floats per array element.
    while ((packed.size() % 4) != 0) {
        packed.push_back(0.0f);
    }
    m_reduce_weights_byte_count = packed.size() * sizeof(float);
    m_reduce_weights_buffer = std::make_unique<Buffer>(
        m_graphics_device,
        Buffer_create_info{
            .capacity_byte_count                    = m_reduce_weights_byte_count,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::none,
            .usage                                  = Buffer_usage::storage,
            .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::device_local,
            .init_data                              = packed.data(),
            .debug_label                            = erhe::utility::Debug_label{"RC reduce weights"}
        }
    );

    log_render->info(
        "Radiance_cascades_renderer: probe field {}x{}x{} probes, {} tiles per row, irradiance {} / distance {} texels, {:.1f} MB, reduce weights {} bytes",
        cascade0.grid.counts.x, cascade0.grid.counts.y, cascade0.grid.counts.z,
        m_field_tiles_per_row,
        m_field_settings.irradiance_texels, m_field_settings.distance_texels,
        static_cast<double>(m_field_byte_count) / (1024.0 * 1024.0),
        m_reduce_weights_byte_count
    );
}

void Radiance_cascades_renderer::record_visibility(
    erhe::graphics::Command_buffer&          command_buffer,
    const Scene_tlas::Frame&                 tlas_frame,
    const erhe::graphics::Ring_buffer_range& light_range,
    erhe::scene_renderer::Material_set&      material_set
)
{
    using namespace erhe::graphics;

    // Classification rays reach across the whole volume: the top cascade's
    // interval end exceeds the volume diagonal.
    const float classify_t_max = m_layout.cascades[static_cast<std::size_t>(m_layout.cascade_count - 1)].interval_end;

    const Scoped_gpu_timer visibility_timer{*m_visibility_timer, command_buffer};
    for (int i = 0; i < m_layout.cascade_count; ++i) {
        const std::size_t       index    = static_cast<std::size_t>(i);
        const Radiance_cascade& cascade  = m_layout.cascades[index];
        const bool              is_top   = (i == (m_layout.cascade_count - 1));
        const Radiance_cascade& upper    = is_top ? cascade : m_layout.cascades[index + 1];
        Texture&                state    = *m_cascade_textures[index].state;

        const std::size_t byte_count = m_visibility_block.get_size_bytes();
        Ring_buffer_range control_range = m_control_buffer->acquire(Ring_buffer_usage::CPU_write, byte_count);
        {
            std::span<std::byte> gpu_data = control_range.get_span();
            std::memset(gpu_data.data(), 0, byte_count);
            const glm::vec4  grid_origin  {cascade.grid.origin,  classify_t_max};
            const glm::vec4  grid_spacing {cascade.grid.spacing, 0.0f};
            const glm::uvec4 grid_counts{
                static_cast<uint32_t>(cascade.grid.counts.x),
                static_cast<uint32_t>(cascade.grid.counts.y),
                static_cast<uint32_t>(cascade.grid.counts.z),
                static_cast<uint32_t>(cascade.tiles_per_row)
            };
            const glm::vec4  upper_origin {upper.grid.origin,  0.0f};
            const glm::vec4  upper_spacing{upper.grid.spacing, 0.0f};
            const glm::uvec4 upper_counts{
                static_cast<uint32_t>(upper.grid.counts.x),
                static_cast<uint32_t>(upper.grid.counts.y),
                static_cast<uint32_t>(upper.grid.counts.z),
                is_top ? 0u : 1u
            };
            write(gpu_data, m_visibility_offsets.grid_origin,   as_span(grid_origin  ));
            write(gpu_data, m_visibility_offsets.grid_spacing,  as_span(grid_spacing ));
            write(gpu_data, m_visibility_offsets.grid_counts,   as_span(grid_counts  ));
            write(gpu_data, m_visibility_offsets.upper_origin,  as_span(upper_origin ));
            write(gpu_data, m_visibility_offsets.upper_spacing, as_span(upper_spacing));
            write(gpu_data, m_visibility_offsets.upper_counts,  as_span(upper_counts ));
            control_range.bytes_written(byte_count);
            control_range.close();
        }

        // Each dispatch writes only its own cascade's state texture.
        command_buffer.transition_texture_layout(state, Image_layout::general);
        {
            Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(m_visibility_bind_group_layout.get());
            encoder.set_compute_pipeline(*m_visibility_pipeline);
            m_light_buffer->bind_light_buffer(encoder, light_range);
            m_scene_tlas->bind_instance_records(encoder, tlas_frame);
            encoder.set_acceleration_structure(m_tlas_binding_point, *tlas_frame.acceleration_structure);
            m_control_buffer->bind(encoder, control_range);
            encoder.set_storage_image(m_state_binding_point, state);
            material_set.bind(encoder);
            encoder.dispatch_compute(
                static_cast<std::uintptr_t>((cascade.get_probe_count() + c_visibility_workgroup_size - 1) / c_visibility_workgroup_size),
                1,
                1
            );
        }
        control_range.release();
    }
    // The states become sampled textures for the merge.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    for (int i = 0; i < m_layout.cascade_count; ++i) {
        command_buffer.transition_texture_layout(*m_cascade_textures[static_cast<std::size_t>(i)].state, Image_layout::shader_read_only_optimal);
    }
    m_visibility_dirty = false;
    ++m_visibility_update_count;
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

    // A change message since the last update starts the temporal history
    // over at the cursor (doc/editor/ddgi.md "History reset").
    m_history.begin_update(total_texels, m_texel_cursor);
    // One jitter seed per update: every texel gets a new footprint point
    // each time it is traced (rc_trace.comp).
    const uint32_t jitter_seed = static_cast<uint32_t>(m_random_engine());
    const uint32_t jitter_mode = static_cast<uint32_t>(m_config.direction_jitter);

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
        int64_t traced    = 0; // texels of this update before the run
        m_trace_runs.clear();
        while (remaining > 0) {
            int cascade_index = m_layout.cascade_count - 1;
            while ((cascade_index > 0) && (m_cascade_texel_offsets[static_cast<std::size_t>(cascade_index)] > m_texel_cursor)) {
                --cascade_index;
            }
            const Radiance_cascade& cascade     = m_layout.cascades[static_cast<std::size_t>(cascade_index)];
            const int64_t           first_texel = m_texel_cursor - m_cascade_texel_offsets[static_cast<std::size_t>(cascade_index)];
            const int64_t           count       = std::min({remaining, cascade.get_texel_count() - first_texel, c_max_texels_per_dispatch});

            const bool needs_barrier = dispatched[static_cast<std::size_t>(cascade_index)];
            if (needs_barrier) {
                command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
            }
            dispatched[static_cast<std::size_t>(cascade_index)] = true;

            // The run's temporal history (first fill after an allocation,
            // running mean after a reset) and its place in the global
            // texel order, which keys the history and the jitter hash.
            const Trace_run& run = m_trace_runs.emplace_back(
                Trace_run{
                    .cascade       = cascade_index,
                    .first_texel   = first_texel,
                    .count         = count,
                    .history       = m_history.get_shader_parameters(traced),
                    .run           = glm::uvec4{
                        static_cast<uint32_t>(m_texel_cursor),
                        jitter_seed,
                        jitter_mode,
                        0u
                    },
                    .needs_barrier = needs_barrier
                }
            );

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
                    0u
                };
                const glm::vec4 params{hysteresis, 0.0f, 0.0f, 0.0f};
                write(gpu_data, m_control_offsets.grid_origin,  as_span(grid_origin ));
                write(gpu_data, m_control_offsets.grid_spacing, as_span(grid_spacing));
                write(gpu_data, m_control_offsets.grid_counts,  as_span(grid_counts ));
                write(gpu_data, m_control_offsets.dispatch,     as_span(dispatch    ));
                write(gpu_data, m_control_offsets.params,       as_span(params      ));
                write(gpu_data, m_control_offsets.history,      as_span(run.history ));
                write(gpu_data, m_control_offsets.run,          as_span(run.run     ));
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
                // Not referenced by the raw variants; the raw atlas stands in.
                encoder.set_storage_image(m_neighbours_binding_point, *m_cascade_textures[static_cast<std::size_t>(cascade_index)].raw);
                bind_trace_field(encoder);
                material_set.bind(encoder);
                encoder.dispatch_compute(
                    static_cast<std::uintptr_t>((count + c_trace_workgroup_size - 1) / c_trace_workgroup_size),
                    1,
                    1
                );
            }
            control_range.release();

            remaining      -= count;
            traced         += count;
            m_texel_cursor += count;
            if (m_texel_cursor >= total_texels) {
                m_texel_cursor = 0;
                ++m_completed_sweeps;
            }
        }
    }

    m_history.end_update(m_texels_per_update, hysteresis);

    // Merge mode per_neighbour_trace: the connecting segments of the same
    // runs, while the raw atlases and the distance texture (bound, not
    // referenced, by the neighbour variant) are still in general layout.
    if (m_merge_mode == Radiance_cascades_merge_mode::per_neighbour_trace) {
        record_neighbour_trace(command_buffer, tlas_frame, light_range, material_set);
    }

    // The raw atlases become sampled textures for the merge pass.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    for (int i = 0; i < m_layout.cascade_count; ++i) {
        command_buffer.transition_texture_layout(*m_cascade_textures[static_cast<std::size_t>(i)].raw, Image_layout::shader_read_only_optimal);
    }
    command_buffer.transition_texture_layout(*m_distance_texture, Image_layout::shader_read_only_optimal);
}

void Radiance_cascades_renderer::record_neighbour_trace(
    erhe::graphics::Command_buffer&          command_buffer,
    const Scene_tlas::Frame&                 tlas_frame,
    const erhe::graphics::Ring_buffer_range& light_range,
    erhe::scene_renderer::Material_set&      material_set
)
{
    using namespace erhe::graphics;

    const float hysteresis = std::clamp(m_config.hysteresis, 0.0f, 0.999f);
    for (int i = 0; (i + 1) < m_layout.cascade_count; ++i) {
        command_buffer.transition_texture_layout(*m_cascade_textures[static_cast<std::size_t>(i)].neighbours, Image_layout::general);
    }

    {
        const Scoped_gpu_timer neighbour_timer{*m_pass_timings[static_cast<std::size_t>(Rc_pass::neighbour_trace)].timer, command_buffer};

        // The runs of this frame's raw trace (record_trace()), with the same
        // temporal history, the same jitter (so the connecting segments start
        // at the end of the very ray the raw trace traced) and the same
        // barrier rule: a second run of a cascade in one frame is ordered
        // after the first. The top cascade has no upper cascade and no
        // neighbour atlas.
        for (const Trace_run& run : m_trace_runs) {
            if ((run.cascade + 1) >= m_layout.cascade_count) {
                continue;
            }
            const std::size_t       index   = static_cast<std::size_t>(run.cascade);
            const Radiance_cascade& cascade = m_layout.cascades[index];
            const Radiance_cascade& upper   = m_layout.cascades[index + 1];
            if (run.needs_barrier) {
                command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
            }

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
                    static_cast<uint32_t>(run.first_texel),
                    static_cast<uint32_t>(run.count),
                    static_cast<uint32_t>(cascade.tiles_per_row),
                    0u
                };
                const glm::vec4  params       {hysteresis, 0.0f, 0.0f, 0.0f};
                const glm::vec4  upper_origin {upper.grid.origin,  0.0f};
                const glm::vec4  upper_spacing{upper.grid.spacing, 0.0f};
                const glm::uvec4 upper_counts{
                    static_cast<uint32_t>(upper.grid.counts.x),
                    static_cast<uint32_t>(upper.grid.counts.y),
                    static_cast<uint32_t>(upper.grid.counts.z),
                    0u
                };
                write(gpu_data, m_control_offsets.grid_origin,   as_span(grid_origin  ));
                write(gpu_data, m_control_offsets.grid_spacing,  as_span(grid_spacing ));
                write(gpu_data, m_control_offsets.grid_counts,   as_span(grid_counts  ));
                write(gpu_data, m_control_offsets.dispatch,      as_span(dispatch     ));
                write(gpu_data, m_control_offsets.params,        as_span(params       ));
                write(gpu_data, m_control_offsets.upper_origin,  as_span(upper_origin ));
                write(gpu_data, m_control_offsets.upper_spacing, as_span(upper_spacing));
                write(gpu_data, m_control_offsets.upper_counts,  as_span(upper_counts ));
                write(gpu_data, m_control_offsets.history,       as_span(run.history  ));
                write(gpu_data, m_control_offsets.run,           as_span(run.run      ));
                control_range.bytes_written(byte_count);
                control_range.close();
            }
            {
                Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
                encoder.set_bind_group_layout(m_trace_bind_group_layout.get());
                encoder.set_compute_pipeline(*m_trace_neighbours.pipeline);
                m_light_buffer->bind_light_buffer(encoder, light_range);
                m_scene_tlas->bind_instance_records(encoder, tlas_frame);
                encoder.set_acceleration_structure(m_tlas_binding_point, *tlas_frame.acceleration_structure);
                m_control_buffer->bind(encoder, control_range);
                // The raw atlas and the distance texture are bound for the
                // layout's bindings; the neighbour variant references only
                // the neighbour atlas.
                encoder.set_storage_image(m_raw_binding_point,        *m_cascade_textures[index].raw);
                encoder.set_storage_image(m_distance_binding_point,   *m_distance_texture);
                encoder.set_storage_image(m_neighbours_binding_point, *m_cascade_textures[index].neighbours);
                bind_trace_field(encoder);
                material_set.bind(encoder);
                encoder.dispatch_compute(
                    static_cast<std::uintptr_t>((run.count + c_trace_workgroup_size - 1) / c_trace_workgroup_size),
                    1,
                    1
                );
            }
            control_range.release();
        }
    }

    // The neighbour atlases become sampled textures for the merge pass.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    for (int i = 0; (i + 1) < m_layout.cascade_count; ++i) {
        command_buffer.transition_texture_layout(*m_cascade_textures[static_cast<std::size_t>(i)].neighbours, Image_layout::shader_read_only_optimal);
    }
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
        // Only the visibility_masked variant references the state samplers
        // and only the per_neighbour_trace variant the neighbour atlas; the
        // raw atlas stands in for them. The top cascade has no neighbour
        // atlas and merges with the sky in every mode, so it takes the
        // interpolate variant in per_neighbour_trace.
        const bool              visibility   = (m_merge_mode == Radiance_cascades_merge_mode::visibility_masked);
        const bool              neighbours   = (m_merge_mode == Radiance_cascades_merge_mode::per_neighbour_trace) && !is_top;
        const Texture&          state        = visibility ? *m_cascade_textures[index].state : raw;
        const Texture&          upper_state  = (visibility && !is_top) ? *m_cascade_textures[index + 1].state : state;
        const Texture&          neighbour_atlas = neighbours ? *m_cascade_textures[index].neighbours : raw;
        const Compute_pipeline& pipeline =
            visibility ? *m_merge_visibility_pipeline :
            neighbours ? *m_merge_neighbours_pipeline :
                         *m_merge_pipeline;

        uint32_t flags = 0u;
        if (is_top) {
            flags |= c_merge_flag_top;
        }
        if ((mask & (1u << static_cast<uint32_t>(i))) != 0u) {
            flags |= c_merge_flag_mask_radiance;
        }
        if (i == 0) {
            flags |= c_merge_flag_child_resolution;
        }
        // One thread per merged texel: cascade 0's merged atlas has
        // c_merged_cascade0_block^2 texels per raw texel.
        const int width  = merged.get_width();
        const int height = merged.get_height();

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
            encoder.set_compute_pipeline(pipeline);
            m_control_buffer->bind(encoder, control_range);
            encoder.set_sampled_image(c_merge_raw_binding_point,   raw,          *m_merge_sampler);
            encoder.set_sampled_image(c_merge_upper_binding_point, upper_merged, *m_merge_sampler);
            encoder.set_sampled_image(c_merge_state_binding_point,       state,       *m_merge_sampler);
            encoder.set_sampled_image(c_merge_upper_state_binding_point, upper_state, *m_merge_sampler);
            encoder.set_sampled_image(c_merge_neighbours_binding_point,  neighbour_atlas, *m_merge_sampler);
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

void Radiance_cascades_renderer::record_reduce(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    const Radiance_cascade& cascade0    = m_layout.cascades[0];
    const int               probe_count = cascade0.get_probe_count();

    const std::size_t byte_count = m_reduce_block.get_size_bytes();
    Ring_buffer_range control_range = m_control_buffer->acquire(Ring_buffer_usage::CPU_write, byte_count);
    {
        std::span<std::byte> gpu_data = control_range.get_span();
        std::memset(gpu_data.data(), 0, byte_count);
        const glm::uvec4 grid_counts{
            static_cast<uint32_t>(cascade0.grid.counts.x),
            static_cast<uint32_t>(cascade0.grid.counts.y),
            static_cast<uint32_t>(cascade0.grid.counts.z),
            static_cast<uint32_t>(cascade0.tile_texels)
        };
        const glm::uvec4 params{
            static_cast<uint32_t>(cascade0.tiles_per_row),
            static_cast<uint32_t>(m_field_tiles_per_row),
            static_cast<uint32_t>(m_field_settings.irradiance_texels),
            static_cast<uint32_t>(m_field_settings.distance_texels)
        };
        const glm::uvec4 weights{
            m_reduce_irradiance_weights_offset,
            m_reduce_distance_weights_offset,
            m_reduce_solid_angle_offset,
            static_cast<uint32_t>(c_merged_cascade0_block * cascade0.tile_texels) // merged cascade 0 tile side
        };
        const glm::vec4 limits{m_layout.r0, 0.0f, 0.0f, 0.0f};
        write(gpu_data, m_reduce_offsets.grid_counts, as_span(grid_counts));
        write(gpu_data, m_reduce_offsets.params,      as_span(params     ));
        write(gpu_data, m_reduce_offsets.weights,     as_span(weights    ));
        write(gpu_data, m_reduce_offsets.limits,      as_span(limits     ));
        control_range.bytes_written(byte_count);
        control_range.close();
    }

    // Merged cascade 0 (left shader_read_only_optimal by the merge) and the
    // distance texture (by the trace) are sampled; each variant writes its
    // own atlas, the irradiance variant also the probe data, so the two
    // dispatches share no written image.
    command_buffer.transition_texture_layout(*m_field_irradiance, Image_layout::general);
    command_buffer.transition_texture_layout(*m_field_distance,   Image_layout::general);
    command_buffer.transition_texture_layout(*m_field_probe_data, Image_layout::general);
    {
        const Scoped_gpu_timer reduce_timer{*m_pass_timings[static_cast<std::size_t>(Rc_pass::reduce)].timer, command_buffer};
        const int groups_x = std::min(probe_count, c_reduce_dispatch_row);
        const int groups_y = (probe_count + groups_x - 1) / groups_x;
        const auto dispatch = [&](const Reduce_pass& pass, Texture& atlas, Texture* probe_data) {
            Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(pass.bind_group_layout.get());
            encoder.set_compute_pipeline(*pass.pipeline);
            m_control_buffer->bind(encoder, control_range);
            encoder.set_buffer(Buffer_target::storage, m_reduce_weights_buffer.get(), 0, m_reduce_weights_byte_count, c_reduce_weights_binding_point);
            encoder.set_sampled_image(c_reduce_merged_binding_point,   *m_cascade_textures[0].merged, *m_merge_sampler);
            encoder.set_sampled_image(c_reduce_distance_binding_point, *m_distance_texture,           *m_merge_sampler);
            encoder.set_storage_image(c_reduce_atlas_binding_point, atlas);
            if (probe_data != nullptr) {
                encoder.set_storage_image(c_reduce_probe_data_binding_point, *probe_data);
            }
            encoder.dispatch_compute(static_cast<std::uintptr_t>(groups_x), static_cast<std::uintptr_t>(groups_y), 1);
        };
        dispatch(m_reduce_irradiance, *m_field_irradiance, m_field_probe_data.get());
        dispatch(m_reduce_distance,   *m_field_distance,   nullptr);
    }
    control_range.release();

    // The field atlases become sampled textures for the forward pass, the
    // irradiance query and the readback.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    command_buffer.transition_texture_layout(*m_field_irradiance, Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_field_distance,   Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_field_probe_data, Image_layout::shader_read_only_optimal);
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
    const Cascade_textures& textures      = m_cascade_textures[static_cast<std::size_t>(cascade_index)];
    // The previewed atlas' own size: cascade 0's merged atlas is at
    // cascade 1's angular resolution.
    // The distance channel reads the cascade 0 distance statistics at the
    // raw atlas' texel addresses, so it always takes the raw atlas' size.
    const bool              merged_source = (m_preview_source == Rc_preview_source::merged) && (m_preview_channel != Rc_preview_channel::distance);
    const Texture&          previewed     = merged_source ? *textures.merged : *textures.raw;
    const int               width         = previewed.get_width();
    const int               height        = previewed.get_height();
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

    Texture& atlas = merged_source ? *textures.merged : *textures.raw;
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
        const std::size_t atlas_bytes  = static_cast<std::size_t>(m_layout.cascades[static_cast<std::size_t>(i)].get_atlas_texel_count()) * radiance_texel_bytes;
        const std::size_t merged_block = (i == 0) ? static_cast<std::size_t>(c_merged_cascade0_block * c_merged_cascade0_block) : 1u;
        m_readback_raw_offsets[static_cast<std::size_t>(i)] = offset;
        offset = round_up(offset + atlas_bytes, c_readback_alignment);
        m_readback_merged_offsets[static_cast<std::size_t>(i)] = offset;
        offset = round_up(offset + (merged_block * atlas_bytes), c_readback_alignment);
    }
    m_readback_distance_offset = offset;
    offset = round_up(offset + (static_cast<std::size_t>(m_layout.cascades[0].get_atlas_texel_count()) * distance_texel_bytes), c_readback_alignment);
    const std::size_t state_texel_bytes = erhe::dataformat::get_format_size_bytes(c_state_format);
    m_readback_has_states = static_cast<bool>(m_cascade_textures[0].state);
    if (m_readback_has_states) {
        for (int i = 0; i < m_layout.cascade_count; ++i) {
            const Radiance_cascade& cascade = m_layout.cascades[static_cast<std::size_t>(i)];
            m_readback_state_offsets[static_cast<std::size_t>(i)] = offset;
            offset = round_up(offset + (static_cast<std::size_t>(cascade.tiles_per_row) * static_cast<std::size_t>(cascade.tile_rows) * state_texel_bytes), c_readback_alignment);
        }
    }
    m_readback_has_field = static_cast<bool>(m_field_irradiance);
    if (m_readback_has_field) {
        m_readback_field_parameters       = get_forward_parameters();
        m_readback_field_irradiance_width = m_field_irradiance->get_width();
        m_readback_field_distance_width   = m_field_distance->get_width();
        m_readback_field_probe_data_width = m_field_probe_data->get_width();
        const auto texture_bytes = [](const Texture& texture, const erhe::dataformat::Format format) -> std::size_t {
            return static_cast<std::size_t>(texture.get_width()) * static_cast<std::size_t>(texture.get_height()) * erhe::dataformat::get_format_size_bytes(format);
        };
        m_readback_field_irradiance_offset = offset;
        offset = round_up(offset + texture_bytes(*m_field_irradiance, c_field_irradiance_format), c_readback_alignment);
        m_readback_field_distance_offset   = offset;
        offset = round_up(offset + texture_bytes(*m_field_distance, c_field_distance_format), c_readback_alignment);
        m_readback_field_probe_data_offset = offset;
        offset = round_up(offset + texture_bytes(*m_field_probe_data, c_field_probe_data_format), c_readback_alignment);
    }
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
            erhe::graphics::Texture_location{.texture = &texture, .slice = 0, .level = 0, .origin = glm::ivec3{0, 0, 0}},
            glm::ivec3{width, height, 1},
            erhe::graphics::Buffer_texel_location{.buffer = m_readback_buffer.get(), .offset = static_cast<std::uintptr_t>(destination_offset), .bytes_per_row = static_cast<std::uintptr_t>(bytes_per_row), .bytes_per_image = static_cast<std::uintptr_t>(bytes_per_row * static_cast<std::size_t>(height))}
        );
    };
    for (int i = 0; i < m_layout.cascade_count; ++i) {
        copy(*m_cascade_textures[static_cast<std::size_t>(i)].raw,    radiance_texel_bytes, m_readback_raw_offsets   [static_cast<std::size_t>(i)]);
        copy(*m_cascade_textures[static_cast<std::size_t>(i)].merged, radiance_texel_bytes, m_readback_merged_offsets[static_cast<std::size_t>(i)]);
    }
    copy(*m_distance_texture, distance_texel_bytes, m_readback_distance_offset);
    if (m_readback_has_states) {
        for (int i = 0; i < m_layout.cascade_count; ++i) {
            copy(*m_cascade_textures[static_cast<std::size_t>(i)].state, state_texel_bytes, m_readback_state_offsets[static_cast<std::size_t>(i)]);
        }
    }
    if (m_readback_has_field) {
        copy(*m_field_irradiance, erhe::dataformat::get_format_size_bytes(c_field_irradiance_format), m_readback_field_irradiance_offset);
        copy(*m_field_distance,   erhe::dataformat::get_format_size_bytes(c_field_distance_format),   m_readback_field_distance_offset);
        copy(*m_field_probe_data, erhe::dataformat::get_format_size_bytes(c_field_probe_data_format), m_readback_field_probe_data_offset);
    }

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
        int64_t                 visible_upper_count{0};
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
                            if ((i == 0) && (read_distance_statistics(glm::ivec3{x, y, z}, glm::ivec2{u, v}).z > 0.5f)) {
                                ++backface_count;
                                probe_has_backface = true;
                            }
                        }
                    }
                    if (probe_has_backface) {
                        ++summary.backface_probe_count;
                    }
                    const uint32_t state = m_readback_has_states ? read_probe_state(i, glm::ivec3{x, y, z}) : 0u;
                    if ((state & c_state_inside) != 0u) {
                        ++summary.inside_probe_count;
                    }
                    if (i < (m_readback_layout.cascade_count - 1)) {
                        visible_upper_count += static_cast<int64_t>(std::popcount(state & c_state_upper_visible_mask));
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
        summary.upper_visible_fraction = static_cast<float>(
            static_cast<double>(visible_upper_count) / std::max(1.0, 8.0 * static_cast<double>(cascade.get_probe_count()))
        );
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
    if (cascade_index > 0) {
        return read_radiance_texel(m_readback_merged_offsets[static_cast<std::size_t>(cascade_index)], cascade_index, probe, texel);
    }
    // Cascade 0 is merged at cascade 1's angular resolution: the texel's
    // merged value is the mean of its child texels.
    glm::vec4 sum{0.0f};
    for (int child = 0; child < (c_merged_cascade0_block * c_merged_cascade0_block); ++child) {
        sum += read_merged_child_texel(probe, texel, child);
    }
    return sum / static_cast<float>(c_merged_cascade0_block * c_merged_cascade0_block);
}

auto Radiance_cascades_renderer::read_merged_child_texel(const glm::ivec3& probe, const glm::ivec2& texel, const int child) const -> glm::vec4
{
    const Radiance_cascade& cascade     = m_readback_layout.cascades[0];
    const int               probe_index = probe.x + (cascade.grid.counts.x * (probe.y + (cascade.grid.counts.y * probe.z)));
    const glm::ivec2        child_texel =
        (cascade.get_tile_origin(probe_index) * c_merged_cascade0_block) +
        (texel * c_merged_cascade0_block) +
        glm::ivec2{child % c_merged_cascade0_block, child / c_merged_cascade0_block};
    const std::size_t       width       = static_cast<std::size_t>(cascade.get_atlas_width() * c_merged_cascade0_block);
    const std::size_t       texel_bytes = erhe::dataformat::get_format_size_bytes(c_radiance_format);
    const std::size_t       offset      =
        m_readback_merged_offsets[0] +
        (((static_cast<std::size_t>(child_texel.y) * width) + static_cast<std::size_t>(child_texel.x)) * texel_bytes);
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

auto Radiance_cascades_renderer::readback_has_field() const -> bool
{
    return m_readback_has_field;
}

auto Radiance_cascades_renderer::get_readback_field_parameters() const -> const erhe::scene_renderer::Ddgi_parameters&
{
    return m_readback_field_parameters;
}

auto Radiance_cascades_renderer::read_field_irradiance_texel(const glm::ivec3& probe, const glm::ivec2& texel) const -> glm::vec3
{
    ERHE_VERIFY(m_readback_has_field);
    const erhe::scene_renderer::Ddgi_parameters& parameters = m_readback_field_parameters;
    const int         tile_size   = parameters.irradiance_texels + (2 * c_field_border_texels);
    const glm::ivec2  atlas_texel = (get_probe_field_tile(probe, parameters.grid_counts, parameters.tiles_per_row) * tile_size) + glm::ivec2{c_field_border_texels} + texel;
    const std::size_t texel_bytes = erhe::dataformat::get_format_size_bytes(c_field_irradiance_format);
    const std::size_t offset      =
        m_readback_field_irradiance_offset +
        (((static_cast<std::size_t>(atlas_texel.y) * static_cast<std::size_t>(m_readback_field_irradiance_width)) + static_cast<std::size_t>(atlas_texel.x)) * texel_bytes);
    ERHE_VERIFY((offset + texel_bytes) <= m_readback_snapshot.size());
    std::array<uint16_t, 4> halves{};
    std::memcpy(halves.data(), m_readback_snapshot.data() + offset, sizeof(halves));
    return glm::vec3{glm::unpackHalf1x16(halves[0]), glm::unpackHalf1x16(halves[1]), glm::unpackHalf1x16(halves[2])};
}

auto Radiance_cascades_renderer::read_field_distance_texel(const glm::ivec3& probe, const glm::ivec2& texel) const -> glm::vec2
{
    ERHE_VERIFY(m_readback_has_field);
    const erhe::scene_renderer::Ddgi_parameters& parameters = m_readback_field_parameters;
    const int         tile_size   = parameters.distance_texels + (2 * c_field_border_texels);
    const glm::ivec2  atlas_texel = (get_probe_field_tile(probe, parameters.grid_counts, parameters.tiles_per_row) * tile_size) + glm::ivec2{c_field_border_texels} + texel;
    const std::size_t texel_bytes = erhe::dataformat::get_format_size_bytes(c_field_distance_format);
    const std::size_t offset      =
        m_readback_field_distance_offset +
        (((static_cast<std::size_t>(atlas_texel.y) * static_cast<std::size_t>(m_readback_field_distance_width)) + static_cast<std::size_t>(atlas_texel.x)) * texel_bytes);
    ERHE_VERIFY((offset + texel_bytes) <= m_readback_snapshot.size());
    std::array<uint16_t, 2> halves{};
    std::memcpy(halves.data(), m_readback_snapshot.data() + offset, sizeof(halves));
    return glm::vec2{glm::unpackHalf1x16(halves[0]), glm::unpackHalf1x16(halves[1])};
}

auto Radiance_cascades_renderer::read_field_probe_data(const glm::ivec3& probe) const -> glm::vec4
{
    ERHE_VERIFY(m_readback_has_field);
    const erhe::scene_renderer::Ddgi_parameters& parameters = m_readback_field_parameters;
    const glm::ivec2  tile   = get_probe_field_tile(probe, parameters.grid_counts, parameters.tiles_per_row);
    const std::size_t offset =
        m_readback_field_probe_data_offset +
        (((static_cast<std::size_t>(tile.y) * static_cast<std::size_t>(m_readback_field_probe_data_width)) + static_cast<std::size_t>(tile.x)) * sizeof(glm::vec4));
    ERHE_VERIFY((offset + sizeof(glm::vec4)) <= m_readback_snapshot.size());
    glm::vec4 value{0.0f};
    std::memcpy(&value, m_readback_snapshot.data() + offset, sizeof(glm::vec4));
    return value;
}

auto Radiance_cascades_renderer::readback_has_probe_states() const -> bool
{
    return m_readback_has_states;
}

auto Radiance_cascades_renderer::read_probe_state(const int cascade_index, const glm::ivec3& probe) const -> uint32_t
{
    ERHE_VERIFY(m_readback_has_states);
    ERHE_VERIFY((cascade_index >= 0) && (cascade_index < m_readback_layout.cascade_count));
    const Radiance_cascade& cascade     = m_readback_layout.cascades[static_cast<std::size_t>(cascade_index)];
    const int               probe_index = probe.x + (cascade.grid.counts.x * (probe.y + (cascade.grid.counts.y * probe.z)));
    const std::size_t       offset      = m_readback_state_offsets[static_cast<std::size_t>(cascade_index)] + (static_cast<std::size_t>(probe_index) * sizeof(float));
    ERHE_VERIFY((offset + sizeof(float)) <= m_readback_snapshot.size());
    float state = 0.0f;
    std::memcpy(&state, m_readback_snapshot.data() + offset, sizeof(float));
    return static_cast<uint32_t>(state + 0.5f);
}

auto Radiance_cascades_renderer::read_distance_statistics(const glm::ivec3& probe, const glm::ivec2& texel) const -> glm::vec4
{
    const Radiance_cascade& cascade     = m_readback_layout.cascades[0];
    const int               probe_index = probe.x + (cascade.grid.counts.x * (probe.y + (cascade.grid.counts.y * probe.z)));
    const glm::ivec2        atlas_texel = cascade.get_tile_origin(probe_index) + texel;
    const std::size_t       offset      =
        m_readback_distance_offset +
        (((static_cast<std::size_t>(atlas_texel.y) * static_cast<std::size_t>(cascade.get_atlas_width())) + static_cast<std::size_t>(atlas_texel.x)) * sizeof(glm::vec4));
    ERHE_VERIFY((offset + sizeof(glm::vec4)) <= m_readback_snapshot.size());
    glm::vec4 statistics{0.0f};
    std::memcpy(&statistics, m_readback_snapshot.data() + offset, sizeof(glm::vec4));
    return statistics;
}

void Radiance_cascades_renderer::bind_trace_field(erhe::graphics::Compute_command_encoder& encoder)
{
    // Every trace variant declares the field samplers (erhe_ray_hit.glsl
    // ERHE_RT_INDIRECT_FIELD); the light block decides whether a hit
    // samples them (bounces multi) or the flat ambient.
    encoder.set_sampled_image(m_field_irradiance_binding_point, *m_field_irradiance, *m_field_sampler);
    encoder.set_sampled_image(m_field_distance_binding_point,   *m_field_distance,   *m_field_sampler);
    encoder.set_sampled_image(m_field_probe_data_binding_point, *m_field_probe_data, *m_field_sampler);
}

void Radiance_cascades_renderer::tick(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root)
{
    using namespace erhe::graphics;

    if (!is_supported() || !is_selected()) {
        return;
    }
    // Probe overlay: take a retired copy; when the overlay is off, release
    // its buffer once no copy is in flight (nothing is copied while off).
    poll_probe_overlay();
    const Radiance_cascades_probe_overlay overlay_mode = m_config.debug_draw_probes;
    if (
        (overlay_mode == Radiance_cascades_probe_overlay::none) &&
        !m_overlay_in_flight &&
        (m_overlay_buffer || (m_overlay_summary.cascade >= 0))
    ) {
        m_overlay_buffer.reset();
        m_overlay_summary = Probe_overlay_summary{};
        m_overlay_probes.clear();
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
    // Bounces multi: the light block carries this producer's field (written
    // by the previous update's reduce), and shade_surface() takes a hit's
    // ambient term from it (erhe_ray_hit.glsl ERHE_RT_INDIRECT_FIELD).
    // Single: no field in the light block, the flat ambient.
    // The feedback is the physical irradiance: intensity is a display
    // multiplier the forward pass applies once, and folded into every
    // bounce it would scale the gain per bounce (intensity x albedo > 1
    // diverges).
    erhe::scene_renderer::Ddgi_parameters field_parameters = get_forward_parameters();
    field_parameters.intensity = 1.0f;
    const bool multi_bounce = (m_config.bounces == Indirect_diffuse_bounces::multi) && has_field();
    Ring_buffer_range light_range = m_light_buffer->update(
        m_light_projections.get(),
        ambient,
        0u,
        multi_bounce ? &field_parameters : nullptr
    );

    sample_pass_timings();
    const uint64_t visibility_ns = m_visibility_timer->last_result();
    if (visibility_ns != 0) {
        m_visibility_last_ns = visibility_ns;
    }
    ++m_update_count;
    if (m_merge_mode == Radiance_cascades_merge_mode::visibility_masked) {
        // After a refit or a switch into this mode the state textures are
        // missing; they are (re)allocated here, where a command buffer is.
        if (!m_cascade_textures[0].state) {
            allocate_state_textures(command_buffer);
            m_visibility_dirty = true;
        }
        if (m_visibility_dirty) {
            record_visibility(command_buffer, tlas_frame, light_range, material_set);
        }
    }
    record_trace(command_buffer, tlas_frame, light_range, material_set);

    light_range.release();
    tlas_frame.instance_records.release();
    material_set.unbind(command_buffer);

    // Every merged texel depends on raw texels of its own and every higher
    // cascade, so each update that traced re-merges all cascades, and
    // reduces merged cascade 0 into the probe field.
    record_merge(command_buffer, ambient);
    record_reduce(command_buffer);

    if (m_preview_requested) {
        m_preview_requested = false;
        record_preview(command_buffer);
    }
    bool recorded_readback = false;
    if (m_readback_state == Rc_readback_state::requested) {
        record_texel_readback(command_buffer);
        recorded_readback = true;
    }
    if (
        (overlay_mode != Radiance_cascades_probe_overlay::none) &&
        !m_overlay_in_flight &&
        (m_graphics_device.get_frame_index() >= m_overlay_next_frame)
    ) {
        recorded_readback = record_probe_overlay_readback(command_buffer, overlay_mode) || recorded_readback;
    }
    if (recorded_readback) {
        // Transfer writes -> host reads once the frame's fence has signalled.
        command_buffer.memory_barrier(Memory_barrier_mask::client_mapped_buffer_barrier_bit);
    }
}

auto Radiance_cascades_renderer::record_probe_overlay_readback(
    erhe::graphics::Command_buffer&       command_buffer,
    const Radiance_cascades_probe_overlay mode
) -> bool
{
    using namespace erhe::graphics;

    const int               cascade_index = std::clamp(m_config.debug_draw_cascade, 0, m_layout.cascade_count - 1);
    const Radiance_cascade& cascade       = m_layout.cascades[static_cast<std::size_t>(cascade_index)];
    const Cascade_textures& textures      = m_cascade_textures[static_cast<std::size_t>(cascade_index)];
    m_overlay_next_frame = m_graphics_device.get_frame_index() + c_probe_overlay_interval_frames;

    // Layout of the copy: the cascade's probe state texture (visibility_masked
    // mode), the field probe data (cascade 0: the reduce's classification),
    // the cascade's merged atlas (state_and_irradiance), each at an aligned
    // offset, tightly packed rows.
    Probe_overlay_copy copy{};
    copy.cascade        = cascade_index;
    copy.layout_cascade = cascade;
    copy.update_count   = m_update_count;
    copy.has_state      = static_cast<bool>(textures.state);
    copy.has_probe_data = (cascade_index == 0) && static_cast<bool>(m_field_probe_data);
    copy.has_merged     = (mode == Radiance_cascades_probe_overlay::state_and_irradiance) && static_cast<bool>(textures.merged);
    copy.merged_block   = (cascade_index == 0) ? c_merged_cascade0_block : 1;
    const auto texture_bytes = [](const Texture& texture, const erhe::dataformat::Format format) -> std::size_t {
        return static_cast<std::size_t>(texture.get_width()) * static_cast<std::size_t>(texture.get_height()) * erhe::dataformat::get_format_size_bytes(format);
    };
    std::size_t offset = 0;
    if (copy.has_state) {
        copy.state_offset = offset;
        offset = round_up(offset + texture_bytes(*textures.state, c_state_format), c_readback_alignment);
    }
    if (copy.has_probe_data) {
        copy.probe_data_offset   = offset;
        copy.probe_data_width    = m_field_probe_data->get_width();
        copy.field_tiles_per_row = m_field_tiles_per_row;
        offset = round_up(offset + texture_bytes(*m_field_probe_data, c_field_probe_data_format), c_readback_alignment);
    }
    if (copy.has_merged) {
        copy.merged_offset = offset;
        offset = round_up(offset + texture_bytes(*textures.merged, c_radiance_format), c_readback_alignment);
    }
    copy.byte_count = offset;
    if (copy.byte_count == 0) {
        // Nothing to classify or reduce (an upper cascade outside the
        // visibility_masked mode, state only): the overlay draws the
        // positions, unclassified.
        m_overlay_grid           = cascade.grid;
        m_overlay_irradiance_max = 0.0f;
        m_overlay_summary        = Probe_overlay_summary{
            .cascade      = cascade_index,
            .probe_count  = cascade.get_probe_count(),
            .unclassified = cascade.get_probe_count(),
            .update_count = m_update_count
        };
        m_overlay_probes.clear();
        m_overlay_probes.resize(static_cast<std::size_t>(cascade.get_probe_count()));
        return false;
    }

    // Grown to the largest copy asked for while the overlay is on; released
    // when it is turned off (tick()).
    if (!m_overlay_buffer || (m_overlay_buffer->get_capacity_byte_count() < copy.byte_count)) {
        m_overlay_buffer = std::make_unique<Buffer>(
            m_graphics_device,
            Buffer_create_info{
                .capacity_byte_count                    = copy.byte_count,
                .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
                .usage                                  = Buffer_usage::transfer_dst | Buffer_usage::storage,
                .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read | Memory_property_flag_bit_mask::host_write,
                .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_coherent | Memory_property_flag_bit_mask::host_persistent,
                .debug_label                            = erhe::utility::Debug_label{"RC probe overlay readback"}
            }
        );
    }
    // copy_from_texture() moves each image from its tracked layout to
    // transfer_src and back, as record_texel_readback() relies on.
    const auto copy_texture = [&](Texture& texture, const std::size_t texel_bytes, const std::size_t destination_offset) {
        const int         width         = texture.get_width();
        const int         height        = texture.get_height();
        const std::size_t bytes_per_row = static_cast<std::size_t>(width) * texel_bytes;
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(
            erhe::graphics::Texture_location{.texture = &texture, .slice = 0, .level = 0, .origin = glm::ivec3{0, 0, 0}},
            glm::ivec3{width, height, 1},
            erhe::graphics::Buffer_texel_location{.buffer = m_overlay_buffer.get(), .offset = static_cast<std::uintptr_t>(destination_offset), .bytes_per_row = static_cast<std::uintptr_t>(bytes_per_row), .bytes_per_image = static_cast<std::uintptr_t>(bytes_per_row * static_cast<std::size_t>(height))}
        );
    };
    if (copy.has_state) {
        copy_texture(*textures.state, erhe::dataformat::get_format_size_bytes(c_state_format), copy.state_offset);
    }
    if (copy.has_probe_data) {
        copy_texture(*m_field_probe_data, erhe::dataformat::get_format_size_bytes(c_field_probe_data_format), copy.probe_data_offset);
    }
    if (copy.has_merged) {
        copy_texture(*textures.merged, erhe::dataformat::get_format_size_bytes(c_radiance_format), copy.merged_offset);
    }
    m_overlay_copy      = copy;
    m_overlay_in_flight = true;
    m_overlay_frame     = m_graphics_device.get_frame_index();
    ++m_overlay_readback_count;
    return true;
}

void Radiance_cascades_renderer::poll_probe_overlay()
{
    if (!m_overlay_in_flight || !m_graphics_device.is_frame_completed(m_overlay_frame)) {
        return;
    }
    m_overlay_in_flight = false;
    if (m_config.debug_draw_probes == Radiance_cascades_probe_overlay::none) {
        return; // turned off while the copy was in flight
    }

    const Probe_overlay_copy&  copy    = m_overlay_copy;
    const Radiance_cascade&    cascade = copy.layout_cascade;
    const std::span<std::byte> mapped  = m_overlay_buffer->map_bytes(0, copy.byte_count);
    m_overlay_buffer->invalidate(0, copy.byte_count);
    const std::byte* const data = mapped.data();

    // Cosine weights toward +Y of the merged tile's texels: max(0, w.y) x
    // the texel's solid angle, so the weighted mean is the cosine-weighted
    // mean radiance (E / pi, what the irradiance atlas stores) for a +Y
    // normal. They depend only on the tile side.
    const int merged_tile_texels = cascade.tile_texels * copy.merged_block;
    if (copy.has_merged && (m_overlay_weights_tile_texels != merged_tile_texels)) {
        compute_octahedral_texel_solid_angles(merged_tile_texels, m_overlay_weights);
        for (int v = 0; v < merged_tile_texels; ++v) {
            for (int u = 0; u < merged_tile_texels; ++u) {
                const std::size_t index = (static_cast<std::size_t>(v) * static_cast<std::size_t>(merged_tile_texels)) + static_cast<std::size_t>(u);
                m_overlay_weights[index] *= std::max(0.0f, get_texel_direction(glm::ivec2{u, v}, merged_tile_texels).y);
            }
        }
        m_overlay_weights_tile_texels = merged_tile_texels;
    }

    Probe_overlay_summary summary{
        .cascade      = copy.cascade,
        .probe_count  = cascade.get_probe_count(),
        .irradiance   = copy.has_merged,
        .update_count = copy.update_count
    };
    m_overlay_probes.clear();
    m_overlay_irradiance_max = 0.0f;
    const std::size_t radiance_texel_bytes = erhe::dataformat::get_format_size_bytes(c_radiance_format);
    const std::size_t merged_atlas_width   = static_cast<std::size_t>(cascade.get_atlas_width()) * static_cast<std::size_t>(copy.merged_block);
    for (int z = 0; z < cascade.grid.counts.z; ++z) {
        for (int y = 0; y < cascade.grid.counts.y; ++y) {
            for (int x = 0; x < cascade.grid.counts.x; ++x) {
                const glm::ivec3    coords      = glm::ivec3{x, y, z};
                const int           probe_index = x + (cascade.grid.counts.x * (y + (cascade.grid.counts.y * z)));
                Probe_overlay_probe probe{};
                if (copy.has_probe_data || copy.has_state) {
                    probe.state = Rc_probe_overlay_state::active;
                }
                if (copy.has_probe_data) {
                    // Probe data w: 1 active, 0 inactive (the reduce's
                    // classification of the field, "Reduce").
                    const glm::ivec2  tile         = get_probe_field_tile(coords, cascade.grid.counts, copy.field_tiles_per_row);
                    const std::size_t texel_offset = copy.probe_data_offset +
                        (((static_cast<std::size_t>(tile.y) * static_cast<std::size_t>(copy.probe_data_width)) + static_cast<std::size_t>(tile.x)) * sizeof(glm::vec4));
                    glm::vec4 probe_data{0.0f};
                    std::memcpy(&probe_data, data + texel_offset, sizeof(glm::vec4));
                    if (probe_data.w < 0.5f) {
                        probe.state = Rc_probe_overlay_state::inside;
                    }
                }
                if (copy.has_state) {
                    // One integer-valued float per probe at its probe index
                    // (rc_visibility.comp).
                    float state = 0.0f;
                    std::memcpy(&state, data + copy.state_offset + (static_cast<std::size_t>(probe_index) * sizeof(float)), sizeof(float));
                    if ((static_cast<uint32_t>(state + 0.5f) & c_state_inside) != 0u) {
                        probe.state = Rc_probe_overlay_state::inside;
                    }
                }
                if (copy.has_merged) {
                    const glm::ivec2 tile_origin = cascade.get_tile_origin(probe_index) * copy.merged_block;
                    glm::vec3 sum{0.0f};
                    float     weight_sum = 0.0f;
                    for (int v = 0; v < merged_tile_texels; ++v) {
                        for (int u = 0; u < merged_tile_texels; ++u) {
                            const float weight = m_overlay_weights[(static_cast<std::size_t>(v) * static_cast<std::size_t>(merged_tile_texels)) + static_cast<std::size_t>(u)];
                            if (weight <= 0.0f) {
                                continue;
                            }
                            const std::size_t texel_offset = copy.merged_offset +
                                (((static_cast<std::size_t>(tile_origin.y + v) * merged_atlas_width) + static_cast<std::size_t>(tile_origin.x + u)) * radiance_texel_bytes);
                            std::array<uint16_t, 4> halves{};
                            std::memcpy(halves.data(), data + texel_offset, sizeof(halves));
                            sum += weight * glm::vec3{glm::unpackHalf1x16(halves[0]), glm::unpackHalf1x16(halves[1]), glm::unpackHalf1x16(halves[2])};
                            weight_sum += weight;
                        }
                    }
                    probe.irradiance = (weight_sum > 0.0f) ? (sum / weight_sum) : glm::vec3{0.0f};
                    m_overlay_irradiance_max = std::max(m_overlay_irradiance_max, std::max(probe.irradiance.r, std::max(probe.irradiance.g, probe.irradiance.b)));
                }
                switch (probe.state) {
                    case Rc_probe_overlay_state::active:       ++summary.active;       break;
                    case Rc_probe_overlay_state::inside:       ++summary.inside;       break;
                    case Rc_probe_overlay_state::unclassified: ++summary.unclassified; break;
                }
                m_overlay_probes.push_back(probe);
            }
        }
    }
    m_overlay_buffer->unmap();
    m_overlay_grid    = cascade.grid;
    m_overlay_summary = summary;
}

auto Radiance_cascades_renderer::get_probe_overlay_summary() const -> const Probe_overlay_summary&
{
    return m_overlay_summary;
}

void Radiance_cascades_renderer::render(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    if (!is_active() || (m_config.debug_draw_probes == Radiance_cascades_probe_overlay::none)) {
        return;
    }
    // CPU phase only (doc/editor/ddgi.md "Traps"): lines submitted in the
    // encoder phase miss the debug renderer's compute dispatch.
    if (context.encoder != nullptr) {
        return;
    }

    const int         cascade_index = std::clamp(m_config.debug_draw_cascade, 0, m_layout.cascade_count - 1);
    const Probe_grid& grid          = m_layout.cascades[static_cast<std::size_t>(cascade_index)].grid;
    erhe::renderer::Primitive_renderer line_renderer = context.get({erhe::graphics::Primitive_type::line, 2, true, true});

    // Volume box of the cascade's probes.
    const glm::vec3 volume_max = grid.origin + (grid.spacing * glm::vec3{grid.counts - glm::ivec3{1}});
    line_renderer.add_cube(glm::mat4{1.0f}, glm::vec4{0.3f, 0.6f, 1.0f, 1.0f}, grid.origin, volume_max);

    // The last retired copy is drawn only while it describes this grid; a
    // refit or another cascade shows unclassified positions until the next
    // copy retires.
    const bool has_copy =
        (m_overlay_summary.cascade == cascade_index) &&
        (m_overlay_grid == grid) &&
        (m_overlay_probes.size() == static_cast<std::size_t>(grid.get_probe_count()));
    const bool draw_irradiance =
        has_copy &&
        m_overlay_summary.irradiance &&
        (m_config.debug_draw_probes == Radiance_cascades_probe_overlay::state_and_irradiance);
    const float min_spacing      = std::min(grid.spacing.x, std::min(grid.spacing.y, grid.spacing.z));
    const float radius           = 0.06f * min_spacing;
    const float irradiance_scale = (m_overlay_irradiance_max > 0.0f) ? (1.0f / m_overlay_irradiance_max) : 0.0f;
    for (int z = 0; z < grid.counts.z; ++z) {
        for (int y = 0; y < grid.counts.y; ++y) {
            for (int x = 0; x < grid.counts.x; ++x) {
                const glm::vec3 position    = grid.origin + (grid.spacing * glm::vec3{x, y, z});
                const int       probe_index = x + (grid.counts.x * (y + (grid.counts.y * z)));
                const Rc_probe_overlay_state state = has_copy
                    ? m_overlay_probes[static_cast<std::size_t>(probe_index)].state
                    : Rc_probe_overlay_state::unclassified;
                const glm::vec4 color =
                    (state == Rc_probe_overlay_state::active) ? glm::vec4{0.2f, 1.0f, 0.4f, 1.0f} :
                    (state == Rc_probe_overlay_state::inside) ? glm::vec4{1.0f, 0.2f, 0.2f, 1.0f} :
                                                                glm::vec4{0.7f, 0.7f, 0.7f, 1.0f};
                line_renderer.add_sphere(
                    erhe::scene::Transform{},
                    color,
                    color,
                    2.0f,
                    1.0f,
                    position,
                    radius,
                    nullptr,
                    8
                );
                if (draw_irradiance) {
                    // A short thick line toward +Y in the probe's irradiance
                    // toward +Y, normalized by the brightest probe of the copy.
                    const glm::vec3 irradiance = m_overlay_probes[static_cast<std::size_t>(probe_index)].irradiance * irradiance_scale;
                    const glm::vec4 patch_color{glm::clamp(irradiance, glm::vec3{0.0f}, glm::vec3{1.0f}), 1.0f};
                    line_renderer.add_line(
                        patch_color, 8.0f, position + glm::vec3{0.0f, radius, 0.0f},
                        patch_color, 8.0f, position + glm::vec3{0.0f, 4.0f * radius, 0.0f}
                    );
                }
            }
        }
    }
}

} // namespace editor
