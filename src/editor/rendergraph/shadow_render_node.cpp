#include "rendergraph/shadow_render_node.hpp"

#include "app_context.hpp"
#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/shadow_frustum_fit_config.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_settings_resolve.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_view.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene_renderer/shadow_renderer.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace editor {

using erhe::graphics::Render_pass;
using erhe::graphics::Texture;

Shadow_render_node::Shadow_render_node(
    erhe::graphics::Device&         graphics_device,
    erhe::graphics::Command_buffer& init_command_buffer,
    erhe::rendergraph::Rendergraph& rendergraph,
    App_context&                    context,
    Scene_view&                     scene_view,
    const int                       resolution,
    const int                       light_count,
    const int                       depth_bits,
    const int                       point_resolution,
    const int                       point_light_count
)
    // TODO fmt::format("Shadow render {}", viewport_scene_view->name())
    : erhe::rendergraph::Rendergraph_node{rendergraph, "shadow_maps"}
    , m_context   {context}
    , m_scene_view{scene_view}
{
    register_output("shadow_maps", erhe::rendergraph::Rendergraph_node_key::shadow_maps);

    // Start on the depth technique; execute_rendergraph_node lazily switches to
    // distance on the first frame if the active preset selects it (the App_context
    // settings are not available to read here at construction time).
    reconfigure(graphics_device, init_command_buffer, resolution, light_count, depth_bits, false, point_resolution, point_light_count);
}

Shadow_render_node::~Shadow_render_node() noexcept
{
}

void Shadow_render_node::reconfigure(erhe::graphics::Device& graphics_device, erhe::graphics::Command_buffer& command_buffer, const int resolution, const int light_count, const int requested_depth_bits, const bool distance_technique, const int point_resolution, const int point_light_count)
{
    // Reverse-Z is the static device value; query it rather than caching.
    const bool reverse_depth = graphics_device.get_reverse_depth();
    std::vector<erhe::dataformat::Format> formats = graphics_device.get_supported_depth_stencil_formats();
    std::stable_sort(
        formats.begin(),
        formats.end(),
        [&](const erhe::dataformat::Format& lhs, const erhe::dataformat::Format& rhs)
        {
            const size_t lhs_format_size_bytes = erhe::dataformat::get_format_size_bytes(lhs);
            const size_t rhs_format_size_bytes = erhe::dataformat::get_format_size_bytes(rhs);
            const int    lhs_depth_size_bits   = static_cast<int>(erhe::dataformat::get_depth_size_bits(lhs));
            const int    rhs_depth_size_bits   = static_cast<int>(erhe::dataformat::get_depth_size_bits(rhs));
            const int    lhs_depth_diff        = lhs_depth_size_bits - requested_depth_bits;
            const int    rhs_depth_diff        = rhs_depth_size_bits - requested_depth_bits;
            const int    lhs_abs_diff          = std::abs(lhs_depth_diff);
            const int    rhs_abs_diff          = std::abs(rhs_depth_diff);
            bool result;
            if (lhs_depth_size_bits == rhs_depth_size_bits) {
                return lhs_format_size_bytes < rhs_format_size_bytes;
            } else if ((lhs_depth_diff > 0) && (rhs_depth_diff < 0)) {
                result = true;
            } else if ((rhs_depth_diff > 0) && (lhs_depth_diff < 0)) {
                result = false;
            } else if (lhs_depth_size_bits == 0) {
                result = false;
            } else if (rhs_depth_size_bits == 0) {
                result = true;
            } else {
                result = (lhs_abs_diff < rhs_abs_diff);
            }
            return result;
        }
    );
    const erhe::dataformat::Format depth_format = graphics_device.choose_depth_stencil_format(formats);
    if (depth_format == erhe::dataformat::Format::format_undefined) {
        log_render->error(
            "Reconfigure shadow resolution = {}, light count = {}, no depth format found, skip",
            resolution, light_count
        );
        m_texture.reset();
        return;
    }

    if (
        m_texture &&
        (m_texture->get_width()             == resolution) &&
        (m_texture->get_height()            == resolution) &&
        (m_texture->get_array_layer_count() == std::max(1, light_count)) &&
        (m_texture->get_pixelformat()       == depth_format) &&
        (m_distance_technique               == distance_technique) &&
        (m_point_resolution                 == point_resolution) &&
        (m_point_light_count                == point_light_count)
    ) {
        log_render->debug(
            "Reconfigure shadow resolution = {}, light count = {}, format = {} - match old settings, skip",
            resolution, light_count, erhe::dataformat::c_str(depth_format)
        );
        return;
    }
    log_render->debug("Reconfigure shadow resolution = {}, light count = {}", resolution, light_count);

    // Cache the resolved configuration so execute_rendergraph_node can re-call
    // reconfigure() with the same dimensions when only the technique changes.
    m_resolution         = resolution;
    m_light_count        = light_count;
    m_depth_bits         = requested_depth_bits;
    m_distance_technique = distance_technique;
    m_point_resolution   = point_resolution;
    m_point_light_count  = point_light_count;

    //// TODO device.wait_for_idle()

    {
        ERHE_PROFILE_SCOPE("allocating shadow map array texture");

        m_texture.reset();
        m_texture = std::make_shared<Texture>(
            graphics_device,
            erhe::graphics::Texture_create_info {
                .device            = graphics_device,
                .usage_mask        =
                    erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment |
                    erhe::graphics::Image_usage_flag_bit_mask::sampled |
                    erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
                .type              = erhe::graphics::Texture_type::texture_2d_array,
                .pixelformat       = depth_format,
                .width             = std::max(1, resolution),
                .height            = std::max(1, resolution),
                .depth             = 1,
                .array_layer_count = std::max(1, light_count),
                .debug_label       = "Shadowmap"
            }
        );

        // Transition all array layers from UNDEFINED to depth_stencil_read_only_optimal.
        // Render passes only transition the layers they target -- without this, layers
        // that no light renders into stay in UNDEFINED, and binding the whole array as
        // a sampled descriptor (Vulkan validation VUID-vkCmdDraw-None-09600) trips on
        // them.
        command_buffer.transition_texture_layout(*m_texture.get(), erhe::graphics::Image_layout::depth_stencil_read_only_optimal);
    }

    // Distance technique (Shadow_technique_mode::distance): a parallel R32F color
    // array the caster writes the fwidth-biased light-space depth into, sampled
    // by the receiver. Allocated only while the technique is active -- a
    // full-resolution R32F array is large, so it must not exist for the depth
    // technique.
    m_distance_texture.reset();
    if (distance_technique) {
        ERHE_PROFILE_SCOPE("allocating shadow distance map array texture");
        m_distance_texture = std::make_shared<Texture>(
            graphics_device,
            erhe::graphics::Texture_create_info {
                .device            = graphics_device,
                .usage_mask        =
                    erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                    erhe::graphics::Image_usage_flag_bit_mask::sampled |
                    erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
                .type              = erhe::graphics::Texture_type::texture_2d_array,
                .pixelformat       = erhe::dataformat::Format::format_32_scalar_float,
                .width             = std::max(1, resolution),
                .height            = std::max(1, resolution),
                .depth             = 1,
                .array_layer_count = std::max(1, light_count),
                .debug_label       = "Shadow distance map"
            }
        );
        command_buffer.transition_texture_layout(*m_distance_texture.get(), erhe::graphics::Image_layout::shader_read_only_optimal);
    }

    log_render->debug("updating render passes, light_count = {}", light_count);
    // Order matters: each Gpu_timer is registered with its Render_pass and
    // must be destroyed before that Render_pass.
    m_gpu_timers.clear();
    m_gpu_timer_labels.clear();
    m_render_passes.clear();
    m_gpu_timer_labels.reserve(static_cast<std::size_t>(light_count));
    m_gpu_timers      .reserve(static_cast<std::size_t>(light_count));
    for (int i = 0; i < light_count; ++i) {
        erhe::graphics::Render_pass_descriptor render_pass_descriptor;
        render_pass_descriptor.depth_attachment.texture        = m_texture.get();
        render_pass_descriptor.depth_attachment.texture_level  = 0;
        render_pass_descriptor.depth_attachment.texture_layer  = static_cast<unsigned int>(i);
        render_pass_descriptor.depth_attachment.load_action    = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.depth_attachment.store_action   = erhe::graphics::Store_action::Store;
        render_pass_descriptor.depth_attachment.usage_before   = erhe::graphics::Image_usage_flag_bit_mask::sampled;
        render_pass_descriptor.depth_attachment.layout_before  = erhe::graphics::Image_layout::depth_stencil_read_only_optimal;
        render_pass_descriptor.depth_attachment.usage_after    = erhe::graphics::Image_usage_flag_bit_mask::sampled;
        render_pass_descriptor.depth_attachment.layout_after   = erhe::graphics::Image_layout::depth_stencil_read_only_optimal;
        render_pass_descriptor.depth_attachment.clear_value[0] = reverse_depth ? 0.0 : 1.0;
        if (distance_technique && m_distance_texture) {
            // Distance map color attachment: the caster writes the fwidth-biased
            // light-space depth here. Cleared to the far value so empty texels
            // resolve to "lit" under the receiver's non-strict compare, matching
            // the depth attachment. Kept in shader_read_only between passes
            // (sampled by the forward pass).
            render_pass_descriptor.color_attachments[0].texture       = m_distance_texture.get();
            render_pass_descriptor.color_attachments[0].texture_level = 0;
            render_pass_descriptor.color_attachments[0].texture_layer = static_cast<unsigned int>(i);
            render_pass_descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
            render_pass_descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
            render_pass_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::sampled;
            render_pass_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::shader_read_only_optimal;
            render_pass_descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::sampled;
            render_pass_descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::shader_read_only_optimal;
            render_pass_descriptor.color_attachments[0].clear_value[0] = reverse_depth ? 0.0 : 1.0;
        }
        render_pass_descriptor.render_target_width             = resolution;
        render_pass_descriptor.render_target_height            = resolution;
        render_pass_descriptor.debug_label                     = erhe::utility::Debug_label{fmt::format("Shadow {}", i)};
        std::unique_ptr<erhe::graphics::Render_pass> render_pass = std::make_unique<Render_pass>(graphics_device, render_pass_descriptor);
        m_gpu_timer_labels.emplace_back(fmt::format("Shadow {}", i));
        m_gpu_timers.emplace_back(
            std::make_unique<erhe::graphics::Gpu_timer>(*render_pass.get(), m_gpu_timer_labels.back().c_str())
        );
        m_render_passes.emplace_back(std::move(render_pass));
    }

    m_viewport = {
        .x      = 0,
        .y      = 0,
        .width  = resolution,
        .height = resolution
    };

    // Point-light shadow cube array (R32F radial distance) + a shared 2D depth
    // scratch reused as the rasterization depth for every face + 6 *
    // point_light_count render passes. Allocated only when point shadows are
    // enabled; otherwise the forward pass binds Light_buffer's fallback cube and
    // no point-cube render passes are issued.
    m_point_cube_texture.reset();
    m_point_cube_depth.reset();
    m_point_render_passes.clear();
    m_point_viewport = {0, 0, 0, 0};
    if ((point_light_count > 0) && (point_resolution > 0)) {
        const int point_res = std::max(1, point_resolution);
        {
            ERHE_PROFILE_SCOPE("allocating point shadow cube array texture");
            m_point_cube_texture = std::make_shared<Texture>(
                graphics_device,
                erhe::graphics::Texture_create_info {
                    .device            = graphics_device,
                    .usage_mask        =
                        erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                        erhe::graphics::Image_usage_flag_bit_mask::sampled |
                        erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
                    .type              = erhe::graphics::Texture_type::texture_cube_map_array,
                    .pixelformat       = erhe::dataformat::Format::format_32_scalar_float,
                    .width             = point_res,
                    .height            = point_res,
                    .depth             = 1,
                    .array_layer_count = 6 * point_light_count, // 6 faces per cube
                    .debug_label       = "Point shadow cube array"
                }
            );
            // Transition all layer-faces UNDEFINED -> shader_read_only_optimal so
            // every cube/face the receiver can sample is in a defined layout even
            // before its render pass first writes it (mirrors m_distance_texture).
            command_buffer.transition_texture_layout(*m_point_cube_texture.get(), erhe::graphics::Image_layout::shader_read_only_optimal);
        }
        {
            ERHE_PROFILE_SCOPE("allocating point shadow cube depth scratch");
            m_point_cube_depth = std::make_shared<Texture>(
                graphics_device,
                erhe::graphics::Texture_create_info {
                    .device            = graphics_device,
                    .usage_mask        = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment,
                    .type              = erhe::graphics::Texture_type::texture_2d,
                    .pixelformat       = depth_format,
                    .width             = point_res,
                    .height            = point_res,
                    .depth             = 1,
                    .array_layer_count = 0, // plain 2D texture (non-array); see Texture_create_info default
                    .debug_label       = "Point shadow cube depth scratch"
                }
            );
            command_buffer.transition_texture_layout(*m_point_cube_depth.get(), erhe::graphics::Image_layout::depth_stencil_attachment_optimal);
        }
        m_point_render_passes.reserve(static_cast<std::size_t>(6 * point_light_count));
        for (int cube = 0; cube < point_light_count; ++cube) {
            for (int face = 0; face < 6; ++face) {
                erhe::graphics::Render_pass_descriptor rp;
                // Color: one cube face (layer 6*cube + face). Clear to a large
                // distance so unrendered texels read "occluder very far" -> lit.
                rp.color_attachments[0].texture        = m_point_cube_texture.get();
                rp.color_attachments[0].texture_level  = 0;
                rp.color_attachments[0].texture_layer  = static_cast<unsigned int>((6 * cube) + face);
                rp.color_attachments[0].load_action    = erhe::graphics::Load_action::Clear;
                rp.color_attachments[0].store_action   = erhe::graphics::Store_action::Store;
                rp.color_attachments[0].usage_before   = erhe::graphics::Image_usage_flag_bit_mask::sampled;
                rp.color_attachments[0].layout_before  = erhe::graphics::Image_layout::shader_read_only_optimal;
                rp.color_attachments[0].usage_after    = erhe::graphics::Image_usage_flag_bit_mask::sampled;
                rp.color_attachments[0].layout_after   = erhe::graphics::Image_layout::shader_read_only_optimal;
                rp.color_attachments[0].clear_value[0] = 1.0e30;
                // Depth: shared scratch, cleared each face, never sampled.
                rp.depth_attachment.texture            = m_point_cube_depth.get();
                rp.depth_attachment.texture_level      = 0;
                rp.depth_attachment.texture_layer      = 0;
                rp.depth_attachment.load_action        = erhe::graphics::Load_action::Clear;
                rp.depth_attachment.store_action       = erhe::graphics::Store_action::Dont_care;
                rp.depth_attachment.usage_before       = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
                rp.depth_attachment.layout_before      = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
                rp.depth_attachment.usage_after        = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
                rp.depth_attachment.layout_after       = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
                rp.depth_attachment.clear_value[0]     = reverse_depth ? 0.0 : 1.0;
                rp.render_target_width                 = point_res;
                rp.render_target_height                = point_res;
                rp.debug_label                         = erhe::utility::Debug_label{fmt::format("Point shadow cube {} face {}", cube, face)};
                m_point_render_passes.emplace_back(std::make_unique<Render_pass>(graphics_device, rp));
            }
        }
        m_point_viewport = {0, 0, point_res, point_res};
    }

    // Invalidate m_light_projections which at this point has stale texture handles
    m_light_projections = erhe::scene_renderer::Light_projections{};
}

void Shadow_render_node::execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    ERHE_PROFILE_FUNCTION();

    // Render shadow maps
    const auto& scene_root = m_scene_view.get_scene_root();
    std::shared_ptr<erhe::scene::Camera> camera = m_scene_view.get_camera();
    if (!scene_root || !camera) {
        return;
    }

    // Optional shadow fit target camera override (Scene View Config window):
    // fit the shadow frustum (and collect the fit debug data) for this camera
    // instead of the view camera, so the fit can be observed from outside.
    // Ignored when the camera no longer exists or belongs to another scene.
    const std::shared_ptr<erhe::scene::Camera> override_camera = m_scene_view.get_shadow_fit_override_camera().lock();
    if (override_camera) {
        const erhe::scene::Node* const override_camera_node = override_camera->get_node();
        if ((override_camera_node != nullptr) && (override_camera_node->get_scene() == scene_root->get_hosted_scene())) {
            camera = override_camera;
        }
    }

    const auto& layers = scene_root->layers();
    if (layers.content()->meshes.empty()) {
        return;
    }

    if (!m_texture) {
        return;
    }

    // Lazily reconfigure when the active shadow technique changed (the preset's
    // Shadow_technique_mode flips between depth and distance). reconfigure()
    // (re)allocates the R32F distance map and rebuilds the render passes with /
    // without the color attachment.
    if (m_context.app_settings != nullptr) {
        const bool want_distance =
            (m_context.app_settings->graphics.current_graphics_preset.shadow_technique == Shadow_technique_mode::distance);
        if (want_distance != m_distance_technique) {
            reconfigure(*m_context.graphics_device, command_buffer, m_resolution, m_light_count, m_depth_bits, want_distance, m_point_resolution, m_point_light_count);
            if (!m_texture) {
                return;
            }
        }
    }

    // TODO: Keep this vector in content library and update when needed.
    //       Make content library item host for content library nodes.
    const auto& material_library = scene_root->get_content_library()->materials;
    const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials = material_library->get_all<erhe::primitive::Material>();

    scene_root->sort_lights();

    // Refresh the shadow frustum fit settings from the live editor settings
    // (edited in the Settings window). m_fit_settings must outlive the render
    // call; Light_projection_parameters keeps a pointer to it.
    if (m_context.editor_settings != nullptr) {
        // Resolve per scene (#239): a scene may override shadow frustum fit.
        const Shadow_frustum_fit_config& config = get_effective_shadow_frustum_fit(*m_context.editor_settings, *scene_root);
        m_fit_settings = erhe::scene::Shadow_frustum_fit_settings{
            .fit_to_view_frustum    = config.fit_to_view_frustum,
            .fit_to_casters         = config.fit_to_casters,
            .fit_to_receivers       = config.fit_to_receivers,
            .fit_to_receivers_hull  = config.fit_to_receivers_hull,
            .optimize_rotation      = config.optimize_rotation,
            .near_from_main_frustum = config.near_from_main_frustum,
            .depth_clamp            = config.depth_clamp,
            .texel_snap             = config.texel_snap,
            .quantize_extents       = config.quantize_extents,
            .quantize_step          = config.quantize_step,
            .cap_by_shadow_range    = config.cap_by_shadow_range,
            .collect_debug          = config.collect_debug
        };
    } else {
        m_fit_settings = erhe::scene::Shadow_frustum_fit_settings{};
    }

    // The tight frustum fit needs the main camera viewport for the view
    // frustum aspect ratio; only Viewport_scene_view has one. The headset has
    // no viewport, but its root camera is driven as perspective_xr (from the
    // combined stereo eye frustum, see Headset_view::update_root_camera_projection),
    // and perspective_xr derives the frustum from explicit fov sides and ignores
    // the aspect ratio - so the empty viewport here is harmless for that path.
    const Viewport_scene_view* viewport_scene_view = m_scene_view.as_viewport_scene_view();
    const erhe::math::Viewport view_camera_viewport = (viewport_scene_view != nullptr)
        ? viewport_scene_view->get_projection_viewport()
        : erhe::math::Viewport{};

    // Diagnostics: the tight shadow fit derives its view frustum from this
    // camera + viewport. For the headset there is no
    // Viewport_scene_view (viewport is empty), and the root camera is driven as
    // perspective_xr from the combined stereo eye frustum
    // (Headset_view::update_root_camera_projection); log on change so a Quest
    // run shows the fov sides the fit is actually fitted to. For perspective_xr
    // the meaningful fields are the fov sides, not fov_x/fov_y.
    {
        const erhe::scene::Projection* const projection = camera->projection();
        const erhe::scene::Projection::Type  type       = (projection != nullptr) ? projection->projection_type : erhe::scene::Projection::Type::other;
        if (
            (camera.get()                != m_dbg_last_camera) ||
            (view_camera_viewport.width  != m_dbg_last_viewport_width) ||
            (view_camera_viewport.height != m_dbg_last_viewport_height) ||
            (static_cast<int>(type)      != m_dbg_last_projection_type)
        ) {
            m_dbg_last_camera          = camera.get();
            m_dbg_last_viewport_width  = view_camera_viewport.width;
            m_dbg_last_viewport_height = view_camera_viewport.height;
            m_dbg_last_projection_type = static_cast<int>(type);
            log_render->info(
                "Shadow fit: view={} camera='{}' projection={} fov[L{:.1f} R{:.1f} U{:.1f} D{:.1f}]deg fov_y={:.1f}deg viewport={}x{} aspect={:.3f} shadow_range={:.2f} tight_fit={}",
                (viewport_scene_view != nullptr) ? "viewport" : "headset/other",
                camera->get_name(),
                (projection != nullptr) ? erhe::scene::Projection::c_type_strings[static_cast<unsigned int>(type)] : "none",
                (projection != nullptr) ? glm::degrees(projection->fov_left ) : 0.0f,
                (projection != nullptr) ? glm::degrees(projection->fov_right) : 0.0f,
                (projection != nullptr) ? glm::degrees(projection->fov_up   ) : 0.0f,
                (projection != nullptr) ? glm::degrees(projection->fov_down ) : 0.0f,
                (projection != nullptr) ? glm::degrees(projection->fov_y) : 0.0f,
                view_camera_viewport.width,
                view_camera_viewport.height,
                view_camera_viewport.aspect_ratio(),
                camera->get_shadow_range(),
                m_fit_settings.any_tightening_enabled()
            );
        }
    }

    // Hardware (rasterizer) depth bias and face culling for the shadow caster
    // pass, from the active graphics preset (depth bias 0 = off, cull mode
    // defaults to front-face culling).
    float depth_bias_constant = 0.0f;
    float depth_bias_slope    = 0.0f;
    erhe::scene_renderer::Shadow_cull_mode cull_mode = erhe::scene_renderer::Shadow_cull_mode::cull_front;
    bool  use_distance        = false;
    float distance_bias_coeff = 0.0f;
    if (m_context.app_settings != nullptr) {
        const Graphics_preset_entry& preset = m_context.app_settings->graphics.current_graphics_preset;
        depth_bias_constant = preset.shadow_depth_bias_constant;
        depth_bias_slope    = preset.shadow_depth_bias_slope;
        cull_mode           = static_cast<erhe::scene_renderer::Shadow_cull_mode>(preset.shadow_cull_mode);
        use_distance        = (preset.shadow_technique == Shadow_technique_mode::distance) && static_cast<bool>(m_distance_texture);
        // fwidth bias coefficient = cdd * (1 + pcfRadius). cdd is -1 for reverse-Z
        // (the sign that pushes the stored occluder away from the light), +1 for
        // forward-Z. pcfRadius = K/2 from the active Shadow_filter_mode (whose
        // value is the PCF kernel width K), so the bake covers the receiver's PCF
        // footprint (see doc/shadows.md).
        const float cdd        = m_scene_view.get_reverse_depth() ? -1.0f : 1.0f;
        const float pcf_radius = 0.5f * static_cast<float>(static_cast<uint32_t>(preset.shadow_filter));
        distance_bias_coeff    = cdd * (1.0f + pcf_radius);
    }

    m_context.shadow_renderer->render(
        erhe::scene_renderer::Shadow_renderer::Render_parameters{
            .command_buffer        = command_buffer,
            .view_camera           = camera.get(),
            .view_camera_viewport  = view_camera_viewport,
            .light_camera_viewport = m_viewport,
            .texture               = m_texture,
            .render_passes         = m_render_passes,
            .mesh_spans            = { layers.content()->meshes },
            .lights                = layers.light()->lights,
            .skins                 = scene_root->get_scene().get_skins(),
            .materials             = materials,
            .light_projections     = m_light_projections,
            .reverse_depth         = m_scene_view.get_reverse_depth(),
            .depth_range           = m_scene_view.get_depth_range(),
            .conventions           = m_scene_view.get_conventions(),
            .fit_settings          = &m_fit_settings,
            .depth_bias_constant   = depth_bias_constant,
            .depth_bias_slope      = depth_bias_slope,
            .cull_mode             = cull_mode,
            .distance_texture      = use_distance ? m_distance_texture : std::shared_ptr<erhe::graphics::Texture>{},
            .use_distance          = use_distance,
            .distance_bias_coeff   = distance_bias_coeff,
            .point_cube_texture       = m_point_cube_texture,
            .point_cube_render_passes = &m_point_render_passes,
            .point_shadow_viewport    = m_point_viewport
        }
    );
}

auto Shadow_render_node::get_producer_output_texture(const int key, int) const -> std::shared_ptr<erhe::graphics::Texture>
{
    if (key == erhe::rendergraph::Rendergraph_node_key::shadow_maps) {
        return m_texture;
    }
    return std::shared_ptr<erhe::graphics::Texture>{};
}

auto Shadow_render_node::get_scene_view() -> Scene_view&
{
    return m_scene_view;
}

auto Shadow_render_node::get_scene_view() const -> const Scene_view&
{
    return m_scene_view;
}

auto Shadow_render_node::get_light_projections() -> erhe::scene_renderer::Light_projections&
{
    return m_light_projections;
}

auto Shadow_render_node::get_texture() const -> std::shared_ptr<erhe::graphics::Texture>
{
    return m_texture;
}

auto Shadow_render_node::get_viewport() const -> erhe::math::Viewport
{
    return m_viewport;
}

auto Shadow_render_node::get_render_passes() const -> std::span<const std::unique_ptr<erhe::graphics::Render_pass>>
{
    return std::span<const std::unique_ptr<erhe::graphics::Render_pass>>{m_render_passes};
}

auto Shadow_render_node::get_reverse_depth() const -> bool
{
    // The single static device value (Scene_view::get_reverse_depth() delegates
    // to Device::get_reverse_depth()). This matches the value fed to
    // Shadow_renderer::Render_parameters in execute_rendergraph_node and the
    // depth clear value reconfigure() bakes, and is the key the prewarm uses.
    return m_scene_view.get_reverse_depth();
}

auto Shadow_render_node::inputs_allowed() const -> bool
{
    return false;
}

}
