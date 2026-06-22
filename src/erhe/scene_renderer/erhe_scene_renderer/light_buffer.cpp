// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"
#include "erhe_scene_renderer/shadow_renderer.hpp"
#include "erhe_scene_renderer/shader_key.hpp"
#include "erhe_renderer/renderer_config.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/transform.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <limits>

namespace erhe::scene_renderer {

Light_interface::Light_interface(erhe::graphics::Device& graphics_device, const int max_light_count)
    : max_light_count    {static_cast<std::size_t>(max_light_count)}
    , light_block        {graphics_device, "light_block",         light_buffer_binding_point,         erhe::graphics::Shader_resource::Type::uniform_block}
    , light_control_block{graphics_device, "light_control_block", light_control_buffer_binding_point, erhe::graphics::Shader_resource::Type::uniform_block}
    , light_struct       {graphics_device, "Light"}
    , offsets{
        .shadow_texture_compare    = light_block.add_uvec2("shadow_texture_compare"   )->get_offset_in_parent(),
        .shadow_texture_no_compare = light_block.add_uvec2("shadow_texture_no_compare")->get_offset_in_parent(),
        .directional_light_count   = light_block.add_uint ("directional_light_count"  )->get_offset_in_parent(),
        .spot_light_count          = light_block.add_uint ("spot_light_count"         )->get_offset_in_parent(),
        .point_light_count         = light_block.add_uint ("point_light_count"        )->get_offset_in_parent(),
        .directional_shadow_count  = light_block.add_uint ("directional_shadow_count" )->get_offset_in_parent(),
        .spot_shadow_count         = light_block.add_uint ("spot_shadow_count"        )->get_offset_in_parent(),
        .point_shadow_count        = light_block.add_uint ("point_shadow_count"       )->get_offset_in_parent(),
        .brdf_material             = light_block.add_uint ("brdf_material"            )->get_offset_in_parent(),
        .reserved_1                = light_block.add_uint ("reserved_1"               )->get_offset_in_parent(),
        .brdf_phi_incident_phi     = light_block.add_vec2 ("brdf_phi_incident_phi"    )->get_offset_in_parent(),
        .ambient_light             = light_block.add_vec4 ("ambient_light"            )->get_offset_in_parent(),

        .light = {
            .clip_from_world              = light_struct.add_mat4 ("clip_from_world"             )->get_offset_in_parent(),
            .texture_from_world           = light_struct.add_mat4 ("texture_from_world"          )->get_offset_in_parent(),
            .world_from_texture           = light_struct.add_mat4 ("world_from_texture"          )->get_offset_in_parent(),
            .position_and_inner_spot_cos  = light_struct.add_vec4 ("position_and_inner_spot_cos" )->get_offset_in_parent(),
            .direction_and_outer_spot_cos = light_struct.add_vec4 ("direction_and_outer_spot_cos")->get_offset_in_parent(),
            .radiance_and_range           = light_struct.add_vec4 ("radiance_and_range"          )->get_offset_in_parent(),
            .shadow_index_packed          = light_struct.add_uvec4("shadow_index_packed"         )->get_offset_in_parent(),
        },
        .light_struct = light_block.add_struct("lights", &light_struct, max_light_count)->get_offset_in_parent()
    }
    , light_index_offset{
        light_control_block.add_uint("light_index")->get_offset_in_parent()
    }
    , shadow_distance_bias_coeff_offset{
        light_control_block.add_float("shadow_distance_bias_coeff")->get_offset_in_parent()
    }
    , point_light_position_offset{
        light_control_block.add_vec4("point_light_position")->get_offset_in_parent()
    }
    , shadow_sampler_compare{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter        = erhe::graphics::Filter::nearest,
            .mag_filter        = erhe::graphics::Filter::nearest,
            .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .compare_enable    = true,
            // Comparison op is baked here from the engine-wide reverse_depth
            // setting because the descriptor set layout pins this sampler as
            // an immutable sampler -- the Vulkan portability subset on
            // MoltenVK refuses comparison samplers via push descriptors, so
            // direction has to be fixed at engine init.
            .compare_operation = graphics_device.get_reverse_depth()
                ? erhe::graphics::Compare_operation::greater_or_equal
                : erhe::graphics::Compare_operation::less_or_equal,
            .lod_bias     = 0.0f,
            .max_lod      = 0.0f,
            .min_lod      = 0.0f,
            .debug_label  = "Light_interface::shadow_sampler_compare"
        }
    }
    , shadow_sampler_no_compare{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter     = erhe::graphics::Filter::nearest,
            .mag_filter     = erhe::graphics::Filter::nearest,
            .mipmap_mode    = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .compare_enable = false,
            .lod_bias       = 0.0f,
            .max_lod        = 0.0f,
            .min_lod        = 0.0f,
            .debug_label    = "Light_interface::shadow_sampler_no_compare"
        }
    }
{
}

auto Light_interface::get_sampler(const bool compare) const -> const erhe::graphics::Sampler*
{
    return compare ? &shadow_sampler_compare : &shadow_sampler_no_compare;
}

Light_buffer::Light_buffer(
    erhe::graphics::Device&         graphics_device,
    erhe::graphics::Command_buffer& init_command_buffer,
    Light_interface&                light_interface
)
    : m_graphics_device{graphics_device}
    , m_light_interface{light_interface}
    , m_light_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::uniform,
        "Light_buffer::m_light_buffer",
        light_interface.light_block.get_binding_point()
    }
    , m_control_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::uniform,
        "Light_buffer::m_control_buffer",
        light_interface.light_control_block.get_binding_point()
    }
    , m_fallback_shadow_texture{
        std::make_shared<erhe::graphics::Texture>(
            graphics_device,
            erhe::graphics::Texture_create_info {
                .device            = graphics_device,
                .usage_mask        = erhe::graphics::Image_usage_flag_bit_mask::sampled | erhe::graphics::Image_usage_flag_bit_mask::transfer_dst | erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment,
                .type              = erhe::graphics::Texture_type::texture_2d_array,
                .pixelformat       = graphics_device.choose_depth_stencil_format(erhe::graphics::format_flag_require_depth, 0),
                .width             = 1,
                .height            = 1,
                .depth             = 1,
                .array_layer_count = 1,
                .debug_label       = "Light_buffer::m_fallback_shadow_texture"
            }
        )
    }
    , m_fallback_distance_texture{
        std::make_shared<erhe::graphics::Texture>(
            graphics_device,
            erhe::graphics::Texture_create_info {
                .device            = graphics_device,
                .usage_mask        = erhe::graphics::Image_usage_flag_bit_mask::sampled | erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
                .type              = erhe::graphics::Texture_type::texture_2d_array,
                .pixelformat       = erhe::dataformat::Format::format_32_scalar_float,
                .width             = 1,
                .height            = 1,
                .depth             = 1,
                .array_layer_count = 1,
                .debug_label       = "Light_buffer::m_fallback_distance_texture"
            }
        )
    }
    , m_fallback_point_cube_texture{
        std::make_shared<erhe::graphics::Texture>(
            graphics_device,
            erhe::graphics::Texture_create_info {
                .device            = graphics_device,
                .usage_mask        = erhe::graphics::Image_usage_flag_bit_mask::sampled | erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
                .type              = erhe::graphics::Texture_type::texture_cube_map_array,
                .pixelformat       = erhe::dataformat::Format::format_32_scalar_float,
                .width             = 1,
                .height            = 1,
                .depth             = 1,
                .array_layer_count = 6, // one cube (6 faces)
                .debug_label       = "Light_buffer::m_fallback_point_cube_texture"
            }
        )
    }
{
    // Initialize fallback shadow texture contents (clear to far-plane depth) and
    // leave it in DEPTH_STENCIL_READ_ONLY_OPTIMAL. A bare UNDEFINED -> READ_ONLY
    // transition would discard contents and is flagged by the validation layer
    // as a best-practices error.
    init_command_buffer.clear_texture(*m_fallback_shadow_texture.get(), {1.0, 0.0, 0.0, 0.0});
    // Same for the distance-map fallback (color R32F): clear to a far value so a
    // stray sample resolves to "lit". The shader's max_u32 sentinel normally
    // short-circuits before sampling, so the exact value only matters defensively.
    init_command_buffer.clear_texture(*m_fallback_distance_texture.get(), {1.0, 0.0, 0.0, 0.0});
    // Point-shadow cube fallback (R32F radial distance): clear to a large
    // distance so any sample reads "occluder very far away" => lit. Bound
    // whenever no point shadow cube array is active.
    init_command_buffer.clear_texture(*m_fallback_point_cube_texture.get(), {1.0e30, 0.0, 0.0, 0.0});
}

void Light_projections::apply(
    const std::span<const std::shared_ptr<erhe::scene::Light>>& lights,
    const erhe::scene::Camera*                                  view_camera,
    const erhe::math::Viewport&                                 view_camera_viewport,
    const erhe::math::Viewport&                                 light_texture_viewport,
    const std::shared_ptr<erhe::graphics::Texture>&             in_shadow_map_texture,
    const bool                                                  reverse_depth,
    const erhe::math::Depth_range                               depth_range,
    const erhe::math::Coordinate_conventions&                   conventions,
    const std::span<const erhe::math::Aabb>                     in_caster_world_aabbs,
    const std::span<const erhe::math::Aabb>                     in_receiver_world_aabbs,
    const erhe::scene::Shadow_frustum_fit_settings*             fit_settings
)
{
    ERHE_PROFILE_FUNCTION();
    // The AABB spans point directly at caller storage. They are consumed only
    // by the per-light fit calls in the loop below and reset right after it,
    // so the member parameters (which outlives this call) never holds dangling
    // spans and no per-frame copy of the AABBs is needed.
    // Invalidate (not clear - buffers keep capacity) the cross-light receiver
    // cache; the first tight directional fit of this pass refills it.
    fit_receiver_cache.valid = false;
    parameters = erhe::scene::Light_projection_parameters{
        .view_camera          = view_camera,
        .main_camera_viewport = view_camera_viewport,
        .shadow_map_viewport  = light_texture_viewport,
        .reverse_depth        = reverse_depth,
        .depth_range          = depth_range,
        .conventions          = conventions,
        .fit_settings         = fit_settings,
        .caster_world_aabbs   = in_caster_world_aabbs,
        .receiver_world_aabbs = in_receiver_world_aabbs,
        .receiver_cache       = &fit_receiver_cache,
        .fit_scratch          = &fit_scratch
    };
    shadow_map_texture = in_shadow_map_texture;

    light_projection_transforms.clear();
    light_projection_transforms.reserve(lights.size());

    const bool collect_fit_debug = (fit_settings != nullptr) && fit_settings->collect_debug;
    fit_debug_data.clear();
    if (collect_fit_debug) {
        fit_debug_data.resize(lights.size());
    }

    // Build the projection transforms in the input order first; the actual
    // UBO slot index is assigned in a second pass below so the lights can
    // be partitioned per-type with shadow-mapped lights occupying the prefix
    // of each per-type sub-array. The fragment shader uses this invariant
    // to split each per-type lighting loop into a shadow-sampling prefix
    // and a direct-lighting suffix, with the bounds becoming compile-time
    // constants under the variant cache.
    for (std::size_t i = 0, end = lights.size(); i < end; ++i) {
        const std::shared_ptr<erhe::scene::Light>& light = lights[i];
        parameters.fit_debug_out = collect_fit_debug ? &fit_debug_data[i] : nullptr;
        light_projection_transforms.push_back(light->projection_transforms(parameters));
    }
    parameters.fit_debug_out        = nullptr;
    parameters.caster_world_aabbs   = {};
    parameters.receiver_world_aabbs = {};

    auto type_index_of = [](erhe::scene::Light_type type) -> std::size_t {
        switch (type) {
            case erhe::scene::Light_type::directional: return 0;
            case erhe::scene::Light_type::spot:        return 1;
            case erhe::scene::Light_type::point:       return 2;
            default:                                   return 3;
        }
    };

    // Pass 1: count, by (type_bucket, shadow_bucket). The tally lives in
    // compute_light_layer_partition (shader_key.hpp) so the variant-cache
    // counts and the UBO slot assignment below share one source.
    const Light_layer_partition partition = compute_light_layer_partition(lights);
    const std::size_t (&per_type_shadow)   [4] = partition.per_type_shadow;
    const std::size_t (&per_type_nonshadow)[4] = partition.per_type_nonshadow;

    // Compute base slots: type major (directional, spot, point, other),
    // shadow minor (shadow-casters, then non-shadow-casters within each type).
    std::size_t base_shadow   [4] = {0, 0, 0, 0};
    std::size_t base_nonshadow[4] = {0, 0, 0, 0};
    // base_shadow_dense[t] = number of shadow-casting lights of types
    // 0..t-1, which is the type-major start of the dense shadow-only
    // array consumed by Shadow_renderer. Unlike base_shadow[t], it
    // excludes the non-shadow lights of each preceding type and so
    // never over-counts past parameters.render_passes.size().
    std::size_t base_shadow_dense[4] = {0, 0, 0, 0};
    {
        std::size_t cursor       = 0;
        std::size_t dense_cursor = 0;
        for (std::size_t t = 0; t < 4; ++t) {
            base_shadow      [t] = cursor;
            cursor              += per_type_shadow[t];
            base_nonshadow   [t] = cursor;
            cursor              += per_type_nonshadow[t];

            base_shadow_dense[t] = dense_cursor;
            dense_cursor        += per_type_shadow[t];
        }
    }

    // Pass 2: walk the input order again and assign each light a UBO slot
    // inside its (type, shadow) bucket. shadow_index is computed in a
    // separate dense space that counts only shadow-casting lights, so
    // it can index the Shadow_renderer's render_passes array (sized to
    // the total shadow-light cap) without including non-shadow lights.
    //
    // Point lights are the exception: they cast omnidirectional shadows into a
    // separate R32F cube-map array, NOT the 2D shadow array, so their
    // shadow_index is left at max() (which makes the 2D shadow loop skip them
    // via the render_passes.size() gate) and they instead receive a dense
    // point_shadow_index counting only shadow-casting point lights. Because
    // point is the last shadow-bearing type bucket (after directional and
    // spot), excluding point lights from the 2D dense space does not shift the
    // directional / spot layer indices.
    std::size_t cursor_shadow   [4] = {0, 0, 0, 0};
    std::size_t cursor_nonshadow[4] = {0, 0, 0, 0};
    std::size_t point_shadow_cursor = 0;
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const std::shared_ptr<erhe::scene::Light>& light = lights[i];
        const std::size_t t        = type_index_of(light->type);
        const bool        is_point = (light->type == erhe::scene::Light_type::point);
        std::size_t slot              = std::numeric_limits<std::size_t>::max();
        std::size_t shadow_slot       = std::numeric_limits<std::size_t>::max();
        std::size_t point_shadow_slot = std::numeric_limits<std::size_t>::max();
        if (!light->is_active()) {
            // Inactive light (e.g. a zero-range point light, which reaches
            // nowhere): assign no slot and advance no cursor, so the dense
            // per-type arrays skip it entirely. All three indices stay max():
            // the light buffer write skips it (index >= max_light_count) and the
            // shadow loops skip it, so it contributes neither light nor shadow.
            // This matches compute_light_layer_partition, which counts inactive
            // lights in neither the shadow nor the non-shadow bucket.
        } else if (light->cast_shadow) {
            slot = base_shadow[t] + cursor_shadow[t];
            if (is_point) {
                // Cube array gets its own dense index; shadow_slot stays max()
                // so the 2D shadow pass / receiver 2D sampling skip this light.
                point_shadow_slot = point_shadow_cursor;
                ++point_shadow_cursor;
            } else {
                shadow_slot = base_shadow_dense[t] + cursor_shadow[t];
            }
            ++cursor_shadow[t];
        } else {
            slot = base_nonshadow[t] + cursor_nonshadow[t];
            // Non-shadow lights still get a deterministic shadow_index
            // (any value works since Shadow_renderer skips them via
            // !light->casts_shadow()), but using max() makes any stray
            // out-of-bound use loudly fail the render_passes.size() gate.
            ++cursor_nonshadow[t];
        }
        light_projection_transforms[i].index              = slot;
        light_projection_transforms[i].shadow_index       = shadow_slot;
        light_projection_transforms[i].point_shadow_index = point_shadow_slot;
    }

    SPDLOG_LOGGER_TRACE(
        log_render,
        "light_projection_transforms.size() = {}",
        light_projection_transforms.size()
    );
}

auto Light_projections::get_light_projection_transforms_for_light(const erhe::scene::Light* light) -> erhe::scene::Light_projection_transforms*
{
    for (auto& i : light_projection_transforms) {
        if (i.light == light) {
            return &i;
        }
    }
    return nullptr;
}

auto Light_projections::get_light_projection_transforms_for_light(const erhe::scene::Light* light) const -> const erhe::scene::Light_projection_transforms*
{
    for (auto& i : light_projection_transforms) {
        if (i.light == light) {
            return &i;
        }
    }
    return nullptr;
}

auto Light_buffer::update(
    const std::span<const std::shared_ptr<erhe::scene::Light>>& lights,
    const Light_projections*                                    light_projections,
    const glm::vec3&                                            ambient_light
) -> erhe::graphics::Ring_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    SPDLOG_LOGGER_TRACE(log_render, "lights.size() = {}, m_light_buffer.writer().write_offset = {}", lights.size(), m_light_buffer.writer().write_offset);

    const auto                   light_struct_size = m_light_interface.light_struct.get_size_bytes();
    const auto&                  offsets           = m_light_interface.offsets;

    // NOTE: As long as we are using uniform buffer, always fill in data max lights
    // (m_light_interface.max_light_count) instea of lights.size(), and pad the
    // unused part of the array with zeros.
    const size_t                      exact_byte_count  = offsets.light_struct + m_light_interface.max_light_count * light_struct_size;
    erhe::graphics::Ring_buffer_range buffer_range      = m_light_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, exact_byte_count);
    std::span<std::byte>              light_gpu_data    = buffer_range.get_span();
    ERHE_VERIFY(light_gpu_data.size_bytes() >= exact_byte_count);
    uint32_t       directional_light_count {0u};
    uint32_t       spot_light_count        {0u};
    uint32_t       point_light_count       {0u};
    uint32_t       directional_shadow_count{0u};
    uint32_t       spot_shadow_count       {0u};
    uint32_t       point_shadow_count      {0u};
    const uint32_t uint_zero               {0u};
    const uint32_t uvec4_zero[4]           {0u, 0u, 0u, 0u};

    const erhe::graphics::Sampler* compare_sampler    = m_light_interface.get_sampler(true);
    const erhe::graphics::Sampler* no_compare_sampler = m_light_interface.get_sampler(false);

    uint64_t shadow_map_texture_handle_compare    = erhe::graphics::invalid_texture_handle;
    uint64_t shadow_map_texture_handle_no_compare = erhe::graphics::invalid_texture_handle;
    erhe::graphics::Texture* shadow_map_texture = light_projections ? light_projections->shadow_map_texture.get() : nullptr;
    if (shadow_map_texture == nullptr) {
        shadow_map_texture = m_fallback_shadow_texture.get();
    }
    if (shadow_map_texture != nullptr) {
        // The handles written into the UBO are only used as a "shadow
        // texture present" sentinel on all backends: the shader reads
        // shadow_texture_compare.x and treats max_u32 as "no shadow map".
        // Actual shadow sampling goes through the named s_shadow_compare /
        // s_shadow_no_compare uniform bindings declared in the default
        // uniform block, which bind_shadow_samplers() below binds via
        // set_sampled_image() for every backend. A zero handle stub from
        // backends without GL_ARB_bindless_texture is fine for the sentinel
        // check.
        shadow_map_texture_handle_compare    = m_graphics_device.get_handle(*shadow_map_texture, *compare_sampler);
        shadow_map_texture_handle_no_compare = m_graphics_device.get_handle(*shadow_map_texture, *no_compare_sampler);
        // log_render->trace(
        //     "Shadow texture assigned: texture='{}' handle_compare={} handle_no_compare={}",
        //     shadow_map_texture->get_debug_label().data(),
        //     shadow_map_texture_handle_compare,
        //     shadow_map_texture_handle_no_compare
        // );
        // } else {
        //     log_render->warn("Shadow map texture is null - no shadow texture assigned");
    }

    using erhe::graphics::as_span;
    using erhe::graphics::write;

    std::size_t write_offset = 0;
    const std::size_t common_offset = write_offset;

    write_offset += offsets.light_struct;
    const std::size_t light_array_offset = write_offset;
    std::size_t max_light_index{0};

    for (const auto& light : lights) {
        ERHE_VERIFY(light);
        erhe::scene::Node* node = light->get_node();
        ERHE_VERIFY(node != nullptr);

        // Inactive lights (e.g. a zero-range point light, which reaches nowhere)
        // contribute nothing: they were assigned no slot in Light_projections::
        // apply(), so they must not be counted here nor written to the buffer.
        if (!light->is_active()) {
            continue;
        }

        switch (light->type) {
            case erhe::scene::Light_type::directional:
                ++directional_light_count;
                if (light->cast_shadow) {
                    ++directional_shadow_count;
                }
                break;
            case erhe::scene::Light_type::point:
                ++point_light_count;
                if (light->cast_shadow) {
                    ++point_shadow_count;
                }
                break;
            case erhe::scene::Light_type::spot:
                ++spot_light_count;
                if (light->cast_shadow) {
                    ++spot_shadow_count;
                }
                break;
            default: break;
        }

        using vec2 = glm::vec2;
        using vec3 = glm::vec3;
        using vec4 = glm::vec4;
        using mat4 = glm::mat4;
        auto* light_projection_transforms = (light_projections != nullptr)
            ? light_projections->get_light_projection_transforms_for_light(light.get())
            : nullptr;
        if (light_projection_transforms == nullptr) {
            continue;
        }

        const mat4 texture_from_world   = light_projection_transforms->texture_from_world.get_matrix();
        const mat4 world_from_texture   = light_projection_transforms->texture_from_world.get_inverse_matrix();
        const vec3 direction            = vec3{node->world_from_node() * vec4{0.0f, 0.0f, 1.0f, 0.0f}};
        const vec3 position             = vec3{light_projection_transforms->world_from_light_camera.get_matrix() * vec4{0.0f, 0.0f, 0.0f, 1.0f}};
        const vec4 radiance             = vec4{light->intensity * light->color, light->range};
        const auto inner_spot_cos       = std::cos(light->inner_spot_angle * 0.5f);
        const auto outer_spot_cos       = std::cos(light->outer_spot_angle * 0.5f);
        const vec4 position_inner_spot  = vec4{position, inner_spot_cos};
        const vec4 direction_outer_spot = vec4{glm::normalize(vec3{direction}), outer_spot_cos};
        const auto light_index          = light_projection_transforms->index;
        const auto light_offset         = light_array_offset + light_index * light_struct_size;

        if (light_index >= m_light_interface.max_light_count) {
            continue;
        }

        ERHE_VERIFY(light_offset < exact_byte_count);
        max_light_index = std::max(max_light_index, light_index);
        write(light_gpu_data, light_offset + offsets.light.clip_from_world,              as_span(light_projection_transforms->clip_from_world.get_matrix()));
        write(light_gpu_data, light_offset + offsets.light.texture_from_world,           as_span(texture_from_world));
        write(light_gpu_data, light_offset + offsets.light.world_from_texture,           as_span(world_from_texture));
        write(light_gpu_data, light_offset + offsets.light.position_and_inner_spot_cos,  as_span(position_inner_spot));
        write(light_gpu_data, light_offset + offsets.light.direction_and_outer_spot_cos, as_span(direction_outer_spot));
        write(light_gpu_data, light_offset + offsets.light.radiance_and_range,           as_span(radiance));

        // Shadow_renderer writes its shadow map into render_passes[shadow_index];
        // the fragment shader reads the same value as the shadow texture
        // array_layer. Non-shadow lights get max() which the shader can
        // detect or which falls out of any reasonable layer range.
        const uint32_t shadow_index_u32 = (light_projection_transforms->shadow_index <= std::numeric_limits<uint32_t>::max())
            ? static_cast<uint32_t>(light_projection_transforms->shadow_index)
            : std::numeric_limits<uint32_t>::max();
        // packed.y = dense cube-array index for omnidirectional point-light
        // shadows (max() for non-point / non-shadow lights). The forward shader
        // reads it as the samplerCubeArray layer base for this light.
        const uint32_t point_shadow_index_u32 = (light_projection_transforms->point_shadow_index <= std::numeric_limits<uint32_t>::max())
            ? static_cast<uint32_t>(light_projection_transforms->point_shadow_index)
            : std::numeric_limits<uint32_t>::max();
        const uint32_t shadow_index_packed[4] = { shadow_index_u32, point_shadow_index_u32, 0u, 0u };
        write(light_gpu_data, light_offset + offsets.light.shadow_index_packed, as_span(shadow_index_packed));
    }
    // Fill in unused part of the array
    size_t padding_light_offset = (max_light_index + 1);
    size_t padding_light_count = m_light_interface.max_light_count - padding_light_offset;
    memset(
        light_gpu_data.data() + light_array_offset + padding_light_offset * light_struct_size,
        0,
        padding_light_count * light_struct_size
    );
    write_offset += m_light_interface.max_light_count * light_struct_size;

    const glm::vec2 brdf_phi_incident_phi = (light_projections != nullptr) ? glm::vec2{light_projections->brdf_phi, light_projections->brdf_incident_phi} : glm::vec2{0.0f, 0.0f};
    const uint32_t  brdf_material         = (light_projections != nullptr) ? (light_projections->brdf_material ? light_projections->brdf_material->material_buffer_index : 0) : 0;

    // Late write to begin of buffer to full in light counts
    write(light_gpu_data, common_offset + offsets.shadow_texture_compare,    as_span(shadow_map_texture_handle_compare));
    write(light_gpu_data, common_offset + offsets.shadow_texture_no_compare, as_span(shadow_map_texture_handle_no_compare));

    write(light_gpu_data, common_offset + offsets.directional_light_count,   as_span(directional_light_count) );
    write(light_gpu_data, common_offset + offsets.spot_light_count,          as_span(spot_light_count)        );
    write(light_gpu_data, common_offset + offsets.point_light_count,         as_span(point_light_count)       );
    write(light_gpu_data, common_offset + offsets.directional_shadow_count,  as_span(directional_shadow_count));

    write(light_gpu_data, common_offset + offsets.spot_shadow_count,         as_span(spot_shadow_count)       );
    write(light_gpu_data, common_offset + offsets.point_shadow_count,        as_span(point_shadow_count)      );
    write(light_gpu_data, common_offset + offsets.brdf_material,             as_span(brdf_material)           );
    write(light_gpu_data, common_offset + offsets.reserved_1,                as_span(uint_zero)               );

    write(light_gpu_data, common_offset + offsets.brdf_phi_incident_phi,     as_span(brdf_phi_incident_phi)   );

    write(light_gpu_data, common_offset + offsets.ambient_light,             as_span(ambient_light)          );

    buffer_range.bytes_written(write_offset);
    buffer_range.close();
    SPDLOG_LOGGER_TRACE(log_draw, "wrote up to {} entries to light buffer", padding_light_offset);

    return buffer_range;
}

void Light_buffer::bind_shadow_samplers(
    erhe::graphics::Render_command_encoder& encoder,
    const Light_projections*                light_projections
)
{
    // Bind the shadow map to the named sampler bindings declared in the
    // bind group layout (s_shadow_compare and s_shadow_no_compare). The
    // depth aspect comes from the layout binding's Sampler_aspect::depth
    // annotation set in program_interface.cpp; the encoder's
    // set_sampled_image() implementation reads it and creates a
    // depth-aspect VkImageView. The fallback texture (always created in
    // the constructor) ensures we always have a valid texture to bind.
    erhe::graphics::Texture* shadow_map_texture = (light_projections != nullptr)
        ? light_projections->shadow_map_texture.get()
        : nullptr;
    if (shadow_map_texture == nullptr) {
        shadow_map_texture = m_fallback_shadow_texture.get();
    }
    if (shadow_map_texture == nullptr) {
        return;
    }
    const erhe::graphics::Sampler* compare_sampler    = m_light_interface.get_sampler(true);
    const erhe::graphics::Sampler* no_compare_sampler = m_light_interface.get_sampler(false);
    // Bind shadow textures to the uniform sampler declarations in the
    // default uniform block. All backends use set_sampled_image() for
    // dedicated (non-texture-heap) samplers.
    encoder.set_sampled_image(c_texture_heap_slot_shadow_compare,    *shadow_map_texture, *compare_sampler);
    encoder.set_sampled_image(c_texture_heap_slot_shadow_no_compare, *shadow_map_texture, *no_compare_sampler);

    // Distance map (Shadow_technique_mode::distance) -> s_shadow_distance. Falls
    // back to the 1x1 R32F texture when no distance map is active (depth
    // technique) so the color-aspect binding always has a valid texture.
    erhe::graphics::Texture* shadow_distance_texture = (light_projections != nullptr)
        ? light_projections->shadow_distance_texture.get()
        : nullptr;
    if (shadow_distance_texture == nullptr) {
        shadow_distance_texture = m_fallback_distance_texture.get();
    }
    encoder.set_sampled_image(c_texture_heap_slot_shadow_distance, *shadow_distance_texture, *no_compare_sampler);

    // Point-light shadow cube array (R32F radial distance), sampled by direction
    // through s_shadow_cube. Falls back to the 1x1 cube array when no point
    // shadows are configured so the samplerCubeArray binding always has a valid
    // texture.
    erhe::graphics::Texture* shadow_cube_texture = (light_projections != nullptr)
        ? light_projections->shadow_cube_texture.get()
        : nullptr;
    if (shadow_cube_texture == nullptr) {
        shadow_cube_texture = m_fallback_point_cube_texture.get();
    }
    encoder.set_sampled_image(c_texture_heap_slot_shadow_cube, *shadow_cube_texture, *no_compare_sampler);
}

auto Light_buffer::update_control(const std::size_t light_index, const float shadow_distance_bias_coeff, const glm::vec4& point_light_position) -> erhe::graphics::Ring_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    const auto                        entry_size   = m_light_interface.light_control_block.get_size_bytes();
    erhe::graphics::Ring_buffer_range buffer_range = m_control_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, entry_size);
    const auto                        gpu_data     = buffer_range.get_span();
    size_t                            write_offset = 0;

    using erhe::graphics::as_span;
    using erhe::graphics::write;

    const auto uint_light_index = static_cast<uint32_t>(light_index);
    write(gpu_data, m_light_interface.light_index_offset,                as_span(uint_light_index));
    write(gpu_data, m_light_interface.shadow_distance_bias_coeff_offset, as_span(shadow_distance_bias_coeff));
    write(gpu_data, m_light_interface.point_light_position_offset,       as_span(point_light_position));
    write_offset += entry_size;

    buffer_range.bytes_written(write_offset);
    buffer_range.close();

    return buffer_range;
}

void Light_buffer::bind_light_buffer(erhe::graphics::Command_encoder& encoder, const erhe::graphics::Ring_buffer_range& range)
{
    m_light_buffer.bind(encoder, range);
}

void Light_buffer::bind_control_buffer(erhe::graphics::Command_encoder& encoder, const erhe::graphics::Ring_buffer_range& range)
{
    m_control_buffer.bind(encoder, range);
}

} // namespace erhe::scene_renderer
