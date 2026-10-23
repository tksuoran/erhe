#include "rendergraph/shadow_render_node.hpp"

#include "app_context.hpp"
#include "renderers/render_settings.hpp"
#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/shadow_frustum_fit_config.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/shader_key.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_settings_resolve.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_view.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene_renderer/shadow_renderer.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>
#include <chrono>

namespace editor {

using erhe::graphics::Render_pass;
using erhe::graphics::Texture;

auto choose_shadow_depth_format(erhe::graphics::Device& graphics_device, const int requested_depth_bits) -> erhe::dataformat::Format
{
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
    return graphics_device.choose_depth_stencil_format(formats);
}

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
    , m_context            {context}
    , m_scene_view         {scene_view}
    , m_configuration_slots{graphics_device.get_number_of_frames_in_flight()}
{
    ERHE_VERIFY(!m_configuration_slots.empty());
    m_frame_results.resize(graphics_device.get_number_of_frames_in_flight());
    register_output("shadow_maps", erhe::rendergraph::Rendergraph_node_key::shadow_maps);

    // Start on the depth technique; prepare() requests distance for the first
    // prepared frame if the active preset selects it (the App_context
    // settings are not available to read here at construction time).
    m_requested_configuration = Shadow_node_configuration{
        .resolution         = resolution,
        .light_count        = light_count,
        .depth_bits         = depth_bits,
        .distance_technique = false,
        .point_resolution   = point_resolution,
        .point_light_count  = point_light_count
    };
    reconfigure(graphics_device, init_command_buffer, m_requested_configuration);
}

Shadow_render_node::~Shadow_render_node() noexcept
{
}

void Shadow_render_node::reconfigure(erhe::graphics::Device& graphics_device, erhe::graphics::Command_buffer& command_buffer, const Shadow_node_configuration& configuration)
{
    const int  resolution           = configuration.resolution;
    const int  light_count          = configuration.light_count;
    const int  requested_depth_bits = configuration.depth_bits;
    const bool distance_technique   = configuration.distance_technique;
    const int  point_resolution     = configuration.point_resolution;
    const int  point_light_count    = configuration.point_light_count;
    // The configuration is applied (or failed, below) once: a request is not
    // retried each frame.
    const Shadow_node_configuration previous = m_configuration;
    m_configuration = configuration;
    // The maps are reallocated (or released) below: nothing they held stays.
    m_map_cache.clear();

    // Reverse-Z is the static device value; query it rather than caching.
    const bool reverse_depth = graphics_device.get_reverse_depth();
    const erhe::dataformat::Format depth_format = choose_shadow_depth_format(graphics_device, requested_depth_bits);
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
        (previous.light_count               == light_count) &&
        (m_texture->get_pixelformat()       == depth_format) &&
        (previous.distance_technique        == distance_technique) &&
        (previous.point_resolution          == point_resolution) &&
        (previous.point_light_count         == point_light_count)
    ) {
        log_render->debug(
            "Reconfigure shadow resolution = {}, light count = {}, format = {} - match old settings, skip",
            resolution, light_count, erhe::dataformat::c_str(depth_format)
        );
        return;
    }
    log_render->info(
        "Reconfigure shadow resolution = {}, light count = {}, requested depth bits = {} -> format = {}, ERHE_SHADOW_DEPTH_BITS = {}",
        resolution, light_count, requested_depth_bits, erhe::dataformat::c_str(depth_format),
        erhe::scene_renderer::get_shadow_depth_bits_axis(depth_format)
    );

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
    // array the caster writes its planes' light distances into, sampled
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
            // Distance map color attachment: the caster writes its plane's light
            // distance on each texel's centre ray here. Cleared to a large
            // distance so empty texels resolve to "lit", like the depth
            // attachment's far clear. Kept in shader_read_only between passes
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
            render_pass_descriptor.color_attachments[0].clear_value[0] = 1.0e30;
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

    // The frame results of earlier frames keep their references to the
    // replaced textures until their slots are rewritten; the main thread may
    // still read the last rendered frame's. This frame's result is cleared at
    // the start of execute_rendergraph_node(), before this runs.
}

// One white directional light along the view camera's axis, standing in for
// the lights a scene has none of. It is not a scene item: nothing owns it but
// this render node, so it appears in no hierarchy, no save and no undo.
auto Shadow_render_node::resolve_headlight(const erhe::scene::Camera_snapshot& camera) -> erhe::scene_renderer::Light_set&
{
    if (!m_headlight) {
        m_headlight = std::make_shared<erhe::scene::Light>("Headlight");
        m_headlight->set_light_type(erhe::scene::Light_type::directional);
        m_headlight->set_color(glm::vec3{1.0f, 1.0f, 1.0f});
        // The intensity the default scene's sun and fill add up to: a white
        // Lambert surface facing the camera reaches mid-gray at the default
        // exposure.
        m_headlight->set_intensity(4.0f);
        m_headlight->set_range(0.0f);
        m_headlight->set_cast_shadow(false);
        m_headlight_lights[0] = m_headlight;
    }

    // The light frame takes its direction from the prim's +Z, which is the
    // axis a camera looks down the negative of - so the direction from a lit
    // surface toward the headlight is the camera's own +Z, and the camera's
    // world transform is the headlight's. Written only when the camera moved:
    // this is the whole per-frame cost of an unlit scene.
    const glm::mat4 world_from_node = camera.world_from_node();
    if (world_from_node != m_headlight_world_from_node) {
        m_headlight_world_from_node = world_from_node;
        m_headlight->set_parent_from_node(world_from_node);
        m_headlight_set.invalidate();
    }

    // One unshadowed directional slot of its own: the headlight casts no
    // shadow, and the active preset may hand out no unshadowed directional
    // slot at all, which would leave it unshaded.
    const erhe::scene_renderer::Light_count_limits headlight_limits{
        .per_type_shadow     = {0, 0, 0, 0},
        .per_type_unshadowed = {1, 0, 0, 0}
    };
    m_headlight_set.resolve(m_headlight_lights, headlight_limits, m_context.program_interface->light_interface.max_light_count);
    return m_headlight_set;
}

void Shadow_render_node::request_configuration(const Shadow_node_configuration& configuration)
{
    erhe::utility::Thread_roles::assert_main_thread();
    m_requested_configuration = configuration;
}

void Shadow_render_node::prepare()
{
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);
    // The active shadow technique (the preset's Shadow_technique_mode flips
    // between depth and distance): reconfigure() (re)allocates the R32F
    // distance map and rebuilds the render passes with / without the color
    // attachment.
    m_requested_configuration.distance_technique =
        (m_context.render_settings->graphics_preset.shadow_technique == Shadow_technique_mode::distance);
    const uint64_t      frame_index = m_context.graphics_device->get_frame_index();
    Configuration_slot& slot        = m_configuration_slots[frame_index % m_configuration_slots.size()];
    slot.frame_index   = frame_index;
    slot.configuration = m_requested_configuration;
}

void Shadow_render_node::apply_requested_configuration(erhe::graphics::Command_buffer& command_buffer)
{
    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);
    const uint64_t            frame_index = m_context.graphics_device->get_frame_index();
    const Configuration_slot& slot        = m_configuration_slots[frame_index % m_configuration_slots.size()];
    if ((slot.frame_index != frame_index) || (slot.configuration == m_configuration)) {
        return;
    }
    // The layout transitions record into the frame command buffer, outside
    // any render pass, before this node's passes use the maps.
    reconfigure(*m_context.graphics_device, command_buffer, slot.configuration);
}

void Shadow_render_node::prepare_casters(const uint64_t frame_index, const std::size_t frames_in_flight)
{
    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);

    ERHE_VERIFY(frames_in_flight > 0);
    if (m_caster_slots.size() != frames_in_flight) {
        m_caster_slots.resize(frames_in_flight);
    }
    Caster_slot& slot = m_caster_slots[frame_index % frames_in_flight];
    slot.frame_index = frame_index;
    slot.prepared    = false;

    // The render phase takes the draw-list path with the gate on; the
    // bucket path is the one to prepare (execute_rendergraph_node()).
    const Render_settings& render_settings = *m_context.render_settings;
    if (render_settings.use_draw_lists) {
        return;
    }
    const std::shared_ptr<Scene_root> scene_root = m_scene_view.get_scene_root();
    if (!scene_root) {
        return;
    }
    const std::span<const std::shared_ptr<erhe::scene::Mesh>> mesh_spans[] = {
        scene_root->layers().content()->meshes
    };
    // Bucketing reads the meshes' Buffer_mesh variants, which a worker may
    // be replacing (commit_geometry_buffer_mesh, under this mutex).
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{scene_root->item_host_mutex};
    m_context.shadow_renderer->prepare(
        erhe::scene_renderer::Shadow_renderer::Prepare_parameters{
            .mesh_spans            = mesh_spans,
            // The root's forward set, which the bucket path binds (D5).
            .material_source       = &scene_root->get_material_set(),
            .exclude_unlit_casters = render_settings.exclude_unlit_primitives
        },
        slot.casters
    );
    slot.prepared = true;
}

namespace {

// CPU timing of the node's work of one frame (P4 measurement): the scope
// guard adds the elapsed time to out_us on every return path.
class Cpu_time_scope
{
public:
    explicit Cpu_time_scope(double& out_us)
        : m_out_us{out_us}
        , m_start {std::chrono::steady_clock::now()}
    {
    }
    ~Cpu_time_scope() noexcept
    {
        const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        m_out_us += std::chrono::duration<double, std::micro>(end - m_start).count();
    }
    Cpu_time_scope(const Cpu_time_scope&)            = delete;
    Cpu_time_scope& operator=(const Cpu_time_scope&) = delete;

private:
    double&                               m_out_us;
    std::chrono::steady_clock::time_point m_start;
};

} // anonymous namespace

void Shadow_render_node::pre_execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    ERHE_PROFILE_FUNCTION();

    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);

    // This frame's result slot (one per frame in flight). The slot holds
    // what the frame frames-in-flight earlier left; reset what this frame
    // publishes before anything can return, so every exit below - no scene
    // root, no camera, no content mesh, no shadow map texture - leaves an
    // empty resolution for the consumers (Composition_pass ->
    // Light_buffer::update) and the main thread instead of an older frame's.
    const uint64_t       frame_index = m_context.graphics_device->get_frame_index();
    Shadow_frame_result& result      = m_frame_results.write(frame_index);
    // (lights keeps its elements: publish_light_copies() assigns over them,
    // so the name strings keep their capacity; only the projections point
    // into it.)
    result.light_projections.clear();
    result.draw_statistics.clear();
    result.light_cluster_stats       = erhe::scene_renderer::Light_cluster_stats{};
    result.light_cluster_gpu_time_ns = 0;
    // Execute draws only what this hook fitted for this frame.
    m_frame_passes.frame_index = frame_index;
    m_frame_passes.parameters.reset();
    m_frame_passes.scene_root.reset();

    // The frame's CPU time: this hook and execute_rendergraph_node() add
    // into it.
    result.cpu_time_us = 0.0;
    const Cpu_time_scope cpu_time_scope{result.cpu_time_us};

    // The configuration the prepare phase of this frame requested, before
    // anything below reads the maps or the render passes.
    apply_requested_configuration(command_buffer);

    // Render shadow maps. The view's scene root and camera as the render
    // phase's copies of this frame (Scene_view::get_render_scene_root(),
    // get_render_camera()).
    const std::shared_ptr<Scene_root> scene_root = m_scene_view.get_render_scene_root();
    const erhe::scene::Camera_snapshot* camera = m_scene_view.get_render_camera();
    if (!scene_root || (camera == nullptr)) {
        return;
    }

    // Optional shadow fit target camera override (Scene View Config window):
    // fit the shadow frustum (and collect the fit debug data) for this camera
    // instead of the view camera, so the fit can be observed from outside.
    // The view copies it only while it exists and belongs to the view's scene.
    const erhe::scene::Camera_snapshot* const override_camera = m_scene_view.get_render_shadow_fit_camera();
    if (override_camera != nullptr) {
        camera = override_camera;
    }

    // The scene's render settings and light set as the prepare phase of
    // this frame copied them (one copy per frame in flight).
    const Scene_render_settings& scene_render_settings = scene_root->get_render_settings(frame_index);
    if (!scene_render_settings.has_content_meshes) {
        return;
    }

    if (!m_texture) {
        return;
    }

    // The scene's resolved light set: which lights are shaded / shadow-mapped,
    // in light UBO slot order, under the active graphics preset's per light
    // type limits (the shadow limits are also what reconfigure() sized the
    // shadow maps from; with shadow_enable off they are zero), as the copies
    // Scene_root::snapshot_render_settings resolved in M2.
    const Render_settings& render_settings = *m_context.render_settings;

    // A scene whose light layer is empty is lit by this view's headlight
    // instead (see resolve_headlight): a file that authors no light renders
    // black otherwise, where usdview lights the stage with a camera light.
    const bool use_headlight =
        !scene_render_settings.has_lights &&
        render_settings.headlight_when_unlit;
    const erhe::scene_renderer::Light_set& light_set = use_headlight
        ? resolve_headlight(*camera)
        : scene_root->get_light_set(frame_index);

    // Shadows off for this view: the preset has shadow_enable off (no shadow
    // maps allocated), or the live per-view Visual Style shadow mode does not
    // ask for live shadow maps (No Shadows and Baked Lightmaps both disable
    // shadow map updates). The lights still need their UBO slots / projection
    // transforms (Light_buffer::update writes only the resolved slots, which
    // would otherwise leave the scene ambient-only), so run just the
    // projection pass with no shadow map texture: Light_buffer then writes
    // the "no shadow map" handle sentinel and the forward shader lights every
    // fragment unshadowed (the shadow-mapped slots of the shared resolution
    // simply sample nothing). No caster / receiver AABB gathering, no frustum
    // fit, and no shadow render passes are issued.
    const bool preset_shadows_enabled = render_settings.graphics_preset.shadow_enable;
    if (!preset_shadows_enabled || (m_scene_view.get_render_config().shadow_mode != Shadow_mode::shadow_maps)) {
        const erhe::math::Viewport light_view_camera_viewport = m_scene_view.get_render_camera_viewport();
        result.light_projections.apply(
            light_set,
            camera,
            light_view_camera_viewport,
            m_viewport,
            {},     // no shadow map -> "no shadow map" sentinel in the light UBO
            m_scene_view.get_reverse_depth(),
            m_scene_view.get_depth_range(),
            m_scene_view.get_conventions(),
            {},     // no caster bounds
            {},     // no receiver bounds
            nullptr // no fit settings
        );
        result.light_projections.shadow_distance_texture = {};
        result.light_projections.shadow_cube_texture     = {};
        publish_light_copies(result, light_set, *camera);
        write_light_view(result, command_buffer);
        return;
    }

    // The shadow technique the maps are configured for follows the preset
    // in prepare() (M2); a flip after that (none: the copy is made before
    // prepare()) renders with the previous configuration this frame.

    // Refresh the shadow frustum fit settings from the scene's render
    // settings copy (the override or the editor-global default, resolved in
    // M2). The frame result's copy outlives the render call;
    // Light_projection_parameters keeps a pointer to it.
    {
        const Shadow_frustum_fit_config& config = scene_render_settings.shadow_frustum_fit;
        result.fit_settings = erhe::scene::Shadow_frustum_fit_settings{
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
    }

    // The tight frustum fit needs the main camera viewport for the view
    // frustum aspect ratio (Scene_view::get_camera_viewport(): a viewport
    // window's or an offscreen scene image's size). The headset has no
    // viewport, but its root camera is driven as perspective_xr (from the
    // combined stereo eye frustum, see Headset_view::update_root_camera_projection),
    // and perspective_xr derives the frustum from explicit fov sides and ignores
    // the aspect ratio - so the empty viewport here is harmless for that path.
    const erhe::math::Viewport view_camera_viewport = m_scene_view.get_render_camera_viewport();

    // Diagnostics: the tight shadow fit derives its view frustum from this
    // camera + viewport. For the headset there is no
    // Viewport_scene_view (viewport is empty), and the root camera is driven as
    // perspective_xr from the combined stereo eye frustum
    // (Headset_view::update_root_camera_projection); log on change so a Quest
    // run shows the fov sides the fit is actually fitted to. For perspective_xr
    // the meaningful fields are the fov sides, not fov_x/fov_y.
    {
        const erhe::scene::Projection&      projection = camera->projection();
        const erhe::scene::Projection::Type type       = projection.projection_type;
        if (
            (view_camera_viewport.width  != m_dbg_last_viewport_width) ||
            (view_camera_viewport.height != m_dbg_last_viewport_height) ||
            (static_cast<int>(type)      != m_dbg_last_projection_type)
        ) {
            m_dbg_last_viewport_width  = view_camera_viewport.width;
            m_dbg_last_viewport_height = view_camera_viewport.height;
            m_dbg_last_projection_type = static_cast<int>(type);
            log_render->info(
                "Shadow fit: view={} projection={} fov[L{:.1f} R{:.1f} U{:.1f} D{:.1f}]deg fov_y={:.1f}deg viewport={}x{} aspect={:.3f} shadow_range={:.2f} tight_fit={}",
                (m_scene_view.as_viewport_scene_view() != nullptr) ? "viewport" : ((view_camera_viewport.width > 0) ? "offscreen" : "headset/other"),
                erhe::scene::Projection::c_type_strings[static_cast<unsigned int>(type)],
                glm::degrees(projection.fov_left ),
                glm::degrees(projection.fov_right),
                glm::degrees(projection.fov_up   ),
                glm::degrees(projection.fov_down ),
                glm::degrees(projection.fov_y),
                view_camera_viewport.width,
                view_camera_viewport.height,
                view_camera_viewport.aspect_ratio(),
                camera->get_shadow_range(),
                result.fit_settings.any_tightening_enabled()
            );
        }
    }

    // Hardware (rasterizer) depth bias and face culling for the shadow caster
    // pass, from the active graphics preset (depth bias 0 = off, cull mode
    // defaults to back-face culling).
    float depth_bias_constant = 0.0f;
    float depth_bias_slope    = 0.0f;
    float bias_texel_scale    = 1.0f;
    float bias_origin_scale   = 1.0f;
    erhe::scene_renderer::Shadow_cull_mode cull_mode = erhe::scene_renderer::Shadow_cull_mode::cull_back;
    bool  use_distance        = false;
    // The forward pass samples these maps with the preset's filter, whose
    // value is the kernel width K (0 = hard).
    erhe::scene::Shadow_map_footprint shadow_map_footprint{};
    {
        const Graphics_preset_entry& preset = render_settings.graphics_preset;
        depth_bias_constant = preset.shadow_depth_bias_constant;
        depth_bias_slope    = preset.shadow_depth_bias_slope;
        bias_texel_scale    = preset.shadow_bias_texel_scale;
        bias_origin_scale   = preset.shadow_bias_origin_scale;
        cull_mode           = static_cast<erhe::scene_renderer::Shadow_cull_mode>(preset.shadow_cull_mode);
        use_distance        = (preset.shadow_technique == Shadow_technique_mode::distance) && static_cast<bool>(m_distance_texture);
        shadow_map_footprint   = erhe::scene::Shadow_map_footprint::from_kernel_width(static_cast<unsigned int>(preset.shadow_filter));
    }

    // Draw-list path (plan phase 4): route casters through the scene's
    // persistent shadow draw lists when the gate is on; content layer only,
    // matching mesh_spans below.
    // Every editor scene root keeps a Draw_list_scene, whatever the gate:
    // its slot holds the frame's frustum fit bounds for both paths
    // (Scene_root::prepare_shadow_bounds, M2).
    erhe::scene_renderer::Draw_list_scene* root_draw_list_scene = scene_root->get_draw_list_scene();
    ERHE_VERIFY(root_draw_list_scene != nullptr);
    erhe::scene_renderer::Draw_list_scene* draw_list_scene =
        render_settings.use_draw_lists
            ? root_draw_list_scene
            : nullptr;
    // The bucket path (use_draw_lists off) draws the casters M2 prepared
    // into this frame's slot (prepare_casters()); neither path is handed
    // meshes, and both read the bounds M2 gathered.
    const erhe::scene_renderer::Render_bucket_snapshot* casters = nullptr;
    if (draw_list_scene == nullptr) {
        ERHE_VERIFY(!m_caster_slots.empty());
        const Caster_slot& slot = m_caster_slots[frame_index % m_caster_slots.size()];
        ERHE_VERIFY(slot.prepared && (slot.frame_index == frame_index));
        casters = &slot.casters;
    }

    // Unlit (KHR_materials_unlit) primitives - sky domes, backdrops, emissive
    // decals - are backdrop geometry, not occluders. The bucket path applied
    // the flag when it prepared the casters (prepare_casters()); the
    // draw-list path bakes shadow membership into its cached entries and
    // gets the flag from the setting's change sites
    // (App_scenes::set_exclude_unlit_from_shadows).
    const bool exclude_unlit_casters = render_settings.exclude_unlit_primitives;

    // The distance fallback report compares with the previous frame's
    // report, whichever frame result that went to.
    result.light_projections.distance_fallback_light_ids = m_distance_fallback_light_ids;

    // The parameters fit() and draw() take, built once: the fit's light
    // frusta are the shadow passes' culling views, which the frame's
    // classification classifies with the color views after every node's
    // pre-execute hook (doc/plans/culling.md phase 6a).
    m_frame_passes.scene_root       = scene_root;
    m_frame_passes.draw_list_layers = { scene_root->layers().content()->id }; // content layer only, as the bucket path
    m_frame_passes.parameters.emplace(
        erhe::scene_renderer::Shadow_renderer::Render_parameters{
            .command_buffer        = command_buffer,
            .view_camera           = camera,
            .view_camera_viewport  = view_camera_viewport,
            .light_camera_viewport = m_viewport,
            .texture               = m_texture,
            .render_passes         = m_render_passes,
            .mesh_spans            = m_no_mesh_spans,
            .light_set             = light_set,
            .joint_poses           = scene_root->get_render_joint_poses(frame_index),
            // The set that matches the path this light takes: the draw list's
            // own when the shadow pass draws from cached entries, the root's
            // forward set when it re-buckets (D5). draw_list_scene is resolved
            // just above and decides both.
            .material_source       = (draw_list_scene != nullptr)
                ? &draw_list_scene->get_material_set()
                : &scene_root->get_material_set(),
            .light_projections     = result.light_projections,
            .reverse_depth         = m_scene_view.get_reverse_depth(),
            .depth_range           = m_scene_view.get_depth_range(),
            .conventions           = m_scene_view.get_conventions(),
            .fit_settings          = &result.fit_settings,
            .shadow_map_footprint  = shadow_map_footprint,
            .depth_bias_constant   = depth_bias_constant,
            .depth_bias_slope      = depth_bias_slope,
            .shadow_bias_texel_scale  = bias_texel_scale,
            .shadow_bias_origin_scale = bias_origin_scale,
            .cull_mode             = cull_mode,
            .distance_texture      = use_distance ? m_distance_texture : std::shared_ptr<erhe::graphics::Texture>{},
            .use_distance          = use_distance,
            .point_cube_texture       = m_point_cube_texture,
            .point_cube_render_passes = &m_point_render_passes,
            .point_shadow_viewport    = m_point_viewport,
            .draw_list_scene          = draw_list_scene,
            .draw_list_layers         = m_frame_passes.draw_list_layers,
            .draw_statistics          = &result.draw_statistics,
            .map_cache                = render_settings.shadow_map_cache ? &m_map_cache : nullptr,
            .fit_cache                = render_settings.shadow_fit_cache ? &m_fit_cache : nullptr,
            .exclude_unlit_casters    = exclude_unlit_casters,
            .casters                  = casters,
            .shadow_bounds            = &root_draw_list_scene->get_shadow_bounds(frame_index)
        }
    );
    // With the cache switched off the passes draw every frame without it:
    // what it held is not kept across the cache-off period.
    if (!render_settings.shadow_map_cache) {
        m_map_cache.clear();
    }
    if (!render_settings.shadow_fit_cache) {
        m_fit_cache.clear();
    }
    m_context.shadow_renderer->fit(m_frame_passes.parameters.value(), m_cull_views);
    m_distance_fallback_light_ids = result.light_projections.distance_fallback_light_ids;
    publish_light_copies(result, light_set, *camera);
    write_light_view(result, command_buffer);
}

void Shadow_render_node::write_light_view(Shadow_frame_result& result, erhe::graphics::Command_buffer& command_buffer)
{
    const erhe::scene_renderer::Light_cluster_view cluster_view{
        .camera        = m_scene_view.get_render_camera(),
        .viewport      = m_scene_view.get_render_camera_viewport(),
        .reverse_depth = m_scene_view.get_reverse_depth(),
        .depth_range   = m_scene_view.get_depth_range(),
        .conventions   = m_scene_view.get_conventions()
    };
    // The editor's codegen enum mirrors the library enum value for value.
    static_assert(static_cast<unsigned int>(Light_cluster_binning::cpu) == static_cast<unsigned int>(erhe::scene_renderer::Light_cluster_binning::cpu));
    static_assert(static_cast<unsigned int>(Light_cluster_binning::gpu) == static_cast<unsigned int>(erhe::scene_renderer::Light_cluster_binning::gpu));
    static_assert(static_cast<unsigned int>(erhe::scene_renderer::Light_cluster_binning::count) == 2u);
    const unsigned int binning_value = static_cast<unsigned int>(m_context.render_settings->light_cluster_binning);
    ERHE_VERIFY(binning_value < static_cast<unsigned int>(erhe::scene_renderer::Light_cluster_binning::count));
    m_context.shadow_renderer->get_light_buffer().write_view(
        result.light_projections,
        cluster_view,
        erhe::scene_renderer::Light_view_binning{
            .binning        = static_cast<erhe::scene_renderer::Light_cluster_binning>(binning_value),
            .gpu_binner     = m_context.light_cluster_gpu_binner,
            .command_buffer = &command_buffer,
            .view_state     = &m_light_cluster_view_state,
            .label          = m_scene_view.get_settings_key()
        }
    );
    const erhe::scene_renderer::Light_cluster_stats& stats = m_light_cluster_view_state.get_stats();
    result.light_cluster_stats       = stats;
    result.light_cluster_gpu_time_ns = m_light_cluster_view_state.get_gpu_time_ns();

    // A comparison with the previously logged count, for one log line per
    // change. Binning reruns every frame (its inputs move with every camera
    // and light change), and the overflow count is a result of that binning
    // (for gpu binning read back with the retired frame) with no change
    // event of its own; the comparison only gates the log, the count itself
    // is derived by the binning.
    if (stats.valid && (stats.overflow_cluster_count != m_light_cluster_overflow_count)) {
        m_light_cluster_overflow_count = stats.overflow_cluster_count;
        if (m_light_cluster_overflow_count > 0) {
            log_render->warn(
                "Light clusters ({}, {} binning): {} clusters of {} exceed the capacity ({} lights); the longest list asks for {} lights",
                m_scene_view.get_settings_key(),
                erhe::scene_renderer::c_str(stats.binning),
                m_light_cluster_overflow_count,
                stats.cluster_count,
                stats.capacity,
                stats.max_list_length
            );
        } else {
            log_render->info("Light clusters ({}): no cluster exceeds the capacity", m_scene_view.get_settings_key());
        }
    }
}

void Shadow_render_node::execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    ERHE_PROFILE_FUNCTION();

    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);

    // The pre-execute hook of this frame reset the result and fitted the
    // passes (Rendergraph::execute() runs it for every node it executes).
    const uint64_t       frame_index = m_context.graphics_device->get_frame_index();
    Shadow_frame_result& result      = m_frame_results.write(frame_index);
    ERHE_VERIFY(m_frame_passes.frame_index == frame_index);

    {
        const Cpu_time_scope cpu_time_scope{result.cpu_time_us};
        // Frozen (MCP set_shadow_maps_frozen, a measurement switch): the
        // maps keep what the last drawn frame left in them.
        const bool frozen = m_context.render_settings->shadow_maps_frozen && (m_execute_count > 0);
        if (m_frame_passes.parameters.has_value() && !frozen) {
            const erhe::scene_renderer::Shadow_renderer::Render_parameters& parameters = m_frame_passes.parameters.value();
            // The frame command buffer the hook fitted with.
            ERHE_VERIFY(&parameters.command_buffer == &command_buffer);
            m_context.shadow_renderer->draw(parameters, m_cull_views);
        }
    }
    // The frame's parameters and scene root go with the frame.
    m_frame_passes.parameters.reset();
    m_frame_passes.scene_root.reset();

    // The render phase's running totals, with this frame's time (hook and
    // execute).
    m_total_cpu_time_us += result.cpu_time_us;
    ++m_execute_count;
    result.total_cpu_time_us = m_total_cpu_time_us;
    result.execute_count     = m_execute_count;
}

void Shadow_render_node::publish_light_copies(
    Shadow_frame_result&                   result,
    const erhe::scene_renderer::Light_set& light_set,
    const erhe::scene::Camera_snapshot&    camera
)
{
    // apply() gave slot i the light set's light i; the copies keep that
    // order. Copy assignment reuses the capacity of the result's vector and
    // of the name strings, so a steady frame allocates nothing.
    const std::vector<erhe::scene::Light_snapshot>& lights = light_set.get_lights();
    std::vector<erhe::scene::Light_projection_transforms>& transforms = result.light_projections.light_projection_transforms;
    ERHE_VERIFY(transforms.size() == lights.size());
    result.lights = lights;
    for (std::size_t slot = 0, end = transforms.size(); slot < end; ++slot) {
        ERHE_VERIFY(transforms[slot].light == &lights[slot]);
        transforms[slot].light = &result.lights[slot];
    }
    result.view_camera = camera;
    result.light_projections.parameters.view_camera = &result.view_camera;
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

auto Shadow_render_node::get_light_projections(const uint64_t frame_index) const -> const erhe::scene_renderer::Light_projections&
{
    erhe::utility::Thread_roles::assert_render_thread();
    return m_frame_results.get(frame_index).light_projections;
}

auto Shadow_render_node::get_frame_result(const std::optional<uint64_t> frame_index) const -> const Shadow_frame_result*
{
    return m_frame_results.find(frame_index);
}

auto Shadow_render_node::get_total_cpu_time_us(const Shadow_frame_result& result) const -> double
{
    return result.total_cpu_time_us - m_total_cpu_time_baseline_us;
}

auto Shadow_render_node::get_execute_count(const Shadow_frame_result& result) const -> std::size_t
{
    return result.execute_count - m_execute_count_baseline;
}

void Shadow_render_node::reset_cpu_time_stats(const std::optional<uint64_t> frame_index)
{
    const Shadow_frame_result* const result = get_frame_result(frame_index);
    if (result == nullptr) {
        return; // nothing rendered since the previous baseline
    }
    m_total_cpu_time_baseline_us = result->total_cpu_time_us;
    m_execute_count_baseline     = result->execute_count;
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
