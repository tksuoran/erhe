#include "renderers/lightmap_baker.hpp"
#include "editor_log.hpp"

#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/acceleration_structure.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/fragment_output.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/image_writer.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include "SkylineBinPack.h" // RectangleBinPack

#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace editor {

namespace {

// Local-space surface area of a polygon mesh (fan triangulation per facet).
[[nodiscard]] auto mesh_surface_area(const GEO::Mesh& mesh) -> float
{
    double area = 0.0;
    for (GEO::index_t facet : mesh.facets) {
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        if (corner_count < 3) {
            continue;
        }
        const GEO::vec3f p0 = erhe::geometry::get_pointf(mesh.vertices, mesh.facet_corners.vertex(mesh.facets.corner(facet, 0)));
        for (GEO::index_t k = 2; k < corner_count; ++k) {
            const GEO::vec3f p1 = erhe::geometry::get_pointf(mesh.vertices, mesh.facet_corners.vertex(mesh.facets.corner(facet, k - 1)));
            const GEO::vec3f p2 = erhe::geometry::get_pointf(mesh.vertices, mesh.facet_corners.vertex(mesh.facets.corner(facet, k)));
            area += 0.5 * GEO::length(GEO::cross(p1 - p0, p2 - p0));
        }
    }
    return static_cast<float>(area);
}

// Area scale factor of a world transform: uniform-scale approximation from
// the 3x3 determinant (|det|^(2/3)); exact for uniform scale, close enough
// for texel-density purposes otherwise.
[[nodiscard]] auto area_scale(const glm::mat4& world_from_node) -> float
{
    const float det = glm::determinant(glm::mat3{world_from_node});
    return std::pow(std::abs(det), 2.0f / 3.0f);
}

// Per-draw UBO offsets must satisfy the device's uniform alignment; 256 is
// the specification maximum, valid everywhere.
constexpr std::size_t c_draw_ubo_stride = 256;

constexpr erhe::dataformat::Format c_position_format = erhe::dataformat::Format::format_32_vec4_float;
constexpr erhe::dataformat::Format c_normal_format   = erhe::dataformat::Format::format_32_vec4_float;

// The lightmap G-buffer vertex shader positions triangles by their
// channel-2 UVs mapped into the region's atlas rect. Which NDC y lands in
// texture row 0 differs per backend (see Texture_renderer's Y_SIGN note);
// ERHE_LM_Y_SIGN keeps "row 0 is v = 0" everywhere.
constexpr const char* c_vertex_source = R"GLSL(
layout(location = 0) out vec3 v_position;
layout(location = 1) out vec3 v_normal;

void main()
{
    vec2 atlas_uv = a_texcoord_2 * lightmap_draw.uv_scale_offset.xy + lightmap_draw.uv_scale_offset.zw;
    vec2 ndc      = atlas_uv * 2.0 - 1.0 + lightmap_draw.jitter_ndc.xy;
    gl_Position   = vec4(ndc.x, ERHE_LM_Y_SIGN * ndc.y, 0.0, 1.0);
    v_position    = (lightmap_draw.world_from_node * vec4(a_position, 1.0)).xyz;
    v_normal      = normalize(mat3(lightmap_draw.world_from_node) * a_normal);
}
)GLSL";

constexpr const char* c_fragment_source = R"GLSL(
layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;

void main()
{
    out_position = vec4(v_position, 1.0);          // w = coverage
    out_normal   = vec4(normalize(v_normal), 1.0); // w = coverage
}
)GLSL";

constexpr std::size_t c_max_gather_lights = 16;

// Direct-light gather: one thread per lightmap texel; explicit light
// sampling with a ray-query shadow ray per light. Bind-group declarations
// (s_tlas, s_position, s_normal, i_lightmap) and the lightmap_gather block
// are injected by erhe.
constexpr const char* c_gather_source = R"GLSL(
// Samplers / storage image / uniform block are declared by the bind group
// layout + interface blocks; the acceleration structure is not (matching
// ray_trace.comp) and is declared here at its layout binding.
layout(binding = 1) uniform accelerationStructureEXT s_tlas;

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size  = imageSize(i_lightmap);
    if ((texel.x >= size.x) || (texel.y >= size.y)) {
        return;
    }
    vec4 position_coverage = texelFetch(s_position, texel, 0);
    if (position_coverage.w <= 0.0) {
        imageStore(i_lightmap, texel, vec4(0.0));
        return;
    }
    vec3 p = position_coverage.xyz;
    vec3 n = normalize(texelFetch(s_normal, texel, 0).xyz);

    float ray_bias   = lightmap_gather.ray_bias;
    vec3  irradiance = vec3(0.0);
    for (uint i = 0u; i < lightmap_gather.light_count; ++i) {
        vec4 pos_type  = lightmap_gather.light_position_and_type[i];
        vec4 dir_cos   = lightmap_gather.light_direction_and_outer_cos[i];
        vec4 rad_range = lightmap_gather.light_radiance_and_range[i];
        vec4 params    = lightmap_gather.light_params[i];

        vec3  to_light;
        float attenuation = 1.0;
        float t_max       = 1.0e30;
        if (pos_type.w < 0.5) { // directional
            to_light = normalize(dir_cos.xyz);
        } else {
            vec3 d = pos_type.xyz - p;
            float distance = length(d);
            if (distance < 1.0e-6) {
                continue;
            }
            to_light    = d / distance;
            attenuation = 1.0 / max(distance * distance, 1.0e-4);
            t_max       = distance - ray_bias;
            if (pos_type.w > 1.5) { // spot: params.x = inner cos, dir_cos.w = outer cos
                float cos_angle = dot(to_light, normalize(dir_cos.xyz));
                attenuation *= smoothstep(dir_cos.w, params.x, cos_angle);
            }
        }
        float n_dot_l = dot(n, to_light);
        if ((n_dot_l <= 0.0) || (attenuation <= 0.0)) {
            continue;
        }
        vec3 origin = p + n * ray_bias;
        rayQueryEXT ray_query;
        // Back-facing triangles do not occlude: an origin that lands just
        // under an adjacent facet (curved surface, interpolated normal)
        // would otherwise see its own mesh from the inside and go black.
        // Closed occluders still block via their front faces.
        rayQueryInitializeEXT(
            ray_query,
            s_tlas,
            gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT,
            0xFFu,
            origin,
            1.0e-4,
            to_light,
            t_max
        );
        rayQueryProceedEXT(ray_query);
        if (rayQueryGetIntersectionTypeEXT(ray_query, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
            irradiance += rad_range.xyz * (n_dot_l * attenuation);
        }
    }
#if defined(ERHE_LM_DEBUG_GATHER)
    // Diagnostics: R = light_count/8, G = max NdotL over lights, B = shadow
    // miss ratio (1 = all rays reached their light).
    {
        float ndotl_max = 0.0;
        float misses    = 0.0;
        float rays      = 0.0;
        for (uint i = 0u; i < lightmap_gather.light_count; ++i) {
            vec3 to_light = normalize(lightmap_gather.light_direction_and_outer_cos[i].xyz);
            ndotl_max = max(ndotl_max, dot(n, to_light));
            rays += 1.0;
            rayQueryEXT rq2;
            rayQueryInitializeEXT(rq2, s_tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xFFu, p + n * lightmap_gather.ray_bias, 1.0e-4, to_light, 1.0e30);
            rayQueryProceedEXT(rq2);
            if (rayQueryGetIntersectionTypeEXT(rq2, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
                misses += 1.0;
            }
        }
        imageStore(i_lightmap, texel, vec4(float(lightmap_gather.light_count) / 8.0, ndotl_max, (rays > 0.0) ? misses / rays : 0.0, 1.0));
        return;
    }
#endif
    imageStore(i_lightmap, texel, vec4(irradiance, 1.0));
}
)GLSL";

// Dilation (plan phase 4): invalid texels (w == 0) take the average of
// their valid 8-neighbors and become valid, so repeated passes grow every
// chart outward one texel at a time. Valid texels pass through unchanged.
// i_src / i_dst are declared by the bind group layout.
constexpr const char* c_dilate_source = R"GLSL(
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size  = imageSize(i_dst);
    if ((texel.x >= size.x) || (texel.y >= size.y)) {
        return;
    }
    vec4 value = imageLoad(i_src, texel);
    if (value.w > 0.0) {
        imageStore(i_dst, texel, value);
        return;
    }
    vec3  sum   = vec3(0.0);
    float count = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if ((dx == 0) && (dy == 0)) {
                continue;
            }
            ivec2 neighbor = texel + ivec2(dx, dy);
            if ((neighbor.x < 0) || (neighbor.y < 0) || (neighbor.x >= size.x) || (neighbor.y >= size.y)) {
                continue;
            }
            vec4 neighbor_value = imageLoad(i_src, neighbor);
            if (neighbor_value.w > 0.0) {
                sum   += neighbor_value.rgb;
                count += 1.0;
            }
        }
    }
    imageStore(i_dst, texel, (count > 0.0) ? vec4(sum / count, 1.0) : vec4(0.0));
}
)GLSL";

} // anonymous namespace

Lightmap_baker::Lightmap_baker(erhe::graphics::Device& graphics_device, erhe::scene_renderer::Mesh_memory& mesh_memory)
    : m_graphics_device{graphics_device}
    , m_mesh_memory    {mesh_memory}
{
    using namespace erhe::graphics;

    m_draw_block = std::make_unique<Shader_resource>(
        graphics_device,
        Shader_resource::Block_create_info{
            .name          = "lightmap_draw",
            .binding_point = 0,
            .type          = Shader_resource::Type::uniform_block
        }
    );
    m_draw_block_world_offset  = m_draw_block->add_mat4("world_from_node")->get_offset_in_parent();
    m_draw_block_uv_offset     = m_draw_block->add_vec4("uv_scale_offset")->get_offset_in_parent();
    m_draw_block_jitter_offset = m_draw_block->add_vec4("jitter_ndc")->get_offset_in_parent();
    m_draw_block_size          = m_draw_block->get_size_bytes(Shader_resource::Layout::std140);

    m_bind_group_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                Bind_group_layout_binding{
                    .binding_point = 0u,
                    .type          = Binding_type::uniform_buffer,
                    .stage_flags   = Shader_stage_flags::vertex
                }
            },
            .debug_label       = erhe::utility::Debug_label{"lightmap gbuffer layout"},
            .uses_texture_heap = false
        }
    );
    m_fragment_outputs = std::make_unique<Fragment_outputs>(
        std::initializer_list<Fragment_output>{
            Fragment_output{ .name = "out_position", .type = Glsl_type::float_vec4, .location = 0 },
            Fragment_output{ .name = "out_normal",   .type = Glsl_type::float_vec4, .location = 1 }
        }
    );

    const bool top_left =
        (graphics_device.get_info().coordinate_conventions.texture_origin == erhe::math::Texture_origin::top_left);

    Shader_stages_create_info shader_create_info{
        .name             = "lightmap_gbuffer",
        .defines          = {
            { "ERHE_LM_Y_SIGN", top_left ? "-1.0" : "1.0" }
        },
        .interface_blocks = { m_draw_block.get() },
        .fragment_outputs = m_fragment_outputs.get(),
        .vertex_format    = &mesh_memory.vertex_format_not_skinned,
        .shaders = {
            { Shader_type::vertex_shader,   std::string_view{c_vertex_source} },
            { Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = m_bind_group_layout.get()
    };
    Shader_stages_prototype prototype = build_shader_stages(graphics_device, shader_create_info);
    if (!prototype.is_valid()) {
        log_render->warn("Lightmap_baker: G-buffer shader failed to compile/link");
        return;
    }
    m_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(prototype));

    const erhe::scene_renderer::Vertex_input_entry& vertex_input_entry =
        mesh_memory.get_vertex_input_from_vertex_format(mesh_memory.vertex_format_not_skinned);

    Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = Input_assembly_state::triangle;
    // Charts are rasterized in UV space. The unwrap is orientation-
    // preserving, so valid triangles keep the mesh's CCW winding there -
    // but folded parametrization triangles (observed on curved shapes:
    // ~1.6% of the torus) arrive mirrored and would overwrite good texels
    // with positions from elsewhere on the surface (they bake black).
    // Culling the flipped winding drops exactly the folds; texels only a
    // fold covered stay invalid and are filled by dilation instead.
    pipeline_create_info.base.rasterization                     = Rasterization_state::cull_mode_back_cw.with_winding_flip_if(top_left);
    pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    pipeline_create_info.base.bind_group_layout                 = m_bind_group_layout.get();
    pipeline_create_info.base.color_blend                       = &Color_blend_state::color_blend_disabled;
    pipeline_create_info.shader_stages                          = m_shader_stages.get();
    pipeline_create_info.vertex_input                           = vertex_input_entry.vertex_input.get();
    pipeline_create_info.color_attachment_count                 = 2;
    pipeline_create_info.color_attachment_formats[0]            = c_position_format;
    pipeline_create_info.color_attachment_formats[1]            = c_normal_format;
    pipeline_create_info.color_usage_before[0]                  = Image_usage_flag_bit_mask::sampled;
    pipeline_create_info.color_usage_after[0]                   = Image_usage_flag_bit_mask::sampled;
    pipeline_create_info.color_usage_before[1]                  = Image_usage_flag_bit_mask::sampled;
    pipeline_create_info.color_usage_after[1]                   = Image_usage_flag_bit_mask::sampled;
    pipeline_create_info.sample_count                           = 1;

    m_pipeline = std::make_unique<Render_pipeline>(graphics_device, pipeline_create_info);
    if (!m_pipeline->is_valid()) {
        log_render->warn("Lightmap_baker: G-buffer pipeline is not valid");
        m_pipeline.reset();
    }

    // Direct-light gather: ray query in compute, exactly the machinery
    // Ray_trace_renderer proves out. Absent ray query, the layout +
    // G-buffer still work; only baking is unavailable.
    if (!graphics_device.get_info().use_ray_query) {
        log_render->info("Lightmap_baker: ray query not available, lightmap gather disabled");
        return;
    }

    m_gather_block = std::make_unique<Shader_resource>(
        graphics_device,
        Shader_resource::Block_create_info{
            .name          = "lightmap_gather",
            .binding_point = 0,
            .type          = Shader_resource::Type::uniform_block
        }
    );
    m_gather_light_count_offset    = m_gather_block->add_uint ("light_count")->get_offset_in_parent();
    m_gather_ray_bias_offset       = m_gather_block->add_float("ray_bias")->get_offset_in_parent();
    m_gather_block->add_float("gather_reserved_0");
    m_gather_block->add_float("gather_reserved_1");
    m_gather_position_type_offset  = m_gather_block->add_vec4("light_position_and_type",       c_max_gather_lights)->get_offset_in_parent();
    m_gather_direction_cos_offset  = m_gather_block->add_vec4("light_direction_and_outer_cos", c_max_gather_lights)->get_offset_in_parent();
    m_gather_radiance_range_offset = m_gather_block->add_vec4("light_radiance_and_range",      c_max_gather_lights)->get_offset_in_parent();
    m_gather_params_offset         = m_gather_block->add_vec4("light_params",                  c_max_gather_lights)->get_offset_in_parent();
    m_gather_block_size            = m_gather_block->get_size_bytes(Shader_resource::Layout::std140);

    m_gather_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                Bind_group_layout_binding{
                    .binding_point = 0u,
                    .type          = Binding_type::uniform_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point = 1u,
                    .type          = Binding_type::acceleration_structure,
                    .name          = "s_tlas",
                    .stage_flags   = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point   = 2u,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_position",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point   = 3u,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_normal",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                // Combined image samplers land at vk binding = user + 1
                // (offset past the uniform buffer at 0), i.e. vk 3 and 4;
                // raw bindings (TLAS, storage image) are NOT offset, so the
                // storage image sits at 5 to stay clear of s_normal's vk 4.
                Bind_group_layout_binding{
                    .binding_point = 5u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_lightmap",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba32f",
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label       = erhe::utility::Debug_label{"lightmap gather layout"},
            .uses_texture_heap = false
        }
    );

    Shader_stages_create_info gather_create_info{
        .name             = "lightmap_gather",
        .extensions       = {
            { Shader_type::compute_shader, "GL_EXT_ray_query" }
        },
        .interface_blocks = { m_gather_block.get() },
        .shaders = {
            { Shader_type::compute_shader, std::string_view{c_gather_source} }
        },
        .bind_group_layout = m_gather_layout.get()
    };
    Shader_stages_prototype gather_prototype = build_shader_stages(graphics_device, gather_create_info);
    if (!gather_prototype.is_valid()) {
        log_render->warn("Lightmap_baker: gather shader failed to compile/link");
        return;
    }
    m_gather_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(gather_prototype));
    m_gather_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "lightmap_gather",
            .shader_stages     = m_gather_shader_stages.get(),
            .bind_group_layout = m_gather_layout.get()
        }
    );
    m_nearest_sampler = std::make_unique<Sampler>(
        graphics_device,
        Sampler_create_info{
            .min_filter  = Filter::nearest,
            .mag_filter  = Filter::nearest,
            .mipmap_mode = Sampler_mipmap_mode::not_mipmapped,
            .debug_label = "lightmap gather sampler"
        }
    );

    // Dilation pipeline. Both bindings are raw storage images, so the
    // combined-image-sampler binding offset (see the gather layout note)
    // does not apply.
    m_dilate_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                Bind_group_layout_binding{
                    .binding_point = 0u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_src",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba32f",
                    .stage_flags   = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point = 1u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_dst",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba32f",
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label       = erhe::utility::Debug_label{"lightmap dilate layout"},
            .uses_texture_heap = false
        }
    );
    Shader_stages_create_info dilate_create_info{
        .name    = "lightmap_dilate",
        .shaders = {
            { Shader_type::compute_shader, std::string_view{c_dilate_source} }
        },
        .bind_group_layout = m_dilate_layout.get()
    };
    Shader_stages_prototype dilate_prototype = build_shader_stages(graphics_device, dilate_create_info);
    if (!dilate_prototype.is_valid()) {
        log_render->warn("Lightmap_baker: dilate shader failed to compile/link");
        return;
    }
    m_dilate_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(dilate_prototype));
    m_dilate_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "lightmap_dilate",
            .shader_stages     = m_dilate_shader_stages.get(),
            .bind_group_layout = m_dilate_layout.get()
        }
    );
}

Lightmap_baker::~Lightmap_baker() noexcept = default;

auto Lightmap_baker::is_supported() const -> bool
{
    return static_cast<bool>(m_pipeline);
}

auto Lightmap_baker::update_layout(Scene_root& scene_root, const float texels_per_meter) -> bool
{
    m_layout         = Atlas_layout{};
    m_gbuffer_valid  = false;
    m_lightmap_valid = false;

    std::vector<Instance_region> regions;
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
        if (!mesh || mesh->skin) {
            continue;
        }
        if ((mesh->get_flag_bits() & erhe::Item_flags::lightmapped) == 0u) {
            continue;
        }
        const erhe::scene::Node* const node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        const float instance_area_scale = area_scale(node->world_from_node());
        const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
        for (std::size_t primitive_index = 0; primitive_index < primitives.size(); ++primitive_index) {
            const erhe::primitive::Primitive* const primitive = primitives[primitive_index].primitive.get();
            if ((primitive == nullptr) || !primitive->render_shape) {
                continue;
            }
            const std::shared_ptr<erhe::geometry::Geometry>& geometry = primitive->render_shape->get_geometry();
            if (!geometry) {
                continue;
            }
            // Only primitives that have lightmap UVs (channel 2) participate;
            // Generate Lightmap UVs in the Lightmap window produces them.
            erhe::geometry::Mesh_attributes& attributes = geometry->get_attributes();
            if (!attributes.corner_texcoord_2.has(0)) {
                continue;
            }
            Instance_region region;
            region.mesh            = mesh;
            region.primitive_index = primitive_index;
            region.world_area      = mesh_surface_area(geometry->get_mesh()) * instance_area_scale;
            regions.push_back(std::move(region));
        }
    }
    if (regions.empty()) {
        return false;
    }

    // Region content side in texels; the normalized per-mesh chart set is
    // square, so the region is too.
    const auto side_of = [texels_per_meter](const Instance_region& region) -> int {
        const float side = std::sqrt(std::max(region.world_area, 0.0f)) * texels_per_meter;
        return std::clamp(static_cast<int>(std::ceil(side)), 4, s_max_page - 2 * s_padding);
    };

    // Big regions first packs tighter with the skyline heuristic.
    std::sort(
        regions.begin(),
        regions.end(),
        [&](const Instance_region& lhs, const Instance_region& rhs) { return side_of(lhs) > side_of(rhs); }
    );

    for (int page = s_min_page; page <= s_max_page; page *= 2) {
        rbp::SkylineBinPack packer;
        packer.Init(page, page, false);
        bool failed = false;
        for (Instance_region& region : regions) {
            const int side = side_of(region);
            const rbp::Rect rect = packer.Insert(side + 2 * s_padding, side + 2 * s_padding, rbp::SkylineBinPack::LevelBottomLeft);
            if ((rect.width == 0) || (rect.height == 0)) {
                failed = true;
                break;
            }
            region.x      = rect.x + s_padding;
            region.y      = rect.y + s_padding;
            region.width  = side;
            region.height = side;
        }
        if (failed) {
            continue;
        }
        const float inv_page = 1.0f / static_cast<float>(page);
        for (Instance_region& region : regions) {
            region.uv_scale_offset = glm::vec4{
                static_cast<float>(region.width)  * inv_page,
                static_cast<float>(region.height) * inv_page,
                static_cast<float>(region.x)      * inv_page,
                static_cast<float>(region.y)      * inv_page
            };
        }
        m_layout.width   = page;
        m_layout.height  = page;
        m_layout.regions = std::move(regions);
        return true;
    }
    // Even the largest page failed; drop the layout (a later change can
    // add multi-page support - plan keeps pages <= 4096^2).
    return false;
}

void Lightmap_baker::ensure_gbuffer_targets()
{
    using namespace erhe::graphics;
    const bool matches =
        m_position_texture &&
        (m_position_texture->get_width()  == m_layout.width) &&
        (m_position_texture->get_height() == m_layout.height);
    if (matches) {
        return;
    }
    const auto make_target = [this](const char* label, erhe::dataformat::Format format) {
        return std::make_shared<Texture>(
            m_graphics_device,
            Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  =
                    Image_usage_flag_bit_mask::color_attachment |
                    Image_usage_flag_bit_mask::sampled          |
                    Image_usage_flag_bit_mask::transfer_src,
                .type        = Texture_type::texture_2d,
                .pixelformat = format,
                .width       = m_layout.width,
                .height      = m_layout.height,
                .debug_label = erhe::utility::Debug_label{label}
            }
        );
    };
    m_position_texture = make_target("lightmap gbuffer position", c_position_format);
    m_normal_texture   = make_target("lightmap gbuffer normal",   c_normal_format);
    m_gbuffer_valid    = false;
}

auto Lightmap_baker::bake_gbuffer() -> bool
{
    using namespace erhe::graphics;

    if (!m_pipeline || (m_layout.width == 0) || m_layout.regions.empty()) {
        return false;
    }

    const std::size_t not_skinned_key = m_mesh_memory.get_vertex_input_from_vertex_format(m_mesh_memory.vertex_format_not_skinned).key;

    ensure_gbuffer_targets();

    // Multi-jitter conservative coverage (plan phase 2, Bakery-style): each
    // region rasterizes 9 times with sub-texel NDC offsets, center pass
    // LAST. Depth test is off, so later draws win: edge texels whose center
    // just misses every triangle still get a jittered write, and properly
    // covered texels end with the unjittered value.
    constexpr int c_jitter_count = 9;
    constexpr float c_jitter[c_jitter_count][2] = {
        {-1.0f, -1.0f}, {0.0f, -1.0f}, {1.0f, -1.0f},
        {-1.0f,  0.0f},                {1.0f,  0.0f},
        {-1.0f,  1.0f}, {0.0f,  1.0f}, {1.0f,  1.0f},
        { 0.0f,  0.0f} // center last
    };

    // Per-draw UBO: one record per region per jitter pass.
    const std::size_t ubo_bytes = m_layout.regions.size() * c_jitter_count * c_draw_ubo_stride;
    Buffer draw_ubo{
        m_graphics_device,
        Buffer_create_info{
            .capacity_byte_count                    = ubo_bytes,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::uniform,
            .required_memory_property_bit_mask      =
                Memory_property_flag_bit_mask::host_read |
                Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     =
                Memory_property_flag_bit_mask::host_coherent |
                Memory_property_flag_bit_mask::host_persistent,
            .debug_label = erhe::utility::Debug_label{"lightmap gbuffer draw ubo"}
        }
    };
    {
        // Half-texel jitter magnitude in NDC ([-1,1] spans the page).
        const float jitter_step_x = 0.5f * 2.0f / static_cast<float>(m_layout.width);
        const float jitter_step_y = 0.5f * 2.0f / static_cast<float>(m_layout.height);
        const std::span<std::byte> mapped = draw_ubo.map_bytes(0, ubo_bytes);
        std::memset(mapped.data(), 0, ubo_bytes);
        for (int j = 0; j < c_jitter_count; ++j) {
            const glm::vec4 jitter_ndc{c_jitter[j][0] * jitter_step_x, c_jitter[j][1] * jitter_step_y, 0.0f, 0.0f};
            for (std::size_t i = 0; i < m_layout.regions.size(); ++i) {
                const Instance_region& region = m_layout.regions[i];
                const erhe::scene::Node* const node = region.mesh ? region.mesh->get_node() : nullptr;
                const glm::mat4 world_from_node = (node != nullptr) ? node->world_from_node() : glm::mat4{1.0f};
                std::byte* const record = mapped.data() + (static_cast<std::size_t>(j) * m_layout.regions.size() + i) * c_draw_ubo_stride;
                std::memcpy(record + m_draw_block_world_offset,  &world_from_node,        sizeof(glm::mat4));
                std::memcpy(record + m_draw_block_uv_offset,     &region.uv_scale_offset, sizeof(glm::vec4));
                std::memcpy(record + m_draw_block_jitter_offset, &jitter_ndc,             sizeof(glm::vec4));
            }
        }
        draw_ubo.unmap();
    }

    // Standalone submit on a dedicated thread slot (7 is the texture graph
    // export; 6 keeps this bake from disturbing either that or the open
    // frame command buffer on slot 0).
    constexpr unsigned int bake_thread_slot = 6;
    Command_buffer& command_buffer = m_graphics_device.get_command_buffer(bake_thread_slot);
    command_buffer.begin();

    // Fresh targets start UNDEFINED; normalize so the render pass can use a
    // uniform layout_before.
    command_buffer.transition_texture_layout(*m_position_texture, Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_normal_texture,   Image_layout::shader_read_only_optimal);

    Render_pass_descriptor descriptor{};
    for (int i = 0; i < 2; ++i) {
        Render_pass_attachment_descriptor& attachment = descriptor.color_attachments[i];
        attachment.texture       = (i == 0) ? m_position_texture.get() : m_normal_texture.get();
        attachment.clear_value   = std::array<double, 4>{0.0, 0.0, 0.0, 0.0};
        attachment.load_action   = Load_action::Clear;
        attachment.store_action  = Store_action::Store;
        attachment.usage_before  = Image_usage_flag_bit_mask::sampled;
        attachment.layout_before = Image_layout::shader_read_only_optimal;
        attachment.usage_after   = Image_usage_flag_bit_mask::sampled;
        attachment.layout_after  = Image_layout::shader_read_only_optimal;
    }
    descriptor.render_target_width  = m_layout.width;
    descriptor.render_target_height = m_layout.height;
    descriptor.debug_label = erhe::utility::Debug_label{"lightmap gbuffer"};

    std::size_t drawn = 0;
    {
        Render_pass            render_pass{m_graphics_device, descriptor};
        Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(command_buffer);
        const Scoped_render_pass scoped{render_pass, command_buffer};
        encoder.set_viewport_rect(0, 0, m_layout.width, m_layout.height);
        encoder.set_scissor_rect (0, 0, m_layout.width, m_layout.height);
        encoder.set_bind_group_layout(m_bind_group_layout.get());
        encoder.set_render_pipeline(*m_pipeline);

        for (int j = 0; j < c_jitter_count; ++j) {
            for (std::size_t i = 0; i < m_layout.regions.size(); ++i) {
                const Instance_region& region = m_layout.regions[i];
                if (!region.mesh) {
                    continue;
                }
                const std::vector<erhe::scene::Mesh_primitive>& primitives = region.mesh->get_primitives();
                if (region.primitive_index >= primitives.size()) {
                    continue;
                }
                const erhe::primitive::Primitive* const primitive = primitives[region.primitive_index].primitive.get();
                if (primitive == nullptr) {
                    continue;
                }
                const erhe::primitive::Buffer_mesh* const buffer_mesh = primitive->get_renderable_mesh();
                if ((buffer_mesh == nullptr) || (buffer_mesh->triangle_fill_indices.index_count == 0)) {
                    continue;
                }
                if (buffer_mesh->vertex_input_key != not_skinned_key) {
                    // Pipeline vertex input is the non-skinned content format;
                    // anything else (should not happen after the skin filter)
                    // is skipped rather than mis-bound.
                    continue;
                }
                for (std::size_t stream = 0; stream < buffer_mesh->vertex_buffer_ranges.size(); ++stream) {
                    const erhe::primitive::Buffer_range& range = buffer_mesh->vertex_buffer_ranges[stream];
                    encoder.set_vertex_buffer(m_mesh_memory.get_vertex_buffer(range), range.byte_offset, stream);
                }
                const erhe::primitive::Buffer_range& index_range = buffer_mesh->index_buffer_range;
                encoder.set_index_buffer(m_mesh_memory.get_index_buffer(index_range));
                const std::size_t record_index = static_cast<std::size_t>(j) * m_layout.regions.size() + i;
                encoder.set_buffer(Buffer_target::uniform, &draw_ubo, record_index * c_draw_ubo_stride, m_draw_block_size, 0);

                const erhe::dataformat::Format index_format = m_mesh_memory.get_index_format(
                    erhe::scene_renderer::Pool_buffer_identity{index_range.pool_id, index_range.buffer_id}
                );
                const std::uintptr_t index_offset =
                    index_range.byte_offset +
                    buffer_mesh->triangle_fill_indices.first_index * index_range.element_size;
                encoder.draw_indexed_primitives(
                    Primitive_type::triangle,
                    buffer_mesh->triangle_fill_indices.index_count,
                    index_format,
                    index_offset
                );
                if (j == 0) {
                    ++drawn;
                }
            }
        }
    }
    command_buffer.end();
    Command_buffer* command_buffers[] = { &command_buffer };
    m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    m_graphics_device.wait_idle();

    m_gbuffer_valid = drawn > 0;
    log_render->info("Lightmap_baker: G-buffer baked, {} of {} regions drawn, {}x{}", drawn, m_layout.regions.size(), m_layout.width, m_layout.height);
    return m_gbuffer_valid;
}

auto Lightmap_baker::debug_write_gbuffer_pngs(const std::string& base_path) -> bool
{
    using namespace erhe::graphics;
    if (!m_gbuffer_valid || !m_position_texture || !m_normal_texture) {
        return false;
    }
    const int         width         = m_layout.width;
    const int         height        = m_layout.height;
    const std::size_t texel_count   = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t bytes_per_row = static_cast<std::size_t>(width) * 16u; // rgba32f
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(height);

    const auto read_texture = [&](Texture& texture, std::vector<float>& out_data) -> bool {
        Buffer readback{
            m_graphics_device,
            Buffer_create_info{
                .capacity_byte_count                    = byte_count,
                .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
                .usage                                  = Buffer_usage::transfer_dst | Buffer_usage::storage,
                .required_memory_property_bit_mask      =
                    Memory_property_flag_bit_mask::host_read |
                    Memory_property_flag_bit_mask::host_write,
                .preferred_memory_property_bit_mask     =
                    Memory_property_flag_bit_mask::host_coherent |
                    Memory_property_flag_bit_mask::host_persistent,
                .debug_label = erhe::utility::Debug_label{"lightmap gbuffer readback"}
            }
        };
        constexpr unsigned int bake_thread_slot = 6;
        Command_buffer& command_buffer = m_graphics_device.get_command_buffer(bake_thread_slot);
        command_buffer.begin();
        command_buffer.transition_texture_layout(texture, Image_layout::transfer_src_optimal);
        {
            Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
            blit.copy_from_texture(
                &texture,
                0, 0,
                glm::ivec3{0, 0, 0},
                glm::ivec3{width, height, 1},
                &readback,
                0,
                bytes_per_row,
                byte_count
            );
        }
        command_buffer.transition_texture_layout(texture, Image_layout::shader_read_only_optimal);
        command_buffer.end();
        Command_buffer* command_buffers[] = { &command_buffer };
        m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
        m_graphics_device.wait_idle();
        const std::span<std::byte> mapped = readback.map_bytes(0, byte_count);
        out_data.resize(texel_count * 4u);
        std::memcpy(out_data.data(), mapped.data(), byte_count);
        readback.unmap();
        return true;
    };

    std::vector<float> position_data;
    std::vector<float> normal_data;
    if (!read_texture(*m_position_texture, position_data) || !read_texture(*m_normal_texture, normal_data)) {
        return false;
    }

    // Positions map into the covered world bounds so the PNG uses the full
    // 8-bit range; normals map as n * 0.5 + 0.5. Alpha = coverage.
    glm::vec3 minimum{std::numeric_limits<float>::max()};
    glm::vec3 maximum{std::numeric_limits<float>::lowest()};
    for (std::size_t i = 0; i < texel_count; ++i) {
        if (position_data[i * 4 + 3] <= 0.0f) {
            continue;
        }
        for (int c = 0; c < 3; ++c) {
            minimum[c] = std::min(minimum[c], position_data[i * 4 + c]);
            maximum[c] = std::max(maximum[c], position_data[i * 4 + c]);
        }
    }
    const glm::vec3 extent = glm::max(maximum - minimum, glm::vec3{1e-6f});

    std::unique_ptr<Image_writer> writer = Image_writer::create();
    if (!writer || !writer->is_supported()) {
        return false;
    }
    const auto write_image = [&](const std::string& path, const std::vector<float>& data, const bool is_position) -> bool {
        std::vector<std::uint8_t> pixels(texel_count * 4u);
        for (std::size_t i = 0; i < texel_count; ++i) {
            const float coverage = data[i * 4 + 3];
            for (int c = 0; c < 3; ++c) {
                const float value = is_position
                    ? (data[i * 4 + c] - minimum[c]) / extent[c]
                    : data[i * 4 + c] * 0.5f + 0.5f;
                pixels[i * 4 + c] = static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
            pixels[i * 4 + 3] = (coverage > 0.0f) ? 255u : 0u;
        }
        return writer->write_png(
            std::filesystem::path{path},
            width,
            height,
            width * 4,
            erhe::dataformat::Format::format_8_vec4_unorm,
            std::span<const std::byte>{reinterpret_cast<const std::byte*>(pixels.data()), pixels.size()}
        );
    };
    const bool position_ok = write_image(base_path + "_position.png", position_data, true);
    const bool normal_ok   = write_image(base_path + "_normal.png",   normal_data,   false);
    return position_ok && normal_ok;
}

auto Lightmap_baker::get_or_create_blas(
    erhe::graphics::Command_buffer&                    command_buffer,
    const std::shared_ptr<erhe::primitive::Primitive>& primitive,
    const erhe::primitive::Buffer_mesh&                buffer_mesh
) -> erhe::graphics::Acceleration_structure*
{
    using namespace erhe::graphics;

    const auto existing = m_blas_cache.find(&buffer_mesh);
    if (existing != m_blas_cache.end()) {
        return existing->second.acceleration_structure.get();
    }
    // Mirrors Ray_trace_renderer::get_or_create_blas: triangles read in
    // place from the mesh memory pools (stream 0 leads with position,
    // uint32 triangle-list indices relative to the range start).
    if (buffer_mesh.vertex_buffer_ranges.empty()) {
        return nullptr;
    }
    const erhe::primitive::Buffer_range& vertex_range = buffer_mesh.vertex_buffer_ranges[0];
    const erhe::primitive::Buffer_range& index_range  = buffer_mesh.index_buffer_range;
    const erhe::primitive::Index_range&  triangles    = buffer_mesh.triangle_fill_indices;
    if ((triangles.index_count == 0) || (triangles.primitive_type != erhe::primitive::Primitive_type::triangles)) {
        return nullptr;
    }
    if ((vertex_range.count == 0) || (index_range.element_size != sizeof(uint32_t))) {
        return nullptr;
    }
    erhe::graphics::Buffer* vertex_buffer = m_mesh_memory.get_vertex_buffer(vertex_range);
    erhe::graphics::Buffer* index_buffer  = m_mesh_memory.get_index_buffer(index_range);
    if ((vertex_buffer == nullptr) || (index_buffer == nullptr)) {
        return nullptr;
    }

    Blas_entry& entry = m_blas_cache[&buffer_mesh];
    entry.primitive = primitive;
    entry.acceleration_structure = std::make_unique<Acceleration_structure>(
        m_graphics_device,
        Acceleration_structure_create_info{
            .type                = Acceleration_structure_type::bottom_level,
            .triangle_geometries = {
                Acceleration_structure_triangles{
                    .vertex_buffer      = vertex_buffer,
                    .vertex_byte_offset = vertex_range.byte_offset,
                    .vertex_byte_stride = vertex_range.element_size,
                    .vertex_count       = vertex_range.count,
                    .index_buffer       = index_buffer,
                    .index_byte_offset  = index_range.byte_offset + (triangles.first_index * index_range.element_size),
                    .index_count        = triangles.index_count,
                    .opaque             = true
                }
            },
            .debug_label = erhe::utility::Debug_label{"Lightmap BLAS"}
        }
    );
    entry.acceleration_structure->build(command_buffer);
    return entry.acceleration_structure.get();
}

auto Lightmap_baker::bake_direct(Scene_root& scene_root) -> bool
{
    using namespace erhe::graphics;

    if (!m_gather_pipeline || !m_gbuffer_valid || !m_position_texture) {
        return false;
    }

    // Lightmap accumulation target at the atlas size.
    const bool lightmap_matches =
        m_lightmap_texture &&
        (m_lightmap_texture->get_width()  == m_layout.width) &&
        (m_lightmap_texture->get_height() == m_layout.height);
    if (!lightmap_matches) {
        m_lightmap_texture = std::make_shared<Texture>(
            m_graphics_device,
            Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  =
                    Image_usage_flag_bit_mask::storage  |
                    Image_usage_flag_bit_mask::sampled  |
                    Image_usage_flag_bit_mask::transfer_src,
                .type        = Texture_type::texture_2d,
                .pixelformat = erhe::dataformat::Format::format_32_vec4_float,
                .width       = m_layout.width,
                .height      = m_layout.height,
                .debug_label = erhe::utility::Debug_label{"lightmap atlas"}
            }
        );
        m_dilate_texture.reset();
    }
    if (m_dilate_pipeline && !m_dilate_texture) {
        m_dilate_texture = std::make_shared<Texture>(
            m_graphics_device,
            Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  = Image_usage_flag_bit_mask::storage,
                .type        = Texture_type::texture_2d,
                .pixelformat = erhe::dataformat::Format::format_32_vec4_float,
                .width       = m_layout.width,
                .height      = m_layout.height,
                .debug_label = erhe::utility::Debug_label{"lightmap dilate scratch"}
            }
        );
    }

    // Scene lights into the gather UBO.
    struct Light_record
    {
        glm::vec4 position_and_type;
        glm::vec4 direction_and_outer_cos;
        glm::vec4 radiance_and_range;
        glm::vec4 params;
    };
    std::vector<Light_record> lights;
    for (const std::shared_ptr<erhe::scene::Light>& light : scene_root.layers().light()->lights) {
        if (!light || !light->is_visible() || (lights.size() >= c_max_gather_lights)) {
            continue;
        }
        const erhe::scene::Node* const node = light->get_node();
        if (node == nullptr) {
            continue;
        }
        const glm::mat4 world_from_node = node->world_from_node();
        // Light direction convention matches Light_buffer: node +Z axis.
        const glm::vec3 direction = glm::normalize(glm::vec3{world_from_node * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f}});
        const glm::vec3 position  = glm::vec3{world_from_node * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
        const glm::vec3 radiance  = light->intensity * light->get_effective_color();
        float type_value = 0.0f;
        switch (light->type) {
            case erhe::scene::Light_type::directional: type_value = 0.0f; break;
            case erhe::scene::Light_type::point:       type_value = 1.0f; break;
            case erhe::scene::Light_type::spot:        type_value = 2.0f; break;
            default: continue;
        }
        lights.push_back(
            Light_record{
                .position_and_type       = glm::vec4{position, type_value},
                .direction_and_outer_cos = glm::vec4{direction, std::cos(light->outer_spot_angle * 0.5f)},
                .radiance_and_range      = glm::vec4{radiance, light->range},
                .params                  = glm::vec4{std::cos(light->inner_spot_angle * 0.5f), 0.0f, 0.0f, 0.0f}
            }
        );
    }

    Buffer gather_ubo{
        m_graphics_device,
        Buffer_create_info{
            .capacity_byte_count                    = m_gather_block_size,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::uniform,
            .required_memory_property_bit_mask      =
                Memory_property_flag_bit_mask::host_read |
                Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     =
                Memory_property_flag_bit_mask::host_coherent |
                Memory_property_flag_bit_mask::host_persistent,
            .debug_label = erhe::utility::Debug_label{"lightmap gather ubo"}
        }
    };
    {
        const std::span<std::byte> mapped = gather_ubo.map_bytes(0, m_gather_block_size);
        std::memset(mapped.data(), 0, m_gather_block_size);
        const uint32_t light_count = static_cast<uint32_t>(lights.size());
        const float    ray_bias    = 0.01f;
        std::memcpy(mapped.data() + m_gather_light_count_offset, &light_count, sizeof(uint32_t));
        std::memcpy(mapped.data() + m_gather_ray_bias_offset,    &ray_bias,    sizeof(float));
        for (std::size_t i = 0; i < lights.size(); ++i) {
            std::memcpy(mapped.data() + m_gather_position_type_offset  + i * sizeof(glm::vec4), &lights[i].position_and_type,       sizeof(glm::vec4));
            std::memcpy(mapped.data() + m_gather_direction_cos_offset  + i * sizeof(glm::vec4), &lights[i].direction_and_outer_cos, sizeof(glm::vec4));
            std::memcpy(mapped.data() + m_gather_radiance_range_offset + i * sizeof(glm::vec4), &lights[i].radiance_and_range,      sizeof(glm::vec4));
            std::memcpy(mapped.data() + m_gather_params_offset         + i * sizeof(glm::vec4), &lights[i].params,                  sizeof(glm::vec4));
        }
        gather_ubo.unmap();
    }

    constexpr unsigned int bake_thread_slot = 6;
    Command_buffer& command_buffer = m_graphics_device.get_command_buffer(bake_thread_slot);
    command_buffer.begin();

    // Shadow world: every visible, non-skinned content mesh occludes,
    // whether or not it is lightmapped.
    std::vector<Acceleration_structure_instance> instances;
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
        if (!mesh || !mesh->is_visible() || mesh->skin) {
            continue;
        }
        const erhe::scene::Node* const node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        const glm::mat4 world_from_node = node->world_from_node();
        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
            if (!mesh_primitive.primitive) {
                continue;
            }
            const erhe::primitive::Buffer_mesh* buffer_mesh = mesh_primitive.primitive->get_renderable_mesh();
            if (buffer_mesh == nullptr) {
                continue;
            }
            Acceleration_structure* blas = get_or_create_blas(command_buffer, mesh_primitive.primitive, *buffer_mesh);
            if (blas == nullptr) {
                continue;
            }
            instances.push_back(
                Acceleration_structure_instance{
                    .transform             = world_from_node,
                    .instance_custom_index = static_cast<uint32_t>(instances.size()),
                    .mask                  = 0xFFu,
                    .bottom_level          = blas,
                    // The gather's shadow rays cull back-facing triangles
                    // (self-hit leak defense), so facing culling must stay
                    // enabled on lightmap TLAS instances.
                    .disable_facing_cull   = false
                }
            );
        }
    }
    if (instances.empty()) {
        command_buffer.end();
        return false;
    }
    const uint32_t required_capacity = static_cast<uint32_t>(instances.size());
    if (!m_tlas || (m_tlas_capacity < required_capacity)) {
        const uint32_t new_capacity = std::max(64u, std::bit_ceil(required_capacity));
        m_tlas = std::make_unique<Acceleration_structure>(
            m_graphics_device,
            Acceleration_structure_create_info{
                .type               = Acceleration_structure_type::top_level,
                .max_instance_count = new_capacity,
                .debug_label        = erhe::utility::Debug_label{"Lightmap TLAS"}
            }
        );
        m_tlas_capacity = new_capacity;
    }
    m_tlas->build(command_buffer, instances);

    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::general);
    {
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_gather_layout.get());
        encoder.set_compute_pipeline(*m_gather_pipeline);
        encoder.set_buffer(Buffer_target::uniform, &gather_ubo, 0, m_gather_block_size, 0);
        encoder.set_acceleration_structure(1u, *m_tlas);
        encoder.set_sampled_image(2u, *m_position_texture, *m_nearest_sampler);
        encoder.set_sampled_image(3u, *m_normal_texture,   *m_nearest_sampler);
        encoder.set_storage_image(5u, *m_lightmap_texture);
        encoder.dispatch_compute(
            (static_cast<std::uintptr_t>(m_layout.width)  + 7) / 8,
            (static_cast<std::uintptr_t>(m_layout.height) + 7) / 8,
            1
        );
    }

    // Dilation: an even iteration count >= s_padding, so the final pass
    // lands back in m_lightmap_texture.
    if (m_dilate_pipeline && m_dilate_texture) {
        command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
        command_buffer.transition_texture_layout(*m_dilate_texture, Image_layout::general);
        constexpr int dilate_iterations = 2 * ((s_padding + 1) / 2);
        Texture* const ping[2] = { m_lightmap_texture.get(), m_dilate_texture.get() };
        for (int i = 0; i < dilate_iterations; ++i) {
            {
                Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
                encoder.set_bind_group_layout(m_dilate_layout.get());
                encoder.set_compute_pipeline(*m_dilate_pipeline);
                encoder.set_storage_image(0u, *ping[i & 1]);
                encoder.set_storage_image(1u, *ping[(i + 1) & 1]);
                encoder.dispatch_compute(
                    (static_cast<std::uintptr_t>(m_layout.width)  + 7) / 8,
                    (static_cast<std::uintptr_t>(m_layout.height) + 7) / 8,
                    1
                );
            }
            command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
        }
    }
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::shader_read_only_optimal);
    command_buffer.end();
    Command_buffer* command_buffers[] = { &command_buffer };
    m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    m_graphics_device.wait_idle();

    m_lightmap_valid = true;

    // Publish per-primitive atlas regions so the forward renderer samples
    // the fresh bake (Primitive_buffer uploads the value per draw; zero =
    // no lightmap). Only set on success - meshes keep sampling nothing
    // until their region holds baked data.
    for (const Instance_region& region : m_layout.regions) {
        if (!region.mesh) {
            continue;
        }
        std::vector<erhe::scene::Mesh_primitive>& primitives = region.mesh->get_mutable_primitives();
        if (region.primitive_index < primitives.size()) {
            primitives[region.primitive_index].lightmap_uv_scale_offset = region.uv_scale_offset;
        }
    }

    log_render->info(
        "Lightmap_baker: direct light baked, {} lights, {} occluder instances, {}x{}",
        lights.size(), instances.size(), m_layout.width, m_layout.height
    );
    return true;
}

auto Lightmap_baker::debug_write_lightmap_png(const std::string& path) -> bool
{
    using namespace erhe::graphics;
    if (!m_lightmap_valid || !m_lightmap_texture) {
        return false;
    }
    const int         width         = m_layout.width;
    const int         height        = m_layout.height;
    const std::size_t texel_count   = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t bytes_per_row = static_cast<std::size_t>(width) * 16u;
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(height);

    Buffer readback{
        m_graphics_device,
        Buffer_create_info{
            .capacity_byte_count                    = byte_count,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::transfer_dst | Buffer_usage::storage,
            .required_memory_property_bit_mask      =
                Memory_property_flag_bit_mask::host_read |
                Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     =
                Memory_property_flag_bit_mask::host_coherent |
                Memory_property_flag_bit_mask::host_persistent,
            .debug_label = erhe::utility::Debug_label{"lightmap readback"}
        }
    };
    constexpr unsigned int bake_thread_slot = 6;
    Command_buffer& command_buffer = m_graphics_device.get_command_buffer(bake_thread_slot);
    command_buffer.begin();
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::transfer_src_optimal);
    {
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(
            m_lightmap_texture.get(),
            0, 0,
            glm::ivec3{0, 0, 0},
            glm::ivec3{width, height, 1},
            &readback,
            0,
            bytes_per_row,
            byte_count
        );
    }
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::shader_read_only_optimal);
    command_buffer.end();
    Command_buffer* command_buffers[] = { &command_buffer };
    m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    m_graphics_device.wait_idle();

    std::vector<float> data(texel_count * 4u);
    {
        const std::span<std::byte> mapped = readback.map_bytes(0, byte_count);
        std::memcpy(data.data(), mapped.data(), byte_count);
        readback.unmap();
    }

    std::unique_ptr<Image_writer> writer = Image_writer::create();
    if (!writer || !writer->is_supported()) {
        return false;
    }
    // Reinhard + gamma for display.
    std::vector<std::uint8_t> pixels(texel_count * 4u);
    for (std::size_t i = 0; i < texel_count; ++i) {
        for (int c = 0; c < 3; ++c) {
            const float hdr = std::max(data[i * 4 + c], 0.0f);
            const float sdr = std::pow(hdr / (1.0f + hdr), 1.0f / 2.2f);
            pixels[i * 4 + c] = static_cast<std::uint8_t>(std::clamp(sdr, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
        pixels[i * 4 + 3] = (data[i * 4 + 3] > 0.0f) ? 255u : 0u;
    }
    return writer->write_png(
        std::filesystem::path{path},
        width,
        height,
        width * 4,
        erhe::dataformat::Format::format_8_vec4_unorm,
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(pixels.data()), pixels.size()}
    );
}

} // namespace editor
