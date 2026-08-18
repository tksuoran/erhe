#include "renderers/ddgi_renderer.hpp"

#include "app_context.hpp"
#include "app_settings.hpp"
#include "config/generated/ddgi_config.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/camera.hpp"
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

// Blend pass layout: the control UBO plus the two storage images. Raw
// bindings are not offset past the buffer bindings, and there are no
// samplers in the set, so 3 / 4 are free.
constexpr unsigned int c_blend_ray_data_binding_point = 3;
constexpr unsigned int c_blend_atlas_binding_point    = 4;

// One workgroup per probe; the workgroup strides over its tile's texels, so
// the group size is independent of the configurable octahedral resolution
// (which would otherwise force a shader recompile per setting change).
constexpr int c_blend_workgroup_size = 64;

constexpr erhe::dataformat::Format c_irradiance_format = erhe::dataformat::Format::format_16_vec4_float;
constexpr erhe::dataformat::Format c_distance_format   = erhe::dataformat::Format::format_16_vec2_float;
constexpr erhe::dataformat::Format c_probe_data_format = erhe::dataformat::Format::format_16_vec4_float;
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

auto Ddgi_renderer::Grid::operator==(const Grid& other) const -> bool
{
    return (counts == other.counts) && (origin == other.origin) && (spacing == other.spacing);
}

Ddgi_renderer::Ddgi_renderer(
    erhe::graphics::Device&                  graphics_device,
    erhe::graphics::Command_buffer&          init_command_buffer,
    App_context&                             context,
    erhe::scene_renderer::Program_interface& program_interface,
    erhe::scene_renderer::Mesh_memory&       mesh_memory,
    const Ddgi_config&                       config
)
    : m_graphics_device{graphics_device}
    , m_context        {context}
    , m_config         {config}
    , m_control_block{
        graphics_device,
        "ddgi",
        static_cast<int>(c_control_binding_point),
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , m_fallback_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter        = erhe::graphics::Filter::nearest,
            .mag_filter        = erhe::graphics::Filter::nearest,
            .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode      = { erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge },
            .compare_enable    = false,
            .compare_operation = erhe::graphics::Compare_operation::always,
            .debug_label       = "Ddgi_renderer::m_fallback_sampler"
        }
    }
{
    using namespace erhe::graphics;

    // Ray query gates the whole feature: the probe update has no rasterized
    // fallback (doc/ddgi-plan.md - the probe-cubemap path is future work).
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
                    .image_format  = "rgba16f",
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label       = "DDGI trace",
            .uses_texture_heap = true
        }
    );

    m_trace_shader_stages = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name                = "ddgi_trace",
            .defines             = {
                { "ERHE_TLAS_BINDING",            fmt::format("{}", m_tlas_binding_point) },
                { "ERHE_DDGI_TRACE_GROUP_SIZE",   fmt::format("{}", c_trace_workgroup_size) },
                { "ERHE_RT_NORMAL_OFFSET",        fmt::format("{}", normal   .attribute->offset / 4) },
                { "ERHE_RT_TANGENT_OFFSET",       fmt::format("{}", tangent  .attribute->offset / 4) },
                { "ERHE_RT_TEXCOORD0_OFFSET",     fmt::format("{}", texcoord0.attribute->offset / 4) },
                { "ERHE_RT_COLOR0_OFFSET",        fmt::format("{}", color0   .attribute->offset / 4) },
                { "ERHE_RT_HAS_POSITION_FETCH",   use_position_fetch ? "1" : "0" }
            },
            .extensions          = [&]() {
                std::vector<Shader_stage_extension> extensions{
                    { Shader_type::compute_shader, "GL_EXT_ray_query" },
                    { Shader_type::compute_shader, "GL_EXT_buffer_reference" },
                    { Shader_type::compute_shader, "GL_EXT_buffer_reference_uvec2" }
                };
                if (use_position_fetch) {
                    extensions.push_back({ Shader_type::compute_shader, "GL_EXT_ray_tracing_position_fetch" });
                }
                return extensions;
            }(),
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

    m_material_buffer = std::make_unique<erhe::scene_renderer::Material_buffer>(
        graphics_device,
        program_interface.material_interface
    );
    m_light_buffer = std::make_unique<erhe::scene_renderer::Light_buffer>(
        graphics_device,
        init_command_buffer,
        program_interface.light_interface
    );
    m_light_projections = std::make_unique<erhe::scene_renderer::Light_projections>();
    m_dummy_texture = graphics_device.create_dummy_texture(init_command_buffer, erhe::dataformat::Format::format_8_vec4_srgb);
    m_texture_heap = std::make_unique<Texture_heap>(
        graphics_device,
        *m_dummy_texture.get(),
        m_fallback_sampler,
        m_trace_bind_group_layout.get()
    );
    m_control_buffer = std::make_unique<Ring_buffer_client>(
        graphics_device,
        Buffer_target::uniform,
        "Ddgi_renderer::control",
        c_control_binding_point
    );

    create_blend_pass(graphics_device, m_blend_irradiance, false, "i_probe_atlas", "rgba16f", "DDGI blend irradiance");
    create_blend_pass(graphics_device, m_blend_distance,   true,  "i_probe_atlas", "rg16f",   "DDGI blend distance"  );

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

Ddgi_renderer::~Ddgi_renderer() noexcept = default;

auto Ddgi_renderer::is_supported() const -> bool
{
    return m_supported && (m_trace_pipeline != nullptr);
}

auto Ddgi_renderer::is_active() const -> bool
{
    return is_supported() && m_config.enabled && m_grid.is_valid() && m_irradiance_texture;
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

auto Ddgi_renderer::compute_volume_bounds(Scene_root& scene_root) const -> erhe::math::Aabb
{
    erhe::math::Aabb bounds{};

    const erhe::scene::Mesh_layer* content_layer = scene_root.layers().content();
    if (content_layer == nullptr) {
        return bounds;
    }

    // Union of the visible content meshes' world bounds. Skinned meshes are
    // included: they do not go into the acceleration structure, but they are
    // lit by the volume, so the volume has to cover them.
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : content_layer->meshes) {
        if (!mesh || !mesh->is_visible()) {
            continue;
        }
        const erhe::math::Aabb mesh_bounds = mesh->get_aabb_world();
        if (!mesh_bounds.is_valid()) {
            continue;
        }
        bounds.include(mesh_bounds);
    }
    if (!bounds.is_valid()) {
        return bounds;
    }

    const float padding = std::max(0.0f, m_config.volume_padding_m);
    bounds.min -= glm::vec3{padding};
    bounds.max += glm::vec3{padding};
    return bounds;
}

auto Ddgi_renderer::fit_grid(const erhe::math::Aabb& bounds) const -> Grid
{
    Grid grid{};
    if (!bounds.is_valid()) {
        return grid;
    }
    const glm::vec3 min    = bounds.min;
    const glm::vec3 extent = glm::max(bounds.max - bounds.min, glm::vec3{1.0e-3f});

    // Probe counts from the target spacing; at least 2 per axis so the
    // trilinear interpolation always has a cell to interpolate inside. If
    // the result exceeds the probe budget, grow the spacing and retry - the
    // budget is a hard memory bound, the spacing is a target.
    const int max_probes = std::max(8, m_config.max_probes);
    float     spacing    = std::max(0.01f, m_config.probe_spacing_m);
    glm::ivec3 counts{0};
    for (;;) {
        counts = glm::ivec3{
            std::max(2, static_cast<int>(std::ceil(extent.x / spacing)) + 1),
            std::max(2, static_cast<int>(std::ceil(extent.y / spacing)) + 1),
            std::max(2, static_cast<int>(std::ceil(extent.z / spacing)) + 1)
        };
        const int64_t probe_count =
            static_cast<int64_t>(counts.x) *
            static_cast<int64_t>(counts.y) *
            static_cast<int64_t>(counts.z);
        if (probe_count <= static_cast<int64_t>(max_probes)) {
            break;
        }
        // Cube root of the overshoot is the spacing factor that would land
        // exactly on the budget; the 1.05 keeps the loop from stalling on
        // rounding. Both counts are >= 2, so this terminates.
        const double overshoot = static_cast<double>(probe_count) / static_cast<double>(max_probes);
        spacing *= static_cast<float>(std::cbrt(overshoot)) * 1.05f;
        if ((counts.x == 2) && (counts.y == 2) && (counts.z == 2)) {
            break; // Cannot get any coarser.
        }
    }

    grid.counts  = counts;
    grid.origin  = min;
    grid.spacing = extent / glm::vec3{counts - glm::ivec3{1}};
    return grid;
}

void Ddgi_renderer::allocate_textures(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    // Probe (x,y,z) -> tile (x + counts.x * z, y): a row of tiles per y
    // slice, so the atlas stays close to square for typical grids.
    const int tiles_x = m_grid.counts.x * m_grid.counts.z;
    const int tiles_y = m_grid.counts.y;

    const int irradiance_tile = m_irradiance_texels + (2 * c_border_texels);
    const int distance_tile   = m_distance_texels   + (2 * c_border_texels);

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
    const int   fit_max_probes    = std::max(8,     m_config.max_probes);

    const erhe::math::Aabb bounds = compute_volume_bounds(scene_root);
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
    const bool have_volume = m_volume_bounds.is_valid();
    const bool outside     = have_volume && (
        glm::any(glm::lessThan   (bounds.min, m_volume_bounds.min)) ||
        glm::any(glm::greaterThan(bounds.max, m_volume_bounds.max))
    );
    const bool much_smaller   = have_volume && (bounds.volume() < (0.5f * m_volume_bounds.volume()));
    const bool bounds_changed = !have_volume || outside || much_smaller;
    // The budget only sizes the ray data texture, so it can change without
    // a refit - but it does need the textures reallocated.
    const bool budget_changed = m_grid.is_valid() && (std::min(probes_per_update, m_grid.get_probe_count()) != m_probes_per_update);

    if (!settings_changed && !bounds_changed && !budget_changed) {
        return true;
    }

    // A settings change refits to the current content exactly - the fit
    // parameters (spacing, padding, budget) are what changed, so the old
    // volume carries no information worth keeping. A pure bounds change
    // grows the box instead: a mesh that just left it takes it along, and
    // keeping the old extent stops a mesh oscillating across the boundary
    // from retriggering a refit every other frame. A large shrink refits to
    // the content, so the volume can also get smaller again.
    erhe::math::Aabb fit_bounds = bounds;
    if (!settings_changed && have_volume && outside && !much_smaller) {
        fit_bounds.include(m_volume_bounds);
    } else if (!settings_changed && !bounds_changed && have_volume) {
        fit_bounds = m_volume_bounds; // budget-only change: keep the volume
    }
    const Grid grid = fit_grid(fit_bounds);
    if (!grid.is_valid()) {
        return false;
    }

    m_volume_bounds     = fit_bounds;
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
    return true;
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
    if (!m_config.enabled) {
        // Release the probe memory while the feature is off; the textures
        // are cheap to recreate and the grid is refitted anyway.
        if (m_irradiance_texture) {
            m_irradiance_texture.reset();
            m_distance_texture  .reset();
            m_probe_data_texture.reset();
            m_ray_data_texture  .reset();
            m_texture_byte_count = 0;
            m_grid          = Grid{};
            m_volume_bounds = erhe::math::Aabb{};
        }
        return;
    }
    if (!update_volume(command_buffer, scene_root)) {
        return;
    }

    // Materials: upload the scene's whole content library, like the raster
    // and ray trace paths do; Material_buffer::update() assigns each
    // material's material_buffer_index, which the instance records capture.
    const std::shared_ptr<Content_library>& content_library = scene_root.get_content_library();
    if (!content_library || !content_library->materials) {
        return;
    }
    const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials = content_library->materials->get_all<erhe::primitive::Material>();
    if (materials.empty()) {
        return;
    }

    // The lights need UBO slots and projection transforms even though DDGI
    // never samples a shadow map (it traces shadow rays): Light_buffer only
    // writes the slots present in the projections. Light_projections::apply
    // fits shadow frusta around a view camera, which DDGI has none of - the
    // fitted transforms are unused here, so any scene camera serves as the
    // fit reference.
    const std::vector<std::shared_ptr<erhe::scene::Camera>>& cameras = scene_root.get_scene().get_cameras();
    if (cameras.empty()) {
        return;
    }
    // Same limits the shadow render node resolves with: Light_set::resolve
    // recomputes whenever the limits differ from the last call, so handing
    // out a different set here would invalidate it every frame.
    const erhe::scene_renderer::Light_count_limits light_count_limits = (m_context.app_settings != nullptr)
        ? get_light_count_limits(m_context.app_settings->graphics.current_graphics_preset)
        : erhe::scene_renderer::Light_count_limits{};
    erhe::scene_renderer::Light_set& light_set = scene_root.get_light_set();
    light_set.resolve(scene_root.layers().light()->lights, light_count_limits);

    const Device_info& info = m_graphics_device.get_info();
    m_light_projections->apply(
        light_set,
        cameras.front().get(),
        erhe::math::Viewport{},
        erhe::math::Viewport{},
        {},     // no shadow map -> "no shadow map" sentinel in the light UBO
        m_graphics_device.get_reverse_depth(),
        info.coordinate_conventions.native_depth_range,
        info.coordinate_conventions
    );

    m_sky_radiance = glm::vec3{scene_root.get_scene().ambient_light};

    m_texture_heap->reset_heap(command_buffer);
    Ring_buffer_range material_range = m_material_buffer->update(*m_texture_heap.get(), materials);
    Scene_tlas::Frame tlas_frame     = m_scene_tlas->update(command_buffer, *scene_root.layers().content());
    ERHE_VERIFY(tlas_frame.is_valid());
    Ring_buffer_range light_range    = m_light_buffer->update(m_light_projections.get(), m_sky_radiance);
    Ring_buffer_range control_range  = update_control_buffer();

    command_buffer.transition_texture_layout(*m_ray_data_texture,   Image_layout::general);
    command_buffer.transition_texture_layout(*m_probe_data_texture, Image_layout::general);
    {
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_trace_bind_group_layout.get());
        encoder.set_compute_pipeline(*m_trace_pipeline);
        m_material_buffer->bind(encoder, material_range);
        m_light_buffer->bind_light_buffer(encoder, light_range);
        m_control_buffer->bind(encoder, control_range);
        m_scene_tlas->bind_instance_records(encoder, tlas_frame);
        encoder.set_acceleration_structure(m_tlas_binding_point, *tlas_frame.acceleration_structure);
        encoder.set_storage_image(m_ray_data_binding_point,   *m_ray_data_texture);
        encoder.set_storage_image(m_probe_data_binding_point, *m_probe_data_texture);
        m_texture_heap->bind(encoder);
        encoder.dispatch_compute(
            static_cast<std::uintptr_t>((m_rays_per_probe + c_trace_workgroup_size - 1) / c_trace_workgroup_size),
            static_cast<std::uintptr_t>(m_probes_per_update),
            1
        );
    }
    material_range.release();
    light_range.release();
    tlas_frame.instance_records.release();
    m_texture_heap->unbind(command_buffer);

    // The blend passes read this tick's ray data.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    command_buffer.transition_texture_layout(*m_irradiance_texture, Image_layout::general);
    command_buffer.transition_texture_layout(*m_distance_texture,   Image_layout::general);
    {
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        const auto blend = [&](const Blend_pass& pass, const std::shared_ptr<Texture>& atlas) {
            encoder.set_bind_group_layout(pass.bind_group_layout.get());
            encoder.set_compute_pipeline(*pass.pipeline);
            m_control_buffer->bind(encoder, control_range);
            encoder.set_storage_image(c_blend_ray_data_binding_point, *m_ray_data_texture);
            encoder.set_storage_image(c_blend_atlas_binding_point,    *atlas);
            encoder.dispatch_compute(static_cast<std::uintptr_t>(m_probes_per_update), 1, 1);
        };
        blend(m_blend_irradiance, m_irradiance_texture);
        blend(m_blend_distance,   m_distance_texture  );
    }
    control_range.release();

    // Both atlases become sampled textures for the DDGI window preview and,
    // from phase 6, the forward shading path.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    command_buffer.transition_texture_layout(*m_ray_data_texture,   Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_probe_data_texture, Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_irradiance_texture, Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_distance_texture,   Image_layout::shader_read_only_optimal);

    // Advance the round-robin cursor for the next tick.
    const uint32_t probe_count = static_cast<uint32_t>(m_grid.get_probe_count());
    m_probe_cursor = (m_probe_cursor + static_cast<uint32_t>(m_probes_per_update)) % probe_count;
}

} // namespace editor
