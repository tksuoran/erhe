#include "renderers/ddgi_renderer.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "config/generated/ddgi_config.hpp"
#include "config/generated/indirect_diffuse_bounces.hpp"
#include "content_library/content_library.hpp"
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
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/material_buffer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace editor {

namespace {

// Trace dispatch shape: one workgroup row per probe, this many rays per
// workgroup. The configured ray count is rounded up to it.
constexpr int c_trace_workgroup_size = 32;

// The octahedral tiles carry a 1-texel border on every side so bilinear
// interpolation inside a tile never samples a neighbouring probe.
constexpr int c_border_texels = 1;

// Raw binding points in the DDGI bind group layout. 0 and 1 are the shared
// material / light block binding points (erhe_scene_renderer); 2 and 3 are
// free (light_control / primitive slots of the raster layout, unused here).
constexpr unsigned int c_control_binding_point         = 2;
constexpr unsigned int c_instance_record_binding_point = 3;

// Probe trace: its own irradiance and distance atlases as combined image
// samplers (bounces multi, erhe_ray_hit.glsl ERHE_RT_INDIRECT_FIELD; the
// probe data is read through the trace's storage image). Vulkan offsets the
// samplers past the highest buffer binding (3), so user 4 / 5 land at 8 / 9,
// after the acceleration structure (4) and the storage images (5, 6).
constexpr unsigned int c_trace_irradiance_binding_point = 4;
constexpr unsigned int c_trace_distance_binding_point   = 5;

// Blend pass layout: the control UBO plus the two storage images. Raw
// bindings are not offset past the buffer bindings, and there are no
// samplers in the set, so 3 / 4 are free.
constexpr unsigned int c_blend_ray_data_binding_point = 3;
constexpr unsigned int c_blend_atlas_binding_point    = 4;

// One workgroup per probe; the workgroup strides over its tile's texels, so
// the group size is independent of the configurable octahedral resolution
// (which would otherwise force a shader recompile per setting change).
constexpr int c_blend_workgroup_size = 64;

// Relocation / classification: one thread per probe.
constexpr unsigned int c_relocate_ray_data_binding_point   = 3;
constexpr unsigned int c_relocate_probe_data_binding_point = 4;
constexpr int          c_relocate_workgroup_size           = 64;

// Irradiance query (ddgi_sample.comp): the light block at its shared binding
// point, the point input and result output storage buffers, and the three
// DDGI atlases as combined image samplers. Vulkan offsets the samplers past
// the highest buffer binding (3), so user 4 / 5 / 6 land at 8 / 9 / 10,
// clear of every buffer binding.
constexpr unsigned int c_query_input_binding_point      = 2;
constexpr unsigned int c_query_output_binding_point     = 3;
constexpr unsigned int c_query_irradiance_binding_point = 4;
constexpr unsigned int c_query_distance_binding_point   = 5;
constexpr unsigned int c_query_probe_data_binding_point = 6;
constexpr int          c_query_workgroup_size           = 64;
// Input record per point: position, normal, view direction (vec4 each).
constexpr std::size_t  c_query_vec4s_per_point          = 3;
// Query buffer capacities are rounded up to this (a multiple of every
// nonCoherentAtomSize the Vulkan spec allows).
constexpr int          c_query_buffer_alignment         = 256;

// Reference irradiance query (ddgi_reference.comp): the trace layout's
// material / light / control / instance-record / TLAS bindings (0 - 4, the
// control binding carrying the reference control block), then the point
// input and sum output storage buffers. One workgroup per point.
constexpr unsigned int c_reference_input_binding_point  = 5;
constexpr unsigned int c_reference_output_binding_point = 6;
constexpr int          c_reference_workgroup_size       = 64;
// Input record per point: position, normal (vec4 each). Output: two vec4s.
constexpr std::size_t  c_reference_vec4s_per_point      = 2;
// Rays that travel this far count as escaped (sky). Unbounded in effect;
// the probe trace's 4 x volume diagonal cannot be reached by content inside
// the volume either, so the two agree.
constexpr float        c_reference_t_max                = 1.0e30f;

constexpr erhe::dataformat::Format c_irradiance_format = erhe::dataformat::Format::format_16_vec4_float;
constexpr erhe::dataformat::Format c_distance_format   = erhe::dataformat::Format::format_16_vec2_float;
// Full float: one texel per probe is tiny, and it keeps the debug
// overlay readback a plain memcpy instead of a half-float decode.
constexpr erhe::dataformat::Format c_probe_data_format = erhe::dataformat::Format::format_32_vec4_float;
constexpr erhe::dataformat::Format c_ray_data_format   = erhe::dataformat::Format::format_16_vec4_float;

[[nodiscard]] auto round_up(const int value, const int multiple) -> int
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

auto c_str(const Ddgi_pass pass) -> const char*
{
    switch (pass) {
        case Ddgi_pass::trace:            return "trace";
        case Ddgi_pass::blend_irradiance: return "blend_irradiance";
        case Ddgi_pass::blend_distance:   return "blend_distance";
        case Ddgi_pass::relocate:         return "relocate";
        default:                          return "?";
    }
}

Ddgi_renderer::Ddgi_renderer(
    erhe::graphics::Device&                  graphics_device,
    erhe::graphics::Command_buffer&          init_command_buffer,
    App_context&                             context,
    App_message_bus&                         app_message_bus,
    erhe::scene_renderer::Program_interface& program_interface,
    erhe::scene_renderer::Mesh_memory&       mesh_memory,
    const Ddgi_config&                       config,
    const Producer_selection                 selection
)
    : m_graphics_device{graphics_device}
    , m_context        {context}
    , m_config         {config}
    , m_selection      {selection}
    , m_control_block{
        graphics_device,
        "ddgi",
        static_cast<int>(c_control_binding_point),
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , m_query_input_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "ddgi_query_input",
            .binding_point = static_cast<int>(c_query_input_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_query_output_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "ddgi_query_output",
            .binding_point = static_cast<int>(c_query_output_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    }
    , m_reference_control_block{
        graphics_device,
        "ddgi_reference",
        static_cast<int>(c_control_binding_point),
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , m_reference_input_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "ddgi_reference_input",
            .binding_point = static_cast<int>(c_reference_input_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_reference_output_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "ddgi_reference_output",
            .binding_point = static_cast<int>(c_reference_output_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    }
{
    using namespace erhe::graphics;

    // Ray query gates the whole feature: the probe update has no rasterized
    // fallback (doc/editor/ddgi.md - the probe-cubemap path is future work).
    if (!graphics_device.get_info().use_ray_query) {
        log_startup->info("Ddgi_renderer: ray query not available, DDGI disabled");
        return;
    }
    const bool use_position_fetch = graphics_device.get_info().use_ray_tracing_position_fetch;

    const std::filesystem::path editor_shaders = std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"};

    m_scene_tlas = std::make_unique<Scene_tlas>(
        graphics_device,
        mesh_memory,
        c_instance_record_binding_point,
        "Ddgi_renderer"
    );

    // Control block (std140). Shared by the trace pass and, from phase 4,
    // the blend passes.
    m_control_offsets.grid_origin     = m_control_block.add_vec4 ("grid_origin"    )->get_offset_in_parent();
    m_control_offsets.grid_spacing    = m_control_block.add_vec4 ("grid_spacing"   )->get_offset_in_parent();
    // xyz = probe counts, w = rays per probe
    m_control_offsets.grid_counts     = m_control_block.add_uvec4("grid_counts"    )->get_offset_in_parent();
    // x = first probe of this update, y = probes updated, z = irradiance
    // texels, w = distance texels
    m_control_offsets.dispatch        = m_control_block.add_uvec4("dispatch"       )->get_offset_in_parent();
    m_control_offsets.random_rotation = m_control_block.add_vec4 ("random_rotation")->get_offset_in_parent();
    // x = hysteresis, y = depth sharpness, z = max ray distance, w = intensity
    m_control_offsets.params          = m_control_block.add_vec4 ("params"         )->get_offset_in_parent();
    m_control_offsets.sky_radiance    = m_control_block.add_vec4 ("sky_radiance"   )->get_offset_in_parent();
    // x = relocation enabled, y = classification enabled
    m_control_offsets.flags           = m_control_block.add_uvec4("flags"          )->get_offset_in_parent();
    // x = tiles per atlas row (get_probe_field_tiles_per_row())
    m_control_offsets.atlas           = m_control_block.add_uvec4("atlas"          )->get_offset_in_parent();
    // Temporal history of the update (Temporal_history::get_shader_parameters(),
    // erhe_temporal_history.glsl): the blend's per-probe hysteresis.
    m_control_offsets.history         = m_control_block.add_uvec4("history"        )->get_offset_in_parent();

    // A committed change of the light transport resets the probes' temporal
    // history (doc/editor/ddgi.md "History reset"): geometry edits, removals
    // and Scene_lighting_changed_message (committed node transforms, content
    // or lights added or removed, light and material edits). Live transform
    // touches during a drag do not.
    m_mesh_geometry_changed_subscription = app_message_bus.mesh_geometry_changed.subscribe(
        [this](Mesh_geometry_changed_message&) { m_history.request_reset(); }
    );
    m_items_removed_subscription = app_message_bus.items_removed.subscribe(
        [this](Items_removed_message&) { m_history.request_reset(); }
    );
    m_scene_lighting_changed_subscription = app_message_bus.scene_lighting_changed.subscribe(
        [this](Scene_lighting_changed_message&) { m_history.request_reset(); }
    );

    // Stream-1 attribute offsets (in uints) for the shared hit path's manual
    // vertex fetch, derived from the Mesh_memory vertex format so they stay
    // in sync. Stream 1 is identical for the skinned and non-skinned formats.
    const erhe::dataformat::Vertex_format&   vertex_format = mesh_memory.vertex_format_not_skinned;
    const erhe::dataformat::Attribute_stream normal        = vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::normal,    erhe::dataformat::normal_attribute);
    const erhe::dataformat::Attribute_stream tangent       = vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::tangent,   0);
    const erhe::dataformat::Attribute_stream texcoord0     = vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::tex_coord, 0);
    const erhe::dataformat::Attribute_stream color0        = vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::color,     0);
    ERHE_VERIFY((normal   .attribute != nullptr) && (normal   .stream != nullptr));
    ERHE_VERIFY((tangent  .attribute != nullptr) && (tangent  .stream != nullptr));
    ERHE_VERIFY((texcoord0.attribute != nullptr) && (texcoord0.stream != nullptr));
    ERHE_VERIFY((color0   .attribute != nullptr) && (color0   .stream != nullptr));

    // Set 0: material / light / control / instance-record blocks at their
    // interface-declared binding points, then the acceleration structure and
    // the two storage images at the next raw binding points. The texture
    // heap (material textures) occupies set 1.
    auto to_binding_type = [](const Shader_resource& block) -> Binding_type {
        return (block.get_type() == Shader_resource::Type::shader_storage_block)
            ? Binding_type::storage_buffer
            : Binding_type::uniform_buffer;
    };
    m_tlas_binding_point       = c_instance_record_binding_point + 1;
    m_ray_data_binding_point   = c_instance_record_binding_point + 2;
    m_probe_data_binding_point = c_instance_record_binding_point + 3;
    m_trace_irradiance_binding_point = c_trace_irradiance_binding_point;
    m_trace_distance_binding_point   = c_trace_distance_binding_point;
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
                    .binding_point = m_ray_data_binding_point,
                    .type          = Binding_type::storage_image,
                    .name          = "i_ray_data",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba16f",
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = m_probe_data_binding_point,
                    .type          = Binding_type::storage_image,
                    .name          = "i_probe_data",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba32f",
                    .stage_flags   = Shader_stage_flags::compute
                },
                // The previous field (erhe_ray_hit.glsl ERHE_RT_INDIRECT_FIELD):
                // sampled at hits when bounces is multi; the light block
                // carries no field otherwise.
                {
                    .binding_point   = m_trace_irradiance_binding_point,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_ddgi_irradiance",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                {
                    .binding_point   = m_trace_distance_binding_point,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_ddgi_distance",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                }
            },
            .debug_label       = "DDGI trace",
            .uses_texture_heap = true
        }
    );

    // The shared hit path (erhe_ray_hit.glsl) needs these in every shader
    // that includes it: the probe trace and the reference irradiance query.
    const std::vector<std::pair<std::string, std::string>> ray_hit_defines{
        { "ERHE_TLAS_BINDING",            fmt::format("{}", m_tlas_binding_point) },
        { "ERHE_RT_NORMAL_OFFSET",        fmt::format("{}", normal   .attribute->offset / 4) },
        { "ERHE_RT_TANGENT_OFFSET",       fmt::format("{}", tangent  .attribute->offset / 4) },
        { "ERHE_RT_TEXCOORD0_OFFSET",     fmt::format("{}", texcoord0.attribute->offset / 4) },
        { "ERHE_RT_COLOR0_OFFSET",        fmt::format("{}", color0   .attribute->offset / 4) },
        { "ERHE_RT_HAS_POSITION_FETCH",   use_position_fetch ? "1" : "0" }
    };
    std::vector<Shader_stage_extension> ray_hit_extensions{
        { Shader_type::compute_shader, "GL_EXT_ray_query" },
        { Shader_type::compute_shader, "GL_EXT_buffer_reference" },
        { Shader_type::compute_shader, "GL_EXT_buffer_reference_uvec2" }
    };
    if (use_position_fetch) {
        ray_hit_extensions.push_back({ Shader_type::compute_shader, "GL_EXT_ray_tracing_position_fetch" });
    }

    m_trace_shader_stages = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name                = "ddgi_trace",
            .defines             = [&]() {
                std::vector<std::pair<std::string, std::string>> defines = ray_hit_defines;
                defines.emplace_back("ERHE_DDGI_TRACE_GROUP_SIZE", fmt::format("{}", c_trace_workgroup_size));
                // Bounces multi: hits sample the previous field; its probe
                // data is the trace's own storage image.
                defines.emplace_back("ERHE_RT_INDIRECT_FIELD",      "1");
                defines.emplace_back("ERHE_DDGI_PROBE_DATA_IMAGE",  "i_probe_data");
                return defines;
            }(),
            .extensions          = ray_hit_extensions,
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
            .shaders             = { { Shader_type::compute_shader, editor_shaders / "ddgi_trace.comp" } },
            .extra_include_paths = shader_paths(),
            .bind_group_layout   = m_trace_bind_group_layout.get()
        }
    );
    graphics_device.get_shader_monitor().add(*m_trace_shader_stages);

    m_trace_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "ddgi_trace",
            .shader_stages     = &m_trace_shader_stages->shader_stages,
            .bind_group_layout = m_trace_bind_group_layout.get()
        }
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
        "Ddgi_renderer::control",
        c_control_binding_point
    );

    create_blend_pass(graphics_device, m_blend_irradiance, false, "i_probe_atlas", "rgba16f", "DDGI blend irradiance");
    create_blend_pass(graphics_device, m_blend_distance,   true,  "i_probe_atlas", "rg16f",   "DDGI blend distance"  );

    m_relocate_bind_group_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                {
                    .binding_point = c_control_binding_point,
                    .type          = Binding_type::uniform_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = c_relocate_ray_data_binding_point,
                    .type          = Binding_type::storage_image,
                    .name          = "i_ray_data",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba16f",
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = c_relocate_probe_data_binding_point,
                    .type          = Binding_type::storage_image,
                    .name          = "i_probe_data",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba32f",
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label = "DDGI relocate"
        }
    );
    m_relocate_shader_stages = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name                = "ddgi_relocate",
            .defines             = { { "ERHE_DDGI_RELOCATE_GROUP_SIZE", fmt::format("{}", c_relocate_workgroup_size) } },
            .interface_blocks    = { &m_control_block },
            .shaders             = { { Shader_type::compute_shader, editor_shaders / "ddgi_relocate.comp" } },
            .extra_include_paths = shader_paths(),
            .bind_group_layout   = m_relocate_bind_group_layout.get()
        }
    );
    graphics_device.get_shader_monitor().add(*m_relocate_shader_stages);
    m_relocate_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "ddgi_relocate",
            .shader_stages     = &m_relocate_shader_stages->shader_stages,
            .bind_group_layout = m_relocate_bind_group_layout.get()
        }
    );

    create_query_pass(graphics_device, program_interface);
    create_reference_pass(graphics_device, program_interface, ray_hit_defines, ray_hit_extensions);

    // Labels double as the Performance window plot names.
    m_pass_timings[static_cast<std::size_t>(Ddgi_pass::trace           )].timer = std::make_unique<Gpu_timer>(graphics_device, "DDGI trace");
    m_pass_timings[static_cast<std::size_t>(Ddgi_pass::blend_irradiance)].timer = std::make_unique<Gpu_timer>(graphics_device, "DDGI blend irradiance");
    m_pass_timings[static_cast<std::size_t>(Ddgi_pass::blend_distance  )].timer = std::make_unique<Gpu_timer>(graphics_device, "DDGI blend distance");
    m_pass_timings[static_cast<std::size_t>(Ddgi_pass::relocate        )].timer = std::make_unique<Gpu_timer>(graphics_device, "DDGI relocate");

    m_supported = true;
    log_startup->info("Ddgi_renderer: DDGI available");
}

void Ddgi_renderer::create_blend_pass(
    erhe::graphics::Device& graphics_device,
    Blend_pass&             pass,
    const bool              distance,
    const char*             image_name,
    const char*             image_format,
    const char*             debug_label
)
{
    using namespace erhe::graphics;

    const std::filesystem::path editor_shaders = std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"};

    pass.bind_group_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                {
                    .binding_point = c_control_binding_point,
                    .type          = Binding_type::uniform_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = c_blend_ray_data_binding_point,
                    .type          = Binding_type::storage_image,
                    .name          = "i_ray_data",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba16f",
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = c_blend_atlas_binding_point,
                    .type          = Binding_type::storage_image,
                    .name          = image_name,
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = image_format,
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label = debug_label
        }
    );

    pass.shader_stages = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name                = distance ? "ddgi_blend_distance" : "ddgi_blend_irradiance",
            .defines             = {
                { "ERHE_DDGI_BLEND_GROUP_SIZE", fmt::format("{}", c_blend_workgroup_size) },
                { "ERHE_DDGI_BLEND_DISTANCE",   distance ? "1" : "0" }
            },
            .interface_blocks    = { &m_control_block },
            .shaders             = { { Shader_type::compute_shader, editor_shaders / "ddgi_blend.comp" } },
            .extra_include_paths = shader_paths(),
            .bind_group_layout   = pass.bind_group_layout.get()
        }
    );
    graphics_device.get_shader_monitor().add(*pass.shader_stages);

    pass.pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = distance ? "ddgi_blend_distance" : "ddgi_blend_irradiance",
            .shader_stages     = &pass.shader_stages->shader_stages,
            .bind_group_layout = pass.bind_group_layout.get()
        }
    );
}

void Ddgi_renderer::create_query_pass(
    erhe::graphics::Device&                  graphics_device,
    erhe::scene_renderer::Program_interface& program_interface
)
{
    using namespace erhe::graphics;

    const std::filesystem::path editor_shaders = std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"};

    // header.x = point count
    m_query_header_offset = m_query_input_block .add_uvec4("header"                                    )->get_offset_in_parent();
    m_query_points_offset = m_query_input_block .add_vec4 ("points",     Shader_resource::unsized_array)->get_offset_in_parent();
    m_query_output_offset = m_query_output_block.add_vec4 ("irradiance", Shader_resource::unsized_array)->get_offset_in_parent();

    const Shader_resource& light_block = program_interface.light_interface.light_block;
    const Binding_type light_binding_type = (light_block.get_type() == Shader_resource::Type::shader_storage_block)
        ? Binding_type::storage_buffer
        : Binding_type::uniform_buffer;

    // The same sampler the forward pass samples the atlases with.
    m_ddgi_sampler = &program_interface.light_interface.ddgi_sampler;

    m_query_bind_group_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                {
                    .binding_point = light_buffer_binding_point,
                    .type          = light_binding_type,
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = c_query_input_binding_point,
                    .type          = Binding_type::storage_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = c_query_output_binding_point,
                    .type          = Binding_type::storage_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point   = c_query_irradiance_binding_point,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_ddgi_irradiance",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                {
                    .binding_point   = c_query_distance_binding_point,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_ddgi_distance",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                {
                    .binding_point   = c_query_probe_data_binding_point,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_ddgi_probe_data",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                }
            },
            .debug_label = "DDGI irradiance query"
        }
    );

    m_query_shader_stages = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name                = "ddgi_sample",
            .defines             = { { "ERHE_DDGI_SAMPLE_GROUP_SIZE", fmt::format("{}", c_query_workgroup_size) } },
            .struct_types        = { &program_interface.light_interface.light_struct },
            .interface_blocks    = {
                &program_interface.light_interface.light_block,
                &m_query_input_block,
                &m_query_output_block
            },
            .shaders             = { { Shader_type::compute_shader, editor_shaders / "ddgi_sample.comp" } },
            .extra_include_paths = shader_paths(),
            .bind_group_layout   = m_query_bind_group_layout.get()
        }
    );
    graphics_device.get_shader_monitor().add(*m_query_shader_stages);

    m_query_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "ddgi_sample",
            .shader_stages     = &m_query_shader_stages->shader_stages,
            .bind_group_layout = m_query_bind_group_layout.get()
        }
    );

    // Host-visible both ways: the CPU writes the points before the frame is
    // submitted, and reads the results after the frame retired. Flushes and
    // invalidations cover the whole buffer, so the capacity is rounded up to
    // a multiple any nonCoherentAtomSize divides.
    const auto make_query_buffer = [&](const std::size_t byte_count, const char* debug_label) -> std::unique_ptr<Buffer> {
        return std::make_unique<Buffer>(
            graphics_device,
            Buffer_create_info{
                .capacity_byte_count                    = static_cast<std::size_t>(round_up(static_cast<int>(byte_count), c_query_buffer_alignment)),
                .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
                .usage                                  = Buffer_usage::storage,
                .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read | Memory_property_flag_bit_mask::host_write,
                .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_coherent | Memory_property_flag_bit_mask::host_persistent,
                .debug_label                            = erhe::utility::Debug_label{debug_label}
            }
        );
    };
    m_query_input_buffer = make_query_buffer(
        m_query_points_offset + (c_max_irradiance_query_points * c_query_vec4s_per_point * sizeof(glm::vec4)),
        "DDGI irradiance query input"
    );
    m_query_output_buffer = make_query_buffer(
        m_query_output_offset + (c_max_irradiance_query_points * sizeof(glm::vec4)),
        "DDGI irradiance query output"
    );
}

void Ddgi_renderer::create_reference_pass(
    erhe::graphics::Device&                                     graphics_device,
    erhe::scene_renderer::Program_interface&                    program_interface,
    const std::vector<std::pair<std::string, std::string>>&     ray_hit_defines,
    const std::vector<erhe::graphics::Shader_stage_extension>& ray_hit_extensions
)
{
    using namespace erhe::graphics;

    const std::filesystem::path editor_shaders = std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"};

    // x = first point of this chunk, y = end point of this chunk, z = rays
    // per point, w = seed
    m_reference_dispatch_offset = m_reference_control_block.add_uvec4("dispatch"    )->get_offset_in_parent();
    // x = normal bias, y = t_max
    m_reference_params_offset   = m_reference_control_block.add_vec4 ("params"      )->get_offset_in_parent();
    m_reference_sky_offset      = m_reference_control_block.add_vec4 ("sky_radiance")->get_offset_in_parent();
    m_reference_points_offset   = m_reference_input_block  .add_vec4 ("points", Shader_resource::unsized_array)->get_offset_in_parent();
    m_reference_output_offset   = m_reference_output_block .add_vec4 ("sums",   Shader_resource::unsized_array)->get_offset_in_parent();

    auto to_binding_type = [](const Shader_resource& block) -> Binding_type {
        return (block.get_type() == Shader_resource::Type::shader_storage_block)
            ? Binding_type::storage_buffer
            : Binding_type::uniform_buffer;
    };
    m_reference_bind_group_layout = std::make_unique<Bind_group_layout>(
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
                    .binding_point = c_reference_input_binding_point,
                    .type          = Binding_type::storage_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = c_reference_output_binding_point,
                    .type          = Binding_type::storage_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label       = "DDGI reference irradiance",
            .uses_texture_heap = true
        }
    );

    m_reference_shader_stages = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name                = "ddgi_reference",
            .defines             = [&]() {
                std::vector<std::pair<std::string, std::string>> defines = ray_hit_defines;
                defines.emplace_back("ERHE_DDGI_REFERENCE_GROUP_SIZE", fmt::format("{}", c_reference_workgroup_size));
                return defines;
            }(),
            .extensions          = ray_hit_extensions,
            .struct_types        = {
                &program_interface.material_interface.material_struct,
                &program_interface.light_interface.light_struct,
                &m_scene_tlas->get_instance_struct()
            },
            .interface_blocks    = {
                &program_interface.material_interface.material_block,
                &program_interface.light_interface.light_block,
                &m_reference_control_block,
                &m_scene_tlas->get_instance_block(),
                &m_reference_input_block,
                &m_reference_output_block
            },
            .shaders             = { { Shader_type::compute_shader, editor_shaders / "ddgi_reference.comp" } },
            .extra_include_paths = shader_paths(),
            .bind_group_layout   = m_reference_bind_group_layout.get()
        }
    );
    graphics_device.get_shader_monitor().add(*m_reference_shader_stages);

    m_reference_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "ddgi_reference",
            .shader_stages     = &m_reference_shader_stages->shader_stages,
            .bind_group_layout = m_reference_bind_group_layout.get()
        }
    );

    // Same host-visible recipe as the irradiance query buffers.
    const auto make_reference_buffer = [&](const std::size_t byte_count, const char* debug_label) -> std::unique_ptr<Buffer> {
        return std::make_unique<Buffer>(
            graphics_device,
            Buffer_create_info{
                .capacity_byte_count                    = static_cast<std::size_t>(round_up(static_cast<int>(byte_count), c_query_buffer_alignment)),
                .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
                .usage                                  = Buffer_usage::storage,
                .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read | Memory_property_flag_bit_mask::host_write,
                .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_coherent | Memory_property_flag_bit_mask::host_persistent,
                .debug_label                            = erhe::utility::Debug_label{debug_label}
            }
        );
    };
    m_reference_input_buffer = make_reference_buffer(
        m_reference_points_offset + (c_max_irradiance_query_points * c_reference_vec4s_per_point * sizeof(glm::vec4)),
        "DDGI reference irradiance input"
    );
    m_reference_output_buffer = make_reference_buffer(
        m_reference_output_offset + (c_max_irradiance_query_points * c_reference_vec4s_per_point * sizeof(glm::vec4)),
        "DDGI reference irradiance output"
    );
}

auto Ddgi_renderer::begin_reference_query(
    const std::span<const Irradiance_query_point> points,
    const Reference_query_settings&               settings
) -> bool
{
    if ((m_reference_state == Irradiance_query_state::queued) || (m_reference_state == Irradiance_query_state::in_flight)) {
        return false;
    }
    if (points.empty() || (points.size() > c_max_irradiance_query_points) || !m_reference_pipeline) {
        return false;
    }
    if ((settings.rays_per_point < 1) || (settings.rays_per_point > c_max_reference_rays_per_point)) {
        return false;
    }
    if ((static_cast<int64_t>(points.size()) * static_cast<int64_t>(settings.rays_per_point)) > c_max_reference_rays_per_query) {
        return false;
    }
    m_reference_points.assign(points.begin(), points.end());
    m_reference_results.clear();
    m_reference_settings   = settings;
    m_reference_error.clear();
    m_reference_next_point = 0;
    m_reference_state      = Irradiance_query_state::queued;
    return true;
}

void Ddgi_renderer::cancel_reference_query()
{
    // Chunks already recorded still write the output buffer; stop recording
    // more, and let the next begin_reference_query() wait until the last
    // recorded frame retired (poll_reference_query() reports in_flight until
    // then, and complete - with partial results nobody reads - after).
    if (m_reference_state == Irradiance_query_state::queued) {
        m_reference_state = Irradiance_query_state::idle;
    } else if (m_reference_state == Irradiance_query_state::in_flight) {
        m_reference_next_point = m_reference_points.size();
    }
}

auto Ddgi_renderer::reference_needs_dispatch() const -> bool
{
    return
        ((m_reference_state == Irradiance_query_state::queued) || (m_reference_state == Irradiance_query_state::in_flight)) &&
        (m_reference_next_point < m_reference_points.size());
}

void Ddgi_renderer::fail_reference_query(const char* reason)
{
    if (m_reference_state == Irradiance_query_state::queued) {
        m_reference_error = reason;
        m_reference_state = Irradiance_query_state::failed;
    } else {
        // Some chunks were recorded already: finish none of the rest, let
        // them retire, and report the failure afterwards.
        m_reference_error      = reason;
        m_reference_next_point = m_reference_points.size();
    }
}

auto Ddgi_renderer::poll_reference_query() -> Irradiance_query_state
{
    if (
        (m_reference_state == Irradiance_query_state::in_flight) &&
        (m_reference_next_point >= m_reference_points.size()) &&
        m_graphics_device.is_frame_completed(m_reference_frame)
    ) {
        if (!m_reference_error.empty()) {
            m_reference_state = Irradiance_query_state::failed;
            return m_reference_state;
        }
        const std::size_t          count    = m_reference_points.size();
        const std::size_t          capacity = m_reference_output_buffer->get_capacity_byte_count();
        const std::span<std::byte> mapped   = m_reference_output_buffer->map_bytes(0, capacity);
        m_reference_output_buffer->invalidate(0, capacity);
        m_reference_results.resize(count);
        const float intensity = m_reference_intensity;
        for (std::size_t i = 0; i < count; ++i) {
            std::array<glm::vec4, c_reference_vec4s_per_point> record{};
            std::memcpy(
                record.data(),
                mapped.data() + m_reference_output_offset + (i * sizeof(record)),
                sizeof(record)
            );
            // Ray sums -> estimate. Luminance is linear, so the mean ray
            // luminance is the luminance of the mean radiance; the squared
            // luminance sum gives the sample variance.
            const double    ray_count     = std::max(1.0, static_cast<double>(record[1].z));
            const glm::dvec3 sum_rgb      = glm::dvec3{record[0]};
            const double    sum_y         = (0.2126 * sum_rgb.r) + (0.7152 * sum_rgb.g) + (0.0722 * sum_rgb.b);
            const double    mean_y        = sum_y / ray_count;
            const double    mean_y2       = static_cast<double>(record[0].a) / ray_count;
            const double    variance      = (ray_count > 1.0)
                ? (std::max(0.0, mean_y2 - (mean_y * mean_y)) * (ray_count / (ray_count - 1.0)))
                : 0.0;
            Reference_irradiance_sample& sample = m_reference_results[i];
            sample.irradiance        = glm::vec3{sum_rgb / ray_count} * intensity;
            sample.standard_error    = static_cast<float>(std::sqrt(variance / ray_count)) * intensity;
            sample.backface_fraction = static_cast<float>(static_cast<double>(record[1].x) / ray_count);
            sample.sky_fraction      = static_cast<float>(static_cast<double>(record[1].y) / ray_count);
        }
        m_reference_output_buffer->unmap();
        m_reference_state = Irradiance_query_state::complete;
    }
    return m_reference_state;
}

auto Ddgi_renderer::get_reference_query_results() const -> std::span<const Reference_irradiance_sample>
{
    return std::span<const Reference_irradiance_sample>{m_reference_results};
}

auto Ddgi_renderer::get_reference_query_error() const -> const std::string&
{
    return m_reference_error;
}

auto Ddgi_renderer::get_reference_query_intensity() const -> float
{
    return m_reference_intensity;
}

void Ddgi_renderer::record_reference_chunk(
    erhe::graphics::Command_buffer&          command_buffer,
    const Scene_tlas::Frame&                 tlas_frame,
    const erhe::graphics::Ring_buffer_range& light_range,
    erhe::scene_renderer::Material_set&      material_set
)
{
    using namespace erhe::graphics;

    const std::size_t count = m_reference_points.size();
    if (m_reference_state == Irradiance_query_state::queued) {
        // First chunk: upload every point once. The buffer is not touched
        // again until the query retired.
        const std::size_t          capacity = m_reference_input_buffer->get_capacity_byte_count();
        const std::span<std::byte> mapped   = m_reference_input_buffer->map_bytes(0, capacity);
        for (std::size_t i = 0; i < count; ++i) {
            const Irradiance_query_point& point = m_reference_points[i];
            const std::array<glm::vec4, c_reference_vec4s_per_point> record{
                glm::vec4{point.position, 1.0f},
                glm::vec4{point.normal,   0.0f}
            };
            std::memcpy(mapped.data() + m_reference_points_offset + (i * sizeof(record)), record.data(), sizeof(record));
        }
        m_reference_input_buffer->flush_bytes(0, capacity);
        m_reference_input_buffer->unmap();
        // The intensity the forward pass multiplies the field with; the
        // results include it the same way.
        m_reference_intensity = std::max(0.0f, m_config.intensity);
        m_reference_state     = Irradiance_query_state::in_flight;
    }

    // This frame's chunk: whole points, about c_reference_rays_per_frame rays.
    const int64_t     rays_per_point   = static_cast<int64_t>(m_reference_settings.rays_per_point);
    const std::size_t points_per_chunk = static_cast<std::size_t>(std::max<int64_t>(1, c_reference_rays_per_frame / rays_per_point));
    const std::size_t first_point      = m_reference_next_point;
    const std::size_t end_point        = std::min(count, first_point + points_per_chunk);

    const std::size_t byte_count = m_reference_control_block.get_size_bytes();
    Ring_buffer_range control_range = m_control_buffer->acquire(Ring_buffer_usage::CPU_write, byte_count);
    {
        std::span<std::byte> gpu_data = control_range.get_span();
        std::memset(gpu_data.data(), 0, byte_count);
        const glm::uvec4 dispatch{
            static_cast<uint32_t>(first_point),
            static_cast<uint32_t>(end_point),
            static_cast<uint32_t>(m_reference_settings.rays_per_point),
            m_reference_settings.seed
        };
        const glm::vec4 params{m_reference_settings.normal_bias, c_reference_t_max, 0.0f, 0.0f};
        const glm::vec4 sky_radiance{m_sky_radiance, 0.0f};
        write(gpu_data, m_reference_dispatch_offset, as_span(dispatch    ));
        write(gpu_data, m_reference_params_offset,   as_span(params      ));
        write(gpu_data, m_reference_sky_offset,      as_span(sky_radiance));
        control_range.bytes_written(byte_count);
        control_range.close();
    }

    const std::size_t input_byte_count  = m_reference_points_offset + (count * c_reference_vec4s_per_point * sizeof(glm::vec4));
    const std::size_t output_byte_count = m_reference_output_offset + (count * c_reference_vec4s_per_point * sizeof(glm::vec4));
    {
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_reference_bind_group_layout.get());
        encoder.set_compute_pipeline(*m_reference_pipeline);
        m_light_buffer->bind_light_buffer(encoder, light_range);
        m_control_buffer->bind(encoder, control_range);
        m_scene_tlas->bind_instance_records(encoder, tlas_frame);
        encoder.set_acceleration_structure(m_tlas_binding_point, *tlas_frame.acceleration_structure);
        encoder.set_buffer(Buffer_target::storage, m_reference_input_buffer.get(),  0, input_byte_count,  c_reference_input_binding_point);
        encoder.set_buffer(Buffer_target::storage, m_reference_output_buffer.get(), 0, output_byte_count, c_reference_output_binding_point);
        material_set.bind(encoder);
        encoder.dispatch_compute(static_cast<std::uintptr_t>(end_point - first_point), 1, 1);
    }
    control_range.release();

    m_reference_next_point = end_point;
    if (m_reference_next_point >= count) {
        // Shader writes -> host reads once the frame's fence has signalled.
        command_buffer.memory_barrier(Memory_barrier_mask::client_mapped_buffer_barrier_bit);
    }
    m_reference_frame = m_graphics_device.get_frame_index();
}

Ddgi_renderer::~Ddgi_renderer() noexcept = default;

auto Ddgi_renderer::get_forward_parameters() const -> erhe::scene_renderer::Ddgi_parameters
{
    erhe::scene_renderer::Ddgi_parameters parameters{};
    if (!is_active()) {
        return parameters;
    }
    parameters.grid_origin       = m_grid.origin;
    parameters.grid_spacing      = m_grid.spacing;
    parameters.grid_counts       = m_grid.counts;
    parameters.irradiance_texels = m_irradiance_texels;
    parameters.distance_texels   = m_distance_texels;
    parameters.tiles_per_row     = m_tiles_per_row;
    parameters.normal_bias       = m_config.normal_bias;
    parameters.view_bias         = m_config.view_bias;
    parameters.depth_sharpness   = m_config.depth_sharpness;
    parameters.intensity         = m_config.intensity;
    return parameters;
}

auto Ddgi_renderer::get_field() const -> Probe_field
{
    if (!is_active()) {
        return Probe_field{};
    }
    return Probe_field{
        .parameters   = get_forward_parameters(),
        .irradiance   = m_irradiance_texture,
        .distance     = m_distance_texture,
        .probe_data   = m_probe_data_texture,
        .update_count = m_update_count
    };
}

auto Ddgi_renderer::begin_irradiance_query(const std::span<const Irradiance_query_point> points) -> bool
{
    if ((m_query_state == Irradiance_query_state::queued) || (m_query_state == Irradiance_query_state::in_flight)) {
        return false;
    }
    if (points.empty() || (points.size() > c_max_irradiance_query_points) || !m_query_pipeline) {
        return false;
    }
    m_query_points.assign(points.begin(), points.end());
    m_query_results.clear();
    m_query_state = Irradiance_query_state::queued;
    return true;
}

void Ddgi_renderer::cancel_irradiance_query()
{
    // Only a query that has not been recorded can be dropped; a recorded one
    // still writes the output buffer, so it runs to completion and the next
    // begin_irradiance_query() waits for it.
    if (m_query_state == Irradiance_query_state::queued) {
        m_query_state = Irradiance_query_state::idle;
    }
}

auto Ddgi_renderer::poll_irradiance_query() -> Irradiance_query_state
{
    if ((m_query_state == Irradiance_query_state::in_flight) && m_graphics_device.is_frame_completed(m_query_frame)) {
        const std::size_t count      = m_query_points.size();
        const std::size_t capacity   = m_query_output_buffer->get_capacity_byte_count();
        const std::span<std::byte> mapped = m_query_output_buffer->map_bytes(0, capacity);
        m_query_output_buffer->invalidate(0, capacity);
        m_query_results.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            glm::vec4 value{0.0f};
            std::memcpy(&value, mapped.data() + m_query_output_offset + (i * sizeof(glm::vec4)), sizeof(glm::vec4));
            m_query_results[i] = glm::vec3{value};
        }
        m_query_output_buffer->unmap();
        m_query_state = Irradiance_query_state::complete;
    }
    return m_query_state;
}

auto Ddgi_renderer::get_irradiance_query_results() const -> std::span<const glm::vec3>
{
    return std::span<const glm::vec3>{m_query_results};
}

auto Ddgi_renderer::get_irradiance_query_update_count() const -> uint64_t
{
    return m_query_update_count;
}

void Ddgi_renderer::record_irradiance_query(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root, const Probe_field& field)
{
    using namespace erhe::graphics;

    if ((m_query_state != Irradiance_query_state::queued) || !m_query_pipeline || !field.is_valid()) {
        return;
    }

    // Points: header (count) + position / normal / view direction per point.
    const std::size_t count             = m_query_points.size();
    const std::size_t input_byte_count  = m_query_points_offset + (count * c_query_vec4s_per_point * sizeof(glm::vec4));
    const std::size_t output_byte_count = m_query_output_offset + (count * sizeof(glm::vec4));
    {
        const std::size_t          capacity = m_query_input_buffer->get_capacity_byte_count();
        const std::span<std::byte> mapped   = m_query_input_buffer->map_bytes(0, capacity);
        const glm::uvec4 header{static_cast<uint32_t>(count), 0u, 0u, 0u};
        std::memcpy(mapped.data() + m_query_header_offset, &header, sizeof(header));
        for (std::size_t i = 0; i < count; ++i) {
            const Irradiance_query_point& point = m_query_points[i];
            const std::array<glm::vec4, c_query_vec4s_per_point> record{
                glm::vec4{point.position,       1.0f},
                glm::vec4{point.normal,         0.0f},
                glm::vec4{point.view_direction, 0.0f}
            };
            std::memcpy(mapped.data() + m_query_points_offset + (i * sizeof(record)), record.data(), sizeof(record));
        }
        m_query_input_buffer->flush_bytes(0, capacity);
        m_query_input_buffer->unmap();
    }

    // The light block contents the forward pass sees for the field: the
    // scene ambient (the no-volume / zero-weight fallback) and the volume
    // parameters. No lights: ddgi_sample_irradiance() does not read them.
    const glm::vec3   ambient     = scene_root.get_scene().get_ambient_light();
    Ring_buffer_range light_range = m_light_buffer->update(nullptr, ambient, 0u, &field.parameters);

    {
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_query_bind_group_layout.get());
        encoder.set_compute_pipeline(*m_query_pipeline);
        m_light_buffer->bind_light_buffer(encoder, light_range);
        encoder.set_buffer(Buffer_target::storage, m_query_input_buffer.get(),  0, input_byte_count,  c_query_input_binding_point);
        encoder.set_buffer(Buffer_target::storage, m_query_output_buffer.get(), 0, output_byte_count, c_query_output_binding_point);
        encoder.set_sampled_image(c_query_irradiance_binding_point, *field.irradiance, *m_ddgi_sampler);
        encoder.set_sampled_image(c_query_distance_binding_point,   *field.distance,   *m_ddgi_sampler);
        encoder.set_sampled_image(c_query_probe_data_binding_point, *field.probe_data, *m_ddgi_sampler);
        const std::size_t group_size = static_cast<std::size_t>(c_query_workgroup_size);
        encoder.dispatch_compute(static_cast<std::uintptr_t>((count + group_size - 1) / group_size), 1, 1);
    }
    light_range.release();

    // Shader writes -> host reads once the frame's fence has signalled.
    command_buffer.memory_barrier(Memory_barrier_mask::client_mapped_buffer_barrier_bit);

    m_query_frame        = m_graphics_device.get_frame_index();
    m_query_update_count = field.update_count;
    m_query_state        = Irradiance_query_state::in_flight;
}

auto Ddgi_renderer::is_supported() const -> bool
{
    return m_supported && (m_trace_pipeline != nullptr);
}

auto Ddgi_renderer::is_selected() const -> bool
{
    return m_selection == Producer_selection::selected;
}

void Ddgi_renderer::set_selection(const Producer_selection selection)
{
    if (selection == m_selection) {
        return;
    }
    m_selection = selection;
    if (selection == Producer_selection::deselected) {
        // Release the probe memory while another source is selected; the
        // textures are cheap to recreate and the grid is refitted anyway.
        // A frame still in flight keeps its textures alive through the
        // shared pointers the forward pass resources hold.
        m_irradiance_texture.reset();
        m_distance_texture  .reset();
        m_probe_data_texture.reset();
        m_ray_data_texture  .reset();
        m_texture_byte_count = 0;
        m_grid = Grid{};
        m_volume_bounds.reset();
        m_probe_readback_valid  = false;
        m_probe_state_requested = false;
        m_probe_state_in_flight = false;
        m_probe_state_valid     = false;
        clear_pass_timings();
    }
}

auto Ddgi_renderer::is_active() const -> bool
{
    return is_supported() && is_selected() && m_grid.is_valid() && m_irradiance_texture;
}

auto Ddgi_renderer::get_grid() const -> const Grid&
{
    return m_grid;
}

auto Ddgi_renderer::get_irradiance_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&
{
    return m_irradiance_texture;
}

auto Ddgi_renderer::get_distance_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&
{
    return m_distance_texture;
}

auto Ddgi_renderer::get_probe_data_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&
{
    return m_probe_data_texture;
}

auto Ddgi_renderer::get_ray_data_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&
{
    return m_ray_data_texture;
}

auto Ddgi_renderer::get_texture_byte_count() const -> std::size_t
{
    return m_texture_byte_count;
}

auto Ddgi_renderer::get_rays_per_probe() const -> int
{
    return m_rays_per_probe;
}

auto Ddgi_renderer::get_irradiance_texels() const -> int
{
    return m_irradiance_texels;
}

auto Ddgi_renderer::get_distance_texels() const -> int
{
    return m_distance_texels;
}

auto Ddgi_renderer::get_probes_per_update() const -> int
{
    return m_probes_per_update;
}

auto Ddgi_renderer::get_instance_count() const -> std::size_t
{
    return m_scene_tlas ? m_scene_tlas->get_instance_count() : 0;
}

void Ddgi_renderer::sample_pass_timings()
{
    // A timer's result is the latest completed measurement of an earlier
    // update; all of them read 0 until the first one has completed. A single
    // pass may legitimately measure 0: the timestamps are taken after all
    // earlier work completes, so a pass that overlaps its predecessor and
    // finishes first is charged nothing (doc/editor/ddgi.md "Performance").
    std::array<uint64_t, c_ddgi_pass_count> results_ns{};
    uint64_t                                sum_ns = 0;
    for (std::size_t i = 0; i < c_ddgi_pass_count; ++i) {
        results_ns[i] = m_pass_timings[i].timer->last_result();
        sum_ns += results_ns[i];
    }
    if (sum_ns == 0) {
        return;
    }
    for (std::size_t i = 0; i < c_ddgi_pass_count; ++i) {
        Pass_timing&   pass_timing = m_pass_timings[i];
        const uint64_t ns          = results_ns[i];
        pass_timing.last_ns = ns;
        pass_timing.history_ns[pass_timing.history_next] = ns;
        pass_timing.history_next  = (pass_timing.history_next + 1) % c_timing_history_size;
        pass_timing.history_count = std::min(pass_timing.history_count + 1, c_timing_history_size);
    }
    ++m_timing_sample_count;
}

void Ddgi_renderer::clear_pass_timings()
{
    for (Pass_timing& pass_timing : m_pass_timings) {
        pass_timing.history_count = 0;
        pass_timing.history_next  = 0;
        pass_timing.last_ns       = 0;
    }
}

auto Ddgi_renderer::get_stats() const -> Stats
{
    Stats stats{};
    for (std::size_t i = 0; i < c_ddgi_pass_count; ++i) {
        const Pass_timing& pass_timing = m_pass_timings[i];
        uint64_t sum_ns = 0;
        for (std::size_t j = 0; j < pass_timing.history_count; ++j) {
            sum_ns += pass_timing.history_ns[j];
        }
        Pass_time& pass_time = stats.passes[i];
        pass_time.last_ms    = static_cast<double>(pass_timing.last_ns) * 1.0e-6;
        pass_time.average_ms = (pass_timing.history_count > 0)
            ? (static_cast<double>(sum_ns) * 1.0e-6) / static_cast<double>(pass_timing.history_count)
            : 0.0;
        stats.total.last_ms    += pass_time.last_ms;
        stats.total.average_ms += pass_time.average_ms;
    }
    stats.update_count        = m_update_count;
    stats.timing_sample_count = m_timing_sample_count;
    stats.history_reset_count = m_history.get_reset_count();
    stats.rays_per_update     = static_cast<int64_t>(m_probes_per_update) * static_cast<int64_t>(m_rays_per_probe);
    const int probe_count = m_grid.get_probe_count();
    stats.updates_per_full_refresh = (m_probes_per_update > 0)
        ? ((probe_count + m_probes_per_update - 1) / m_probes_per_update)
        : 0;
    stats.ms_per_million_rays = (stats.rays_per_update > 0)
        ? (stats.total.average_ms * 1.0e6) / static_cast<double>(stats.rays_per_update)
        : 0.0;
    stats.full_refresh_ms = static_cast<double>(stats.updates_per_full_refresh) * stats.total.average_ms;
    return stats;
}

void Ddgi_renderer::allocate_textures(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    const int irradiance_tile = m_irradiance_texels + (2 * c_border_texels);
    const int distance_tile   = m_distance_texels   + (2 * c_border_texels);

    // Probe tiles wrap into rows of m_tiles_per_row (get_probe_field_tile(),
    // shared with the forward pass and the radiance cascades reduce), sized
    // so the larger tile keeps both atlas sides within the texture limit.
    m_tiles_per_row = get_probe_field_tiles_per_row(m_grid.counts, std::max(irradiance_tile, distance_tile), m_graphics_device.get_info().max_texture_size);
    const int tiles_x = m_tiles_per_row;
    const int tiles_y = get_probe_field_tile_rows(m_grid.counts, m_tiles_per_row);

    m_texture_byte_count = 0;
    const auto make_texture = [&](
        const char*                     debug_label,
        const erhe::dataformat::Format  format,
        const int                       width,
        const int                       height
    ) -> std::shared_ptr<Texture> {
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
        m_texture_byte_count +=
            static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height) *
            erhe::dataformat::get_format_size_bytes(format);
        // Probes start black with zero distance and zero relocation offset;
        // the update passes overwrite them progressively, and until then
        // every reader sees defined (dark) data instead of undefined memory.
        command_buffer.clear_texture(*texture, {0.0, 0.0, 0.0, 0.0});
        command_buffer.transition_texture_layout(*texture, Image_layout::shader_read_only_optimal);
        return texture;
    };

    m_irradiance_texture = make_texture("DDGI irradiance", c_irradiance_format, tiles_x * irradiance_tile, tiles_y * irradiance_tile);
    m_distance_texture   = make_texture("DDGI distance",   c_distance_format,   tiles_x * distance_tile,   tiles_y * distance_tile  );
    m_probe_data_texture = make_texture("DDGI probe data", c_probe_data_format, tiles_x,                   tiles_y                  );
    // One row per probe SLOT of an update, not per probe: only the budgeted
    // probes are traced each tick, and the blend passes read the same rows.
    m_ray_data_texture   = make_texture("DDGI ray data",   c_ray_data_format,   m_rays_per_probe,          m_probes_per_update      );

    // Host-visible mirror for the probe overlay. One rgba32f texel per
    // probe, so even a large grid is a few hundred kilobytes.
    const std::size_t probe_data_byte_count =
        static_cast<std::size_t>(tiles_x) *
        static_cast<std::size_t>(tiles_y) *
        erhe::dataformat::get_format_size_bytes(c_probe_data_format);
    m_probe_readback_buffer = std::make_unique<Buffer>(
        m_graphics_device,
        Buffer_create_info{
            .capacity_byte_count                    = probe_data_byte_count,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::transfer_dst | Buffer_usage::storage,
            .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read | Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_coherent | Memory_property_flag_bit_mask::host_persistent,
            .debug_label                            = erhe::utility::Debug_label{"DDGI probe readback"}
        }
    );
    m_probe_readback_valid = false;
    m_probe_state_readback_buffer = std::make_unique<Buffer>(
        m_graphics_device,
        Buffer_create_info{
            .capacity_byte_count                    = probe_data_byte_count,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::transfer_dst | Buffer_usage::storage,
            .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read | Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_coherent | Memory_property_flag_bit_mask::host_persistent,
            .debug_label                            = erhe::utility::Debug_label{"DDGI probe state readback"}
        }
    );
    m_probe_state_in_flight = false;
    m_probe_state_valid     = false;

    // Refits happen at runtime (content moved, settings changed), so this is
    // a render-log event, not a startup one.
    log_render->info(
        "Ddgi_renderer: grid {}x{}x{} = {} probes, spacing {:.2f} {:.2f} {:.2f} m, {} rays/probe, {} probes/update, {:.1f} MB",
        m_grid.counts.x, m_grid.counts.y, m_grid.counts.z, m_grid.get_probe_count(),
        m_grid.spacing.x, m_grid.spacing.y, m_grid.spacing.z,
        m_rays_per_probe, m_probes_per_update,
        static_cast<double>(m_texture_byte_count) / (1024.0 * 1024.0)
    );
}

auto Ddgi_renderer::update_volume(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root) -> bool
{
    const int   rays_per_probe    = round_up(std::max(8, m_config.rays_per_probe), c_trace_workgroup_size);
    const int   irradiance_texels = std::clamp(m_config.irradiance_texels, 2, 32);
    const int   distance_texels   = std::clamp(m_config.distance_texels,   2, 64);
    const float fit_spacing_m     = std::max(0.01f, m_config.probe_spacing_m);
    const float fit_padding_m     = std::max(0.0f,  m_config.volume_padding_m);
    // The probe budget is also bounded by the atlas: every probe tile must
    // fit within the texture size limit.
    const int   fit_max_probes    = std::min(
        std::max(8, m_config.max_probes),
        get_probe_field_max_probes(std::max(irradiance_texels, distance_texels) + (2 * c_border_texels), m_graphics_device.get_info().max_texture_size)
    );

    const erhe::math::Aabb bounds = compute_padded_content_bounds(scene_root, fit_padding_m);
    if (!bounds.is_valid()) {
        return false;
    }

    // Refit only when the content left the current volume, or shrank so far
    // inside it that the probe density is being wasted. Everything else -
    // meshes moving within the volume - keeps the existing grid and its
    // converged probe contents.
    const int  probes_per_update = std::max(1, m_config.probes_per_frame);
    const bool settings_changed =
        (rays_per_probe    != m_rays_per_probe   ) ||
        (irradiance_texels != m_irradiance_texels) ||
        (distance_texels   != m_distance_texels  ) ||
        (fit_spacing_m     != m_fit_spacing_m    ) ||
        (fit_padding_m     != m_fit_padding_m    ) ||
        (fit_max_probes    != m_fit_max_probes   ) ||
        !m_irradiance_texture ||
        !m_grid.is_valid();
    const bool bounds_changed = m_volume_bounds.content_changed(bounds);
    // The budget only sizes the ray data texture, so it can change without
    // a refit - but it does need the textures reallocated.
    const bool budget_changed = m_grid.is_valid() && (std::min(probes_per_update, m_grid.get_probe_count()) != m_probes_per_update);

    if (!settings_changed && !bounds_changed && !budget_changed) {
        return true;
    }

    // A settings change refits to the current content exactly - the fit
    // parameters (spacing, padding, budget) are what changed, so the old
    // volume carries no information worth keeping. A content change grows
    // or shrinks the volume (Probe_volume_bounds::get_fit_bounds()); a
    // budget-only change keeps it.
    const Volume_refit_cause cause =
        settings_changed ? Volume_refit_cause::settings :
        bounds_changed   ? Volume_refit_cause::content  :
                           Volume_refit_cause::budget;
    const erhe::math::Aabb fit_bounds = m_volume_bounds.get_fit_bounds(bounds, cause);
    const Grid grid = fit_probe_grid(fit_bounds, fit_spacing_m, fit_max_probes);
    if (!grid.is_valid()) {
        return false;
    }

    m_volume_bounds.set(fit_bounds);
    m_fit_spacing_m     = fit_spacing_m;
    m_fit_padding_m     = fit_padding_m;
    m_fit_max_probes    = fit_max_probes;
    m_grid              = grid;
    m_rays_per_probe    = rays_per_probe;
    m_irradiance_texels = irradiance_texels;
    m_distance_texels   = distance_texels;
    m_probes_per_update = std::min(probes_per_update, grid.get_probe_count());
    m_probe_cursor      = 0;
    allocate_textures(command_buffer);
    // Every probe is new: its first trace replaces the allocation clear.
    m_history.reset(grid.get_probe_count(), m_probe_cursor);
    return true;
}

void Ddgi_renderer::copy_probe_data(erhe::graphics::Command_buffer& command_buffer, erhe::graphics::Buffer& destination)
{
    using namespace erhe::graphics;

    if (!m_probe_data_texture) {
        return;
    }
    const int         width         = m_probe_data_texture->get_width();
    const int         height        = m_probe_data_texture->get_height();
    const std::size_t bytes_per_row = static_cast<std::size_t>(width) * erhe::dataformat::get_format_size_bytes(c_probe_data_format);
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(height);

    // copy_from_texture() moves the image from its tracked layout
    // (shader_read_only_optimal after the update) to transfer_src and back.
    {
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(
            m_probe_data_texture.get(),
            0,                             // source_slice
            0,                             // source_level
            glm::ivec3{0, 0, 0},           // source_origin
            glm::ivec3{width, height, 1},  // source_size
            &destination,                  // destination_buffer
            0,                             // destination_offset
            static_cast<std::uintptr_t>(bytes_per_row),
            static_cast<std::uintptr_t>(byte_count)
        );
    }
}

void Ddgi_renderer::request_probe_states()
{
    m_probe_state_requested = true;
}

auto Ddgi_renderer::poll_probe_states() -> bool
{
    if (m_probe_state_in_flight && m_probe_state_readback_buffer && m_graphics_device.is_frame_completed(m_probe_state_frame)) {
        m_probe_state_in_flight = false;
        const int         probe_count = m_grid.get_probe_count();
        const float       min_spacing = std::min(m_grid.spacing.x, std::min(m_grid.spacing.y, m_grid.spacing.z));
        const std::size_t capacity    = m_probe_state_readback_buffer->get_capacity_byte_count();
        const std::span<std::byte> mapped = m_probe_state_readback_buffer->map_bytes(0, capacity);
        m_probe_state_readback_buffer->invalidate(0, capacity);
        m_probe_states.resize(static_cast<std::size_t>(probe_count));
        Probe_state_summary summary{};
        summary.update_count = m_probe_state_copy_update_count;
        float max_offset = 0.0f;
        for (int z = 0; z < m_grid.counts.z; ++z) {
            for (int y = 0; y < m_grid.counts.y; ++y) {
                for (int x = 0; x < m_grid.counts.x; ++x) {
                    // Probe data texel: the probe's atlas tile.
                    const glm::ivec2  tile  = get_probe_field_tile(glm::ivec3{x, y, z}, m_grid.counts, m_tiles_per_row);
                    const std::size_t texel =
                        (static_cast<std::size_t>(tile.y) * static_cast<std::size_t>(m_tiles_per_row)) +
                        static_cast<std::size_t>(tile.x);
                    glm::vec4 value{0.0f};
                    std::memcpy(&value, mapped.data() + (texel * sizeof(glm::vec4)), sizeof(glm::vec4));
                    const std::size_t probe_index = static_cast<std::size_t>(x + (m_grid.counts.x * (y + (m_grid.counts.y * z))));
                    m_probe_states[probe_index] = value;
                    const float offset = glm::length(glm::vec3{value});
                    max_offset = std::max(max_offset, offset);
                    if (value.w < 0.5f) {
                        ++summary.inactive;
                    } else {
                        ++summary.active;
                    }
                    if (offset > c_relocated_offset_m) {
                        ++summary.relocated;
                    }
                }
            }
        }
        m_probe_state_readback_buffer->unmap();
        summary.max_offset_over_spacing = (min_spacing > 0.0f) ? (max_offset / min_spacing) : 0.0f;
        m_probe_state_summary = summary;
        m_probe_state_valid   = true;
    }
    return m_probe_state_valid;
}

auto Ddgi_renderer::get_probe_state_summary() const -> const Probe_state_summary&
{
    return m_probe_state_summary;
}

auto Ddgi_renderer::get_probe_states() const -> std::span<const glm::vec4>
{
    return std::span<const glm::vec4>{m_probe_states};
}

void Ddgi_renderer::render(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    if (!is_active() || !m_config.debug_draw_probes) {
        return;
    }
    // render_viewport_renderables() runs twice per viewport: first the CPU
    // phase (no encoder) where debug lines are generated, then the encoder
    // phase for renderables that issue draw calls. Lines submitted in the
    // second phase would miss the debug renderer's compute dispatch, whose
    // buffer bookkeeping then trips on the unconsumed range.
    if (context.encoder != nullptr) {
        return;
    }

    erhe::renderer::Primitive_renderer line_renderer = context.get({erhe::graphics::Primitive_type::line, 2, true, true});

    // Volume box: what the grid was fitted to.
    const glm::vec3 volume_max = m_grid.origin + m_grid.spacing * glm::vec3{m_grid.counts - glm::ivec3{1}};
    line_renderer.add_cube(glm::mat4{1.0f}, glm::vec4{0.3f, 0.6f, 1.0f, 1.0f}, m_grid.origin, volume_max);

    // Probe spheres. The relocation offset and the classification state come
    // from the host-visible mirror the tick copies; without a copy yet, the
    // probes draw at their nominal grid positions in the "active" colour.
    const float* probe_data = nullptr;
    std::span<std::byte> mapped{};
    if (m_probe_readback_valid && m_probe_readback_buffer) {
        mapped = m_probe_readback_buffer->map_bytes(0, m_probe_readback_buffer->get_capacity_byte_count());
        if (!mapped.empty()) {
            probe_data = reinterpret_cast<const float*>(mapped.data());
        }
    }

    const float radius = 0.06f * std::min(m_grid.spacing.x, std::min(m_grid.spacing.y, m_grid.spacing.z));
    for (int z = 0; z < m_grid.counts.z; ++z) {
        for (int y = 0; y < m_grid.counts.y; ++y) {
            for (int x = 0; x < m_grid.counts.x; ++x) {
                glm::vec3 position = m_grid.origin + glm::vec3{x, y, z} * m_grid.spacing;
                glm::vec4 color    = glm::vec4{0.2f, 1.0f, 0.4f, 1.0f}; // active
                if (probe_data != nullptr) {
                    const glm::ivec2  tile  = get_probe_field_tile(glm::ivec3{x, y, z}, m_grid.counts, m_tiles_per_row);
                    const std::size_t texel = (static_cast<std::size_t>(tile.y) * static_cast<std::size_t>(m_tiles_per_row)) +
                                              static_cast<std::size_t>(tile.x);
                    const float* rgba = probe_data + (texel * 4);
                    position += glm::vec3{rgba[0], rgba[1], rgba[2]};
                    if (rgba[3] < 0.5f) {
                        color = glm::vec4{1.0f, 0.2f, 0.2f, 1.0f}; // classified inactive
                    }
                }
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
            }
        }
    }
    if (!mapped.empty()) {
        m_probe_readback_buffer->unmap();
    }
}

auto Ddgi_renderer::next_random_rotation() -> glm::vec4
{
    // Shoemake's uniform random rotation quaternion.
    std::uniform_real_distribution<float> distribution{0.0f, 1.0f};
    const float u0 = distribution(m_random_engine);
    const float u1 = distribution(m_random_engine);
    const float u2 = distribution(m_random_engine);
    const float r0 = std::sqrt(1.0f - u0);
    const float r1 = std::sqrt(u0);
    const float t1 = glm::two_pi<float>() * u1;
    const float t2 = glm::two_pi<float>() * u2;
    return glm::vec4{r0 * std::sin(t1), r0 * std::cos(t1), r1 * std::sin(t2), r1 * std::cos(t2)};
}

auto Ddgi_renderer::update_control_buffer() -> erhe::graphics::Ring_buffer_range
{
    using namespace erhe::graphics;

    const std::size_t byte_count = m_control_block.get_size_bytes();
    Ring_buffer_range range = m_control_buffer->acquire(Ring_buffer_usage::CPU_write, byte_count);
    std::span<std::byte> gpu_data = range.get_span();
    std::memset(gpu_data.data(), 0, byte_count);

    const glm::vec4 grid_origin {m_grid.origin,  0.0f};
    const glm::vec4 grid_spacing{m_grid.spacing, 0.0f};
    const glm::uvec4 grid_counts{
        static_cast<uint32_t>(m_grid.counts.x),
        static_cast<uint32_t>(m_grid.counts.y),
        static_cast<uint32_t>(m_grid.counts.z),
        static_cast<uint32_t>(m_rays_per_probe)
    };
    const glm::uvec4 dispatch{
        m_probe_cursor,
        static_cast<uint32_t>(m_probes_per_update),
        static_cast<uint32_t>(m_irradiance_texels),
        static_cast<uint32_t>(m_distance_texels)
    };
    const glm::vec4 random_rotation = next_random_rotation();
    // Rays that reach this far are treated as escaped; the diagonal of a
    // grid cell scaled up covers the volume with room to spare, and it
    // keeps distant geometry outside the volume from dominating the probe's
    // visibility statistics.
    const float max_ray_distance = 4.0f * glm::length(m_grid.spacing * glm::vec3{m_grid.counts - glm::ivec3{1}});
    const glm::uvec4 flags{
        m_config.relocation_enabled     ? 1u : 0u,
        m_config.classification_enabled ? 1u : 0u,
        0u,
        0u
    };
    const glm::vec4 params{
        std::clamp(m_config.hysteresis, 0.0f, 0.999f),
        std::max(1.0f, m_config.depth_sharpness),
        max_ray_distance,
        std::max(0.0f, m_config.intensity)
    };

    write(gpu_data, m_control_offsets.grid_origin,     as_span(grid_origin    ));
    write(gpu_data, m_control_offsets.grid_spacing,    as_span(grid_spacing   ));
    write(gpu_data, m_control_offsets.grid_counts,     as_span(grid_counts    ));
    write(gpu_data, m_control_offsets.dispatch,        as_span(dispatch       ));
    write(gpu_data, m_control_offsets.random_rotation, as_span(random_rotation));
    write(gpu_data, m_control_offsets.params,          as_span(params         ));
    write(gpu_data, m_control_offsets.sky_radiance,    as_span(m_sky_radiance ));
    write(gpu_data, m_control_offsets.flags,           as_span(flags          ));
    const glm::uvec4 atlas{static_cast<uint32_t>(m_tiles_per_row), 0u, 0u, 0u};
    write(gpu_data, m_control_offsets.atlas,           as_span(atlas          ));
    const glm::uvec4 history = m_history.get_shader_parameters(0);
    write(gpu_data, m_control_offsets.history,         as_span(history        ));
    range.bytes_written(byte_count);
    range.close();
    return range;
}

void Ddgi_renderer::tick(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root)
{
    using namespace erhe::graphics;

    if (!is_supported()) {
        return;
    }
    // Two consumers of this frame's trace inputs (TLAS, lights, materials):
    // the probe update, and a pending reference irradiance query - which
    // runs whatever the source. Building the inputs once serves both, and
    // costs nothing while neither needs them.
    const bool update_field    = is_selected() && update_volume(command_buffer, scene_root);
    const bool trace_reference = reference_needs_dispatch();
    if (!update_field && !trace_reference) {
        return;
    }

    // Materials: the scene root's FORWARD set, already updated for this frame
    // by App_scenes::update_material_sets() (doc/erhe/draw_list_material_set.md
    // D5, D6). This dispatch binds it and the TLAS instance records name slots
    // in it, so the two agree by construction rather than because the buffer
    // happens to have been rewritten from the same list a moment ago.
    erhe::scene_renderer::Material_set& material_set = scene_root.get_material_set();
    if (material_set.get_live_count() == 0) {
        if (trace_reference) {
            fail_reference_query("the scene has no materials (nothing to trace against)");
        }
        return;
    }

    // The lights need UBO slots and projection transforms even though DDGI
    // never samples a shadow map (it traces shadow rays).
    if (!fit_trace_light_projections(m_context, m_graphics_device, scene_root, *m_light_projections)) {
        if (trace_reference) {
            fail_reference_query("the scene has no camera (the light block fit needs one)");
        }
        return;
    }

    m_sky_radiance = scene_root.get_scene().get_ambient_light();

    Scene_tlas::Frame tlas_frame     = m_scene_tlas->update(command_buffer, *scene_root.layers().content(), &material_set);
    ERHE_VERIFY(tlas_frame.is_valid());
    // Bounces multi: the light block carries this producer's field (the
    // previous update's atlases), and shade_surface() takes a probe ray
    // hit's ambient term from it (erhe_ray_hit.glsl ERHE_RT_INDIRECT_FIELD).
    // The reference query shares the light block and ignores the field (it
    // is compiled without the define: single bounce, the ground truth).
    // The feedback is the physical irradiance: intensity is a display
    // multiplier the forward pass applies once, and folded into every
    // bounce it would scale the gain per bounce (intensity x albedo > 1
    // diverges).
    erhe::scene_renderer::Ddgi_parameters field_parameters = get_forward_parameters();
    field_parameters.intensity = 1.0f;
    const bool multi_bounce = update_field && (m_config.bounces == Indirect_diffuse_bounces::multi);
    Ring_buffer_range light_range    = m_light_buffer->update(
        m_light_projections.get(),
        m_sky_radiance,
        0u,
        multi_bounce ? &field_parameters : nullptr
    );

    if (trace_reference) {
        // Outside the probe update's timers, so the pass timings stay the
        // probe update's own.
        record_reference_chunk(command_buffer, tlas_frame, light_range, material_set);
    }
    if (!update_field) {
        light_range.release();
        tlas_frame.instance_records.release();
        material_set.unbind(command_buffer);
        return;
    }

    // A change message since the last update starts the probes' temporal
    // history over at the cursor.
    m_history.begin_update(m_grid.get_probe_count(), static_cast<int64_t>(m_probe_cursor));
    Ring_buffer_range control_range  = update_control_buffer();

    sample_pass_timings();
    ++m_update_count;

    const auto pass_timer = [this](const Ddgi_pass pass) -> Gpu_timer& {
        return *m_pass_timings[static_cast<std::size_t>(pass)].timer;
    };

    command_buffer.transition_texture_layout(*m_ray_data_texture,   Image_layout::general);
    command_buffer.transition_texture_layout(*m_probe_data_texture, Image_layout::general);
    {
        const Scoped_gpu_timer trace_timer{pass_timer(Ddgi_pass::trace), command_buffer};
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_trace_bind_group_layout.get());
        encoder.set_compute_pipeline(*m_trace_pipeline);
        m_light_buffer->bind_light_buffer(encoder, light_range);
        m_control_buffer->bind(encoder, control_range);
        m_scene_tlas->bind_instance_records(encoder, tlas_frame);
        encoder.set_acceleration_structure(m_tlas_binding_point, *tlas_frame.acceleration_structure);
        encoder.set_storage_image(m_ray_data_binding_point,   *m_ray_data_texture);
        encoder.set_storage_image(m_probe_data_binding_point, *m_probe_data_texture);
        encoder.set_sampled_image(m_trace_irradiance_binding_point, *m_irradiance_texture, *m_ddgi_sampler);
        encoder.set_sampled_image(m_trace_distance_binding_point,   *m_distance_texture,   *m_ddgi_sampler);
        material_set.bind(encoder);
        encoder.dispatch_compute(
            static_cast<std::uintptr_t>((m_rays_per_probe + c_trace_workgroup_size - 1) / c_trace_workgroup_size),
            static_cast<std::uintptr_t>(m_probes_per_update),
            1
        );
    }
    light_range.release();
    tlas_frame.instance_records.release();
    material_set.unbind(command_buffer);

    // The blend passes read this tick's ray data.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    command_buffer.transition_texture_layout(*m_irradiance_texture, Image_layout::general);
    command_buffer.transition_texture_layout(*m_distance_texture,   Image_layout::general);
    {
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        const auto blend = [&](const Blend_pass& pass, const std::shared_ptr<Texture>& atlas, const Ddgi_pass timed_pass) {
            const Scoped_gpu_timer blend_timer{pass_timer(timed_pass), command_buffer};
            encoder.set_bind_group_layout(pass.bind_group_layout.get());
            encoder.set_compute_pipeline(*pass.pipeline);
            m_control_buffer->bind(encoder, control_range);
            encoder.set_storage_image(c_blend_ray_data_binding_point, *m_ray_data_texture);
            encoder.set_storage_image(c_blend_atlas_binding_point,    *atlas);
            encoder.dispatch_compute(static_cast<std::uintptr_t>(m_probes_per_update), 1, 1);
        };
        blend(m_blend_irradiance, m_irradiance_texture, Ddgi_pass::blend_irradiance);
        blend(m_blend_distance,   m_distance_texture,   Ddgi_pass::blend_distance  );

        // Relocation / classification reads the same ray data, and writes
        // only the probe data texture the blend passes never touch, so it
        // needs no barrier against them.
        const Scoped_gpu_timer relocate_timer{pass_timer(Ddgi_pass::relocate), command_buffer};
        encoder.set_bind_group_layout(m_relocate_bind_group_layout.get());
        encoder.set_compute_pipeline(*m_relocate_pipeline);
        m_control_buffer->bind(encoder, control_range);
        encoder.set_storage_image(c_relocate_ray_data_binding_point,   *m_ray_data_texture);
        encoder.set_storage_image(c_relocate_probe_data_binding_point, *m_probe_data_texture);
        encoder.dispatch_compute(
            static_cast<std::uintptr_t>((m_probes_per_update + c_relocate_workgroup_size - 1) / c_relocate_workgroup_size),
            1,
            1
        );
    }
    control_range.release();

    // Both atlases become sampled textures for the DDGI window preview and,
    // from phase 6, the forward shading path.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    command_buffer.transition_texture_layout(*m_ray_data_texture,   Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_probe_data_texture, Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_irradiance_texture, Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_distance_texture,   Image_layout::shader_read_only_optimal);

    if (m_config.debug_draw_probes && m_probe_readback_buffer) {
        copy_probe_data(command_buffer, *m_probe_readback_buffer);
        // The overlay reads whatever the previous frame left in the mirror;
        // the copy recorded here lands before the next one runs.
        m_probe_readback_valid = true;
    }
    if (m_probe_state_requested && !m_probe_state_in_flight && m_probe_state_readback_buffer) {
        copy_probe_data(command_buffer, *m_probe_state_readback_buffer);
        m_probe_state_requested         = false;
        m_probe_state_in_flight         = true;
        m_probe_state_frame             = m_graphics_device.get_frame_index();
        m_probe_state_copy_update_count = m_update_count;
    }

    m_history.end_update(m_probes_per_update, m_config.hysteresis);

    // Advance the round-robin cursor for the next tick.
    const uint32_t probe_count = static_cast<uint32_t>(m_grid.get_probe_count());
    m_probe_cursor = (m_probe_cursor + static_cast<uint32_t>(m_probes_per_update)) % probe_count;
}

} // namespace editor
