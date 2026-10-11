#include "scene/scene_image_capture.hpp"

#include "app_context.hpp"
#include "renderers/render_settings.hpp"
#include "app_rendering.hpp"
#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/sky_config.hpp"
#include "editor_log.hpp"
#include "renderers/render_context.hpp"
#include "renderers/sky_renderer.hpp"
#include "rendergraph/post_processing.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_settings_resolve.hpp"
#include "scene/viewport_scene_views.hpp"
#include "renderers/viewport_config.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_window/window.hpp"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cstring>
#include <limits>

namespace editor {

// ---------------------------------------------------------------------------
// Scene_image_view

Scene_image_view::Scene_image_view(
    App_context&                                context,
    erhe::rendergraph::Rendergraph&             rendergraph,
    const Viewport_config&                      viewport_config,
    const std::shared_ptr<Scene_root>&          scene_root,
    const std::shared_ptr<erhe::scene::Camera>& camera,
    const int                                   width,
    const int                                   height,
    const int                                   msaa_sample_count,
    const erhe::dataformat::Format              color_format,
    const Render_content                        content,
    const erhe::scene_renderer::Shader_debug    shader_debug,
    const std::shared_ptr<erhe::scene::Light>&  shadow_debug_light,
    erhe::graphics::Gpu_timer* const            forward_pass_timer
)
    // No settings store: nothing about this view persists.
    : Scene_view{context, nullptr, "", viewport_config}
    , Texture_rendergraph_node{
        erhe::rendergraph::Texture_rendergraph_node_create_info{
            .rendergraph          = rendergraph,
            .debug_label          = erhe::utility::Debug_label{"Scene image view"},
            .output_key           = erhe::rendergraph::Rendergraph_node_key::viewport_texture,
            .color_format         = color_format,
            .depth_stencil_format = erhe::dataformat::Format::format_d32_sfloat_s8_uint,
            .sample_count         = msaa_sample_count
        }
    }
    , m_camera      {camera}
    , m_viewport    {0, 0, width, height}
    , m_content     {content}
    , m_shader_debug{shader_debug}
    , m_shadow_debug_light_id{shadow_debug_light ? std::optional<std::size_t>{shadow_debug_light->get_id()} : std::nullopt}
    , m_forward_pass_timer{forward_pass_timer}
{
    set_scene_root(scene_root);
    // The content render target for the request's fixed size, here on the
    // main thread before the node first executes; the size never changes,
    // so the render phase only renders into it. The capture's
    // post-processing node sizes its pyramid from it in the render phase.
    m_render_target.update(m_viewport.width, m_viewport.height, nullptr);
    // The render phase renders from this copy of the camera (main thread,
    // at the request): the camera may be a live scene camera. Published per
    // frame by prepare_composition(), with the scene root of that frame
    // (the copy holds no scene root, so the view does not keep the scene
    // alive).
    capture_render_view(m_camera.get(), m_request_render_view);
    m_request_render_view.scene_root.reset();
    register_input("shadow_maps", erhe::rendergraph::Rendergraph_node_key::shadow_maps);
    // The last M2 step prepares this view's composition until it is released.
    m_context.app_rendering->register_scene_image_view(this);
    m_registered_with_app_rendering = true;
}

Scene_image_view::~Scene_image_view() noexcept
{
    unregister_from_app_rendering();
}

void Scene_image_view::unregister_from_app_rendering()
{
    if (!m_registered_with_app_rendering) {
        return;
    }
    m_context.app_rendering->unregister_scene_image_view(this);
    m_registered_with_app_rendering = false;
}

void Scene_image_view::on_released()
{
    unregister_from_app_rendering();
}

void Scene_image_view::prepare_composition(const uint64_t frame_index, const std::size_t frames_in_flight, const int64_t host_system_time_ns)
{
    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);

    Composer_prepared& slot = m_composer_slots.begin_prepare(frame_index, frames_in_flight);

    // The render view copy of this frame: the request's camera copy and the
    // live scene root.
    publish_render_view(m_request_render_view);
    Render_view& render_view = get_render_view();
    render_view.scene_root = get_scene_root();

    if (!is_enabled()) {
        return;
    }
    const std::shared_ptr<Scene_root>& scene_root = render_view.scene_root;
    if (!scene_root || !m_camera || (m_viewport.width < 1) || (m_viewport.height < 1)) {
        return;
    }
    const erhe::scene_renderer::Camera_view_input single_view_input{
        .camera      = get_render_camera(),
        .viewport    = m_viewport,
        .pixel_scale = (m_context.context_window != nullptr) ? m_context.context_window->get_scale_factor() : 1.0f
    };
    // The render phase's context for this view, minus the encoder.
    const Render_context context{
        .command_buffer      = nullptr,
        .encoder             = nullptr,
        .app_context         = m_context,
        .scene_view          = *this,
        .viewport_config     = render_view.viewport_config,
        .camera              = nullptr,
        .camera_snapshot     = get_render_camera(),
        .viewport_scene_view = nullptr,
        .scene_root          = scene_root,
        .viewport            = m_viewport,
        .shader_debug        = m_shader_debug,
        .views               = std::span<const erhe::scene_renderer::Camera_view_input>(&single_view_input, 1),
        .content             = m_content
    };
    // Bucketing reads the meshes' Buffer_mesh variants, which a worker may
    // be replacing (commit_geometry_buffer_mesh, under this mutex).
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{scene_root->item_host_mutex};
    m_context.app_rendering->prepare_composer(context, host_system_time_ns, slot);
}

auto Scene_image_view::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
    return m_camera;
}

auto Scene_image_view::get_projection_scale(const float view_distance) const -> float
{
    // Same formula as Viewport_scene_view::get_projection_scale().
    if (!m_camera) {
        return view_distance;
    }
    const erhe::scene::Camera_projection_transforms transforms = m_camera->projection_transforms(m_viewport, get_reverse_depth(), get_depth_range(), get_conventions());
    const glm::mat4 clip_from_view = transforms.clip_from_camera.get_matrix();
    const glm::mat2 inverted_top_left_2x2 = glm::inverse(glm::mat2{clip_from_view});
    const float x        = inverted_top_left_2x2[0][0];
    const float y        = inverted_top_left_2x2[1][1];
    const float vp_scale = 1000.0f / static_cast<float>(std::min(m_viewport.width, m_viewport.height));
    const float clip_w   = (std::abs(clip_from_view[2][3]) * view_distance) + clip_from_view[3][3];
    return std::min(x, y) * vp_scale * clip_w;
}

auto Scene_image_view::get_rendergraph_node() -> erhe::rendergraph::Rendergraph_node*
{
    return this;
}

auto Scene_image_view::get_shadow_render_node() const -> Shadow_render_node*
{
    return static_cast<Shadow_render_node*>(get_consumer_input_node(erhe::rendergraph::Rendergraph_node_key::shadow_maps));
}

auto Scene_image_view::resolve_shadow_debug_light_index() -> uint32_t
{
    // The light slots are assigned by this view's own shadow pass, which ran
    // earlier in this frame (Light_projections::apply), so resolve here and
    // not at request time.
    m_shadow_debug_light_index.reset();
    if (!m_shadow_debug_light_id.has_value()) {
        return 0u;
    }
    const erhe::scene_renderer::Light_projections* light_projections = get_light_projections();
    const erhe::scene::Light_projection_transforms* transforms = (light_projections != nullptr)
        ? light_projections->get_light_projection_transforms_for_light_id(m_shadow_debug_light_id.value())
        : nullptr;
    if ((transforms == nullptr) || (transforms->index == std::numeric_limits<std::size_t>::max())) {
        // No slot: an index past every shaded light, which the shader reads
        // as "not shadow-mapped" (visibility 1).
        return std::numeric_limits<uint32_t>::max();
    }
    m_shadow_debug_light_index = static_cast<uint32_t>(transforms->index);
    return m_shadow_debug_light_index.value();
}

void Scene_image_view::copy_shadow_projections()
{
    // A copy, not the Light_projections entries: those point at the light
    // set's snapshots and are rewritten by the next apply(), while the reply is
    // built frames later, once the readback retired.
    m_shadow_lights.clear();
    m_shadow_maps = Scene_image_shadow_maps{};
    const erhe::scene_renderer::Light_projections* light_projections = get_light_projections();
    if (light_projections == nullptr) {
        return;
    }
    m_shadow_maps.reverse_depth = light_projections->parameters.reverse_depth;
    m_shadow_maps.depth_range   = light_projections->parameters.depth_range;
    if (light_projections->shadow_map_texture) {
        m_shadow_maps.map_width  = light_projections->shadow_map_texture->get_width();
        m_shadow_maps.map_height = light_projections->shadow_map_texture->get_height();
        m_shadow_maps.map_format = light_projections->shadow_map_texture->get_pixelformat();
    }
    if (light_projections->shadow_distance_texture) {
        m_shadow_maps.distance_format = light_projections->shadow_distance_texture->get_pixelformat();
    }
    if (light_projections->shadow_cube_texture) {
        m_shadow_maps.cube_size   = light_projections->shadow_cube_texture->get_width();
        m_shadow_maps.cube_format = light_projections->shadow_cube_texture->get_pixelformat();
    }
    for (std::size_t slot = 0, end = light_projections->light_projection_transforms.size(); slot < end; ++slot) {
        const erhe::scene::Light_projection_transforms& transforms = light_projections->light_projection_transforms[slot];
        if ((transforms.light == nullptr) || !transforms.is_shadow_mapped()) {
            continue;
        }
        const erhe::scene_renderer::Light_shadow_limits& shadow_limits = light_projections->light_shadow_limits[slot];
        const erhe::scene::Light_snapshot& light = *transforms.light;
        const erhe::scene::Light_type      type  = light.get_light_type();
        m_shadow_lights.push_back(
            Scene_image_shadow_light{
                .name               = std::string{light.get_name()},
                .id                 = light.get_id(),
                .type               = type,
                .slot               = transforms.index,
                .layer              = (type == erhe::scene::Light_type::point) ? transforms.point_shadow_index : transforms.shadow_index,
                .texture_from_world = transforms.texture_from_world.get_matrix(),
                .clip_from_world    = transforms.clip_from_world.get_matrix(),
                // Same expression Light_buffer::update() writes as the light position.
                .position           = glm::vec3{transforms.world_from_light_camera.get_matrix() * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}},
                .raster_vertex_depth    = shadow_limits.raster_vertex_depth,
                .caster_vertex_rounding = shadow_limits.caster_vertex_rounding,
                .distance_rays_valid    = shadow_limits.distance_rays_valid
            }
        );
    }
}

void Scene_image_view::execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    ERHE_PROFILE_FUNCTION();

    // The scene root and camera as this frame's render view copy
    // (prepare_composition()).
    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);
    Render_view&                      render_view = get_render_view();
    const std::shared_ptr<Scene_root> scene_root  = render_view.scene_root;
    if (!scene_root || !m_camera || (m_viewport.width < 1) || (m_viewport.height < 1)) {
        return;
    }

    // The camera as the copy the constructor took (main thread, at the
    // request); the render phase reads no live camera.
    const erhe::scene_renderer::Camera_view_input single_view_input{
        .camera      = get_render_camera(),
        .viewport    = m_viewport,
        .pixel_scale = (m_context.context_window != nullptr) ? m_context.context_window->get_scale_factor() : 1.0f
    };
    Render_context context{
        .command_buffer      = &command_buffer,
        .encoder             = nullptr,
        .app_context         = m_context,
        .scene_view          = *this,
        .viewport_config     = render_view.viewport_config,
        .camera              = nullptr,
        .camera_snapshot     = get_render_camera(),
        .viewport_scene_view = nullptr,
        .scene_root          = scene_root,
        .viewport            = m_viewport,
        .shader_debug        = m_shader_debug,
        .shadow_debug_light_index = resolve_shadow_debug_light_index(),
        .views               = std::span<const erhe::scene_renderer::Camera_view_input>(&single_view_input, 1),
        .content             = m_content,
        // The composition the prepare phase decided for this frame.
        .composer_prepared   = &m_composer_slots.get_prepared(m_rendergraph.get_graphics_device().get_frame_index())
    };
    copy_shadow_projections();

    erhe::graphics::Device& graphics_device = m_rendergraph.get_graphics_device();

    // Physically-based sky: the atmosphere LUTs are generated outside the
    // render pass (compute + image barriers), as Viewport_scene_view does.
    if (m_context.sky_renderer != nullptr) {
        const int sky_mode = scene_root->get_render_settings(graphics_device.get_frame_index()).sky.mode;
        if (sky_mode == 1) {
            m_context.sky_renderer->ensure_luts(graphics_device, command_buffer);
        }
    }

    // Sized at construction for the request's fixed size.
    ERHE_VERIFY(m_render_target.get_render_pass() != nullptr);

    if (m_forward_pass_timer != nullptr) {
        m_forward_pass_timer->begin(command_buffer);
    }
    {
        erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
        erhe::graphics::Scoped_render_pass scoped_render_pass{*m_render_target.get_render_pass(), command_buffer};
        context.encoder     = &encoder;
        context.render_pass = m_render_target.get_render_pass();

        // Content passes only; the overlay passes (tool / rendertarget meshes)
        // are editor UI.
        m_context.app_rendering->render_viewport_main(context, false);
    }
    if (m_forward_pass_timer != nullptr) {
        m_forward_pass_timer->end(command_buffer);
    }
    m_has_rendered = true;
}

// ---------------------------------------------------------------------------
// Scene_image_readback_node

Scene_image_readback_node::Scene_image_readback_node(
    erhe::rendergraph::Rendergraph& rendergraph,
    App_context&                    context,
    Scene_image_capture&            capture,
    Scene_image_view&               view,
    Post_processing_node* const     post_processing_node,
    const int                       width,
    const int                       height,
    const erhe::dataformat::Format  color_format,
    const Scene_image_output        output
)
    : erhe::rendergraph::Rendergraph_node{rendergraph, erhe::utility::Debug_label{"Scene image readback"}}
    , m_context             {context}
    , m_capture             {&capture}
    , m_view                {view}
    , m_post_processing_node{post_processing_node}
    , m_width               {width}
    , m_height              {height}
    , m_color_format        {color_format}
    , m_output              {output}
{
    register_input("texture", erhe::rendergraph::Rendergraph_node_key::viewport_texture);
}

Scene_image_readback_node::~Scene_image_readback_node() noexcept = default;

void Scene_image_readback_node::prepare_rendergraph_node(const uint64_t frame_index)
{
    static_cast<void>(frame_index);
    ERHE_VERIFY(m_capture != nullptr);
    m_capture->prepare_chain();
}

void Scene_image_readback_node::on_released()
{
    m_capture = nullptr;
}

void Scene_image_readback_node::execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    erhe::utility::Thread_roles::assert_render_thread();
    // The view and the post-processing node are named by every frame that
    // names this node (the capture registers and releases the chain
    // together), so a retired chain is alive for the frame executing it.
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (m_record.state != Scene_image_capture_state::rendering) {
        return; // recorded in an earlier frame
    }

    if (!m_view.has_rendered()) {
        m_record.error = "the scene view did not render (scene closed or camera missing)";
        m_record.state = Scene_image_capture_state::failed;
        return;
    }

    const std::shared_ptr<Texture> texture = (m_output == Scene_image_output::png) && (m_post_processing_node != nullptr)
        ? m_post_processing_node->get_producer_output_texture(erhe::rendergraph::Rendergraph_node_key::viewport_texture)
        : m_view.get_render_target().get_color_texture();
    if (!texture) {
        m_record.error = "no image texture to read back";
        m_record.state = Scene_image_capture_state::failed;
        return;
    }
    if (
        (texture->get_width () != m_width ) ||
        (texture->get_height() != m_height) ||
        (texture->get_pixelformat() != m_color_format)
    ) {
        m_record.error = "image texture has an unexpected size or format";
        m_record.state = Scene_image_capture_state::failed;
        return;
    }

    const std::size_t bytes_per_row = static_cast<std::size_t>(m_width) * erhe::dataformat::get_format_size_bytes(m_color_format);
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(m_height);
    m_record.buffer = std::make_unique<Buffer>(
        *m_context.graphics_device,
        Buffer_create_info{
            .capacity_byte_count                    = byte_count,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::transfer_dst,
            .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read | Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_coherent | Memory_property_flag_bit_mask::host_persistent,
            .debug_label                            = erhe::utility::Debug_label{"Scene image readback"}
        }
    );
    {
        Blit_command_encoder blit = m_context.graphics_device->make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(
            erhe::graphics::Texture_location{.texture = texture.get(), .slice = 0, .level = 0, .origin = glm::ivec3{0, 0, 0}},
            glm::ivec3{m_width, m_height, 1},
            erhe::graphics::Buffer_texel_location{.buffer = m_record.buffer.get(), .offset = 0, .bytes_per_row = static_cast<std::uintptr_t>(bytes_per_row), .bytes_per_image = static_cast<std::uintptr_t>(byte_count)}
        );
    }
    // Transfer writes -> host reads once the frame's fence has signalled.
    command_buffer.memory_barrier(Memory_barrier_mask::client_mapped_buffer_barrier_bit);

    // What the view's render of this frame reported, copied with the image:
    // a later frame of the chain may render the view again.
    m_record.shadow_debug_light_index = m_view.get_shadow_debug_light_index();
    m_record.shadow_lights.assign(m_view.get_shadow_lights().begin(), m_view.get_shadow_lights().end());
    m_record.shadow_maps              = m_view.get_shadow_maps();
    m_record.frame                    = m_context.graphics_device->get_frame_index();
    m_record.state                    = Scene_image_capture_state::in_flight;
}

// ---------------------------------------------------------------------------
// Scene_image_capture

Scene_image_capture::Scene_image_capture(App_context& context, const Scene_image_capture_create_info& create_info)
    : m_context{context}
    , m_width  {create_info.width}
    , m_height {create_info.height}
    , m_output {create_info.output}
    , m_color_format{create_info.color_format}
    , m_background  {create_info.background}
{
    ERHE_VERIFY(m_context.rendergraph != nullptr);
    ERHE_VERIFY(m_context.graphics_device != nullptr);
    ERHE_VERIFY(m_context.app_rendering != nullptr);
    ERHE_VERIFY(m_context.app_settings != nullptr);
    ERHE_VERIFY(m_context.scene_views != nullptr);

    erhe::rendergraph::Rendergraph& rendergraph = *m_context.rendergraph;

    // The viewport config a new viewport window starts from
    // (default_viewport_config.json), with the editor-only primitive modes off:
    // polygon fill only, and selected items styled like unselected ones.
    Viewport_config viewport_config = make_viewport_config(m_context.scene_views->get_viewport_config_data());
    viewport_config.render_style_not_selected.edge_lines        = false;
    viewport_config.render_style_not_selected.solid_wireframe   = false;
    viewport_config.render_style_not_selected.polygon_centroids = false;
    viewport_config.render_style_not_selected.corner_points     = false;
    viewport_config.render_style_selected = viewport_config.render_style_not_selected;

    m_view = erhe::rendergraph::make_rendergraph_node<Scene_image_view>(
        m_context,
        rendergraph,
        viewport_config,
        create_info.scene_root,
        create_info.camera,
        create_info.width,
        create_info.height,
        create_info.msaa_sample_count,
        create_info.color_format,
        // marked: skip the sky so uncovered pixels keep the clear value
        // (alpha 0), which the readback turns into the NaN marker.
        (create_info.background == Scene_image_background::marked) ? Render_content::scene_surfaces : Render_content::scene_only,
        create_info.shader_debug,
        create_info.shadow_debug_light,
        create_info.forward_pass_timer
    );

    m_shadow_render_node = m_context.app_rendering->create_shadow_node_for_scene_view(
        *m_context.graphics_device,
        rendergraph,
        *m_context.app_settings,
        *m_view
    );
    rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::shadow_maps, m_shadow_render_node.get(), m_view.get());

    // --no-post-processing: viewports show the HDR scene color directly, so
    // the png output does too. The linear output reads the scene color itself,
    // so its chain has no post-processing node.
    const bool post_processing =
        (m_output == Scene_image_output::png) &&
        (m_context.post_processing != nullptr) &&
        !m_context.force_post_processing_off;
    if (post_processing) {
        m_post_processing_node = erhe::rendergraph::make_rendergraph_node<Post_processing_node>(
            *m_context.graphics_device,
            rendergraph,
            *m_context.post_processing,
            erhe::utility::Debug_label{"Post processing for scene image"}
        );
    }

    m_readback_node = erhe::rendergraph::make_rendergraph_node<Scene_image_readback_node>(
        rendergraph,
        m_context,
        *this,
        *m_view,
        m_post_processing_node.get(),
        m_width,
        m_height,
        m_color_format,
        m_output
    );

    if (post_processing) {
        rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::viewport_texture, m_view.get(), m_post_processing_node.get());
        rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::viewport_texture, m_post_processing_node.get(), m_readback_node.get());
        // The node sizes its pyramid from the view's content texture when it
        // executes (Post_processing_node::update_size()).
    } else {
        rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::viewport_texture, m_view.get(), m_readback_node.get());
    }
    log_render->info("Scene image capture: created chain for {} x {}", m_width, m_height);
}

Scene_image_capture::~Scene_image_capture() noexcept
{
    if (m_shadow_render_node) {
        static_cast<void>(m_context.app_rendering->destroy_shadow_node(m_shadow_render_node));
    }
    // Rendergraph_node destructors unregister the nodes (and their links).
    m_readback_node.reset();
    m_post_processing_node.reset();
    m_view.reset();
    m_shadow_render_node.reset();
    log_render->info("Scene image capture: destroyed chain");
}

void Scene_image_capture::prepare_chain()
{
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);
    const bool recorded = m_readback_node->with_record(
        [](const Scene_image_readback_record& record) -> bool {
            return record.state != Scene_image_capture_state::rendering;
        }
    );
    if (recorded) {
        disable_chain();
    }
}

void Scene_image_capture::disable_chain()
{
    if (m_shadow_render_node  ) { m_shadow_render_node  ->set_enabled(false); }
    if (m_view                ) { m_view                ->set_enabled(false); }
    if (m_post_processing_node) { m_post_processing_node->set_enabled(false); }
    if (m_readback_node       ) { m_readback_node       ->set_enabled(false); }
}

auto Scene_image_capture::poll() -> Scene_image_capture_state
{
    if ((m_state == Scene_image_capture_state::complete) || (m_state == Scene_image_capture_state::failed)) {
        return m_state;
    }

    // The render phase's record, under the readback node's mutex: the copy
    // is read once the frame that recorded it has retired on the GPU.
    m_readback_node->with_record(
        [this](Scene_image_readback_record& record) {
            m_state = record.state;
            if (record.state == Scene_image_capture_state::failed) {
                m_error = record.error;
                return;
            }
            if ((record.state != Scene_image_capture_state::in_flight) || !m_context.graphics_device->is_frame_completed(record.frame)) {
                return;
            }
            read_pixels(*record.buffer);
            record.buffer.reset();
            m_shadow_debug_light_index = record.shadow_debug_light_index;
            m_shadow_lights            = record.shadow_lights;
            m_shadow_maps              = record.shadow_maps;
        }
    );
    return m_state;
}

void Scene_image_capture::read_pixels(erhe::graphics::Buffer& readback_buffer)
{
    const bool        fp32            = (m_color_format == erhe::dataformat::Format::format_32_vec4_float);
    const std::size_t bytes_per_pixel = erhe::dataformat::get_format_size_bytes(m_color_format);
    const std::size_t pixel_count     = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
    const std::size_t byte_count      = pixel_count * bytes_per_pixel;
    const std::span<std::byte> mapped = readback_buffer.map_bytes(0, byte_count);
    readback_buffer.invalidate(0, byte_count);
    if (mapped.size() < byte_count) {
        readback_buffer.unmap();
        m_error = "readback buffer could not be mapped";
        m_state = Scene_image_capture_state::failed;
        return;
    }

    // Rows top to bottom: a bottom-left texture origin (OpenGL) stores the
    // image bottom-up (see Imgui_renderer::get_rtt_uv0()).
    const bool bottom_left = (m_context.graphics_device->get_info().coordinate_conventions.texture_origin == erhe::math::Texture_origin::bottom_left);
    // Background marker: the render cleared the color target to (0, 0, 0, 0)
    // and skipped the sky, and every surface writes alpha > 0 (opaque 1,
    // blended surfaces their alpha), so alpha 0 is a pixel no surface covered.
    const bool  mark_background = (m_background == Scene_image_background::marked);
    const float nan             = std::numeric_limits<float>::quiet_NaN();
    m_background_pixel_count = 0;
    m_pixels.resize(pixel_count);
    for (int y = 0; y < m_height; ++y) {
        const int source_row = bottom_left ? (m_height - 1 - y) : y;
        const std::byte* source = mapped.data() + (static_cast<std::size_t>(source_row) * static_cast<std::size_t>(m_width) * bytes_per_pixel);
        for (int x = 0; x < m_width; ++x) {
            glm::vec4 pixel{0.0f};
            if (fp32) {
                std::memcpy(&pixel[0], source + (static_cast<std::size_t>(x) * bytes_per_pixel), 4 * sizeof(float));
            } else {
                uint16_t half[4];
                std::memcpy(half, source + (static_cast<std::size_t>(x) * bytes_per_pixel), sizeof(half));
                pixel = glm::vec4{
                    glm::unpackHalf1x16(half[0]),
                    glm::unpackHalf1x16(half[1]),
                    glm::unpackHalf1x16(half[2]),
                    glm::unpackHalf1x16(half[3])
                };
            }
            if (mark_background && (pixel.a == 0.0f)) {
                pixel = glm::vec4{nan, nan, nan, 0.0f};
                ++m_background_pixel_count;
            }
            m_pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width)) + static_cast<std::size_t>(x)] = pixel;
        }
    }
    readback_buffer.unmap();
    m_state = Scene_image_capture_state::complete;
}

auto Scene_image_capture::get_shadow_debug_light_index() const -> std::optional<uint32_t>
{
    return m_shadow_debug_light_index;
}

auto Scene_image_capture::get_shadow_lights() const -> std::span<const Scene_image_shadow_light>
{
    return std::span<const Scene_image_shadow_light>{m_shadow_lights};
}

auto Scene_image_capture::get_shadow_maps() const -> const Scene_image_shadow_maps&
{
    return m_shadow_maps;
}

auto Scene_image_capture::get_pixels() const -> std::span<const glm::vec4>
{
    return std::span<const glm::vec4>{m_pixels};
}

} // namespace editor
