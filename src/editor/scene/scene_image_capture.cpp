#include "scene/scene_image_capture.hpp"

#include "app_context.hpp"
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
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_window/window.hpp"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cstring>

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
    const erhe::scene_renderer::Shader_debug    shader_debug
)
    // No settings store: nothing about this view persists.
    : Scene_view{context, nullptr, "", viewport_config}
    , Texture_rendergraph_node{
        erhe::rendergraph::Texture_rendergraph_node_create_info{
            .rendergraph          = rendergraph,
            .debug_label          = erhe::utility::Debug_label{"Scene image view"},
            .output_key           = erhe::rendergraph::Rendergraph_node_key::viewport_texture,
            .color_format         = erhe::dataformat::Format::format_16_vec4_float,
            .depth_stencil_format = erhe::dataformat::Format::format_d32_sfloat_s8_uint,
            .sample_count         = msaa_sample_count
        }
    }
    , m_camera      {camera}
    , m_viewport    {0, 0, width, height}
    , m_shader_debug{shader_debug}
{
    set_scene_root(scene_root);
    register_input("shadow_maps", erhe::rendergraph::Rendergraph_node_key::shadow_maps);
}

Scene_image_view::~Scene_image_view() noexcept = default;

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

void Scene_image_view::execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    ERHE_PROFILE_FUNCTION();

    const std::shared_ptr<Scene_root> scene_root = get_scene_root();
    if (!scene_root || !m_camera || (m_viewport.width < 1) || (m_viewport.height < 1)) {
        return;
    }

    const erhe::scene_renderer::Camera_view_input single_view_input{
        .projection  = m_camera->projection(),
        .node        = m_camera.get(),
        .viewport    = m_viewport,
        .pixel_scale = (m_context.context_window != nullptr) ? m_context.context_window->get_scale_factor() : 1.0f
    };
    Render_context context{
        .command_buffer      = &command_buffer,
        .encoder             = nullptr,
        .app_context         = m_context,
        .scene_view          = *this,
        .viewport_config     = m_viewport_config,
        .camera              = m_camera.get(),
        .viewport_scene_view = nullptr,
        .viewport            = m_viewport,
        .shader_debug        = m_shader_debug,
        .views               = std::span<const erhe::scene_renderer::Camera_view_input>(&single_view_input, 1),
        .content             = Render_content::scene_only
    };

    erhe::graphics::Device& graphics_device = m_rendergraph.get_graphics_device();

    // Physically-based sky: the atmosphere LUTs are generated outside the
    // render pass (compute + image barriers), as Viewport_scene_view does.
    if ((m_context.sky_renderer != nullptr) && (m_context.editor_settings != nullptr)) {
        const int sky_mode = get_effective_sky(*m_context.editor_settings, *scene_root).mode;
        if (sky_mode == 1) {
            m_context.sky_renderer->ensure_luts(graphics_device, command_buffer);
        }
    }

    m_render_target.update(m_viewport.width, m_viewport.height, nullptr);
    ERHE_VERIFY(m_render_target.get_render_pass() != nullptr);

    erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
    erhe::graphics::Scoped_render_pass scoped_render_pass{*m_render_target.get_render_pass(), command_buffer};
    context.encoder     = &encoder;
    context.render_pass = m_render_target.get_render_pass();

    // Content passes only; the overlay passes (tool / rendertarget meshes)
    // are editor UI.
    m_context.app_rendering->render_viewport_main(context, false);
    m_has_rendered = true;
}

// ---------------------------------------------------------------------------
// Scene_image_readback_node

Scene_image_readback_node::Scene_image_readback_node(erhe::rendergraph::Rendergraph& rendergraph, Scene_image_capture& capture)
    : erhe::rendergraph::Rendergraph_node{rendergraph, erhe::utility::Debug_label{"Scene image readback"}}
    , m_capture{capture}
{
    register_input("texture", erhe::rendergraph::Rendergraph_node_key::viewport_texture);
}

void Scene_image_readback_node::execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    m_capture.record_readback(command_buffer);
}

// ---------------------------------------------------------------------------
// Scene_image_capture

Scene_image_capture::Scene_image_capture(App_context& context, const Scene_image_capture_create_info& create_info)
    : m_context{context}
    , m_width  {create_info.width}
    , m_height {create_info.height}
    , m_output {create_info.output}
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

    m_view = std::make_shared<Scene_image_view>(
        m_context,
        rendergraph,
        viewport_config,
        create_info.scene_root,
        create_info.camera,
        create_info.width,
        create_info.height,
        create_info.msaa_sample_count,
        create_info.shader_debug
    );

    m_shadow_render_node = m_context.app_rendering->create_shadow_node_for_scene_view(
        *m_context.graphics_device,
        rendergraph,
        *m_context.app_settings,
        *m_view
    );
    rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::shadow_maps, m_shadow_render_node.get(), m_view.get());

    m_readback_node = std::make_shared<Scene_image_readback_node>(rendergraph, *this);

    // --no-post-processing: viewports show the HDR scene color directly, so
    // the png output does too.
    const bool post_processing = (m_context.post_processing != nullptr) && !m_context.force_post_processing_off;
    if (post_processing) {
        m_post_processing_node = std::make_shared<Post_processing_node>(
            *m_context.graphics_device,
            rendergraph,
            *m_context.post_processing,
            erhe::utility::Debug_label{"Post processing for scene image"}
        );
        rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::viewport_texture, m_view.get(), m_post_processing_node.get());
        rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::viewport_texture, m_post_processing_node.get(), m_readback_node.get());
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

void Scene_image_capture::disable_chain()
{
    // One frame is all a capture needs; later frames skip the nodes until the
    // capture is destroyed.
    if (m_shadow_render_node  ) { m_shadow_render_node  ->set_enabled(false); }
    if (m_view                ) { m_view                ->set_enabled(false); }
    if (m_post_processing_node) { m_post_processing_node->set_enabled(false); }
    if (m_readback_node       ) { m_readback_node       ->set_enabled(false); }
}

void Scene_image_capture::record_readback(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    if (m_state != Scene_image_capture_state::rendering) {
        return;
    }
    disable_chain();

    if (!m_view->has_rendered()) {
        m_error = "the scene view did not render (scene closed or camera missing)";
        m_state = Scene_image_capture_state::failed;
        return;
    }

    const std::shared_ptr<Texture> texture = (m_output == Scene_image_output::png) && m_post_processing_node
        ? m_post_processing_node->get_producer_output_texture(erhe::rendergraph::Rendergraph_node_key::viewport_texture)
        : m_view->get_render_target().get_color_texture();
    if (!texture) {
        m_error = "no image texture to read back";
        m_state = Scene_image_capture_state::failed;
        return;
    }
    if (
        (texture->get_width () != m_width ) ||
        (texture->get_height() != m_height) ||
        (texture->get_pixelformat() != erhe::dataformat::Format::format_16_vec4_float)
    ) {
        m_error = "image texture has an unexpected size or format";
        m_state = Scene_image_capture_state::failed;
        return;
    }

    const std::size_t bytes_per_row = static_cast<std::size_t>(m_width) * erhe::dataformat::get_format_size_bytes(erhe::dataformat::Format::format_16_vec4_float);
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(m_height);
    m_readback_buffer = std::make_unique<Buffer>(
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
            texture.get(),
            0,                                    // source_slice
            0,                                    // source_level
            glm::ivec3{0, 0, 0},                  // source_origin
            glm::ivec3{m_width, m_height, 1},     // source_size
            m_readback_buffer.get(),              // destination_buffer
            0,                                    // destination_offset
            static_cast<std::uintptr_t>(bytes_per_row),
            static_cast<std::uintptr_t>(byte_count)
        );
    }
    // Transfer writes -> host reads once the frame's fence has signalled.
    command_buffer.memory_barrier(Memory_barrier_mask::client_mapped_buffer_barrier_bit);

    m_readback_frame = m_context.graphics_device->get_frame_index();
    m_state          = Scene_image_capture_state::in_flight;
}

auto Scene_image_capture::poll() -> Scene_image_capture_state
{
    if ((m_state != Scene_image_capture_state::in_flight) || !m_context.graphics_device->is_frame_completed(m_readback_frame)) {
        return m_state;
    }

    const std::size_t pixel_count = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
    const std::size_t byte_count  = pixel_count * 4 * sizeof(uint16_t);
    const std::span<std::byte> mapped = m_readback_buffer->map_bytes(0, byte_count);
    m_readback_buffer->invalidate(0, byte_count);
    if (mapped.size() < byte_count) {
        m_readback_buffer->unmap();
        m_error = "readback buffer could not be mapped";
        m_state = Scene_image_capture_state::failed;
        return m_state;
    }

    // Rows top to bottom: a bottom-left texture origin (OpenGL) stores the
    // image bottom-up (see Imgui_renderer::get_rtt_uv0()).
    const bool bottom_left = (m_context.graphics_device->get_info().coordinate_conventions.texture_origin == erhe::math::Texture_origin::bottom_left);
    m_pixels.resize(pixel_count);
    for (int y = 0; y < m_height; ++y) {
        const int source_row = bottom_left ? (m_height - 1 - y) : y;
        const std::byte* source = mapped.data() + (static_cast<std::size_t>(source_row) * static_cast<std::size_t>(m_width) * 4 * sizeof(uint16_t));
        for (int x = 0; x < m_width; ++x) {
            uint16_t half[4];
            std::memcpy(half, source + (static_cast<std::size_t>(x) * sizeof(half)), sizeof(half));
            m_pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width)) + static_cast<std::size_t>(x)] = glm::vec4{
                glm::unpackHalf1x16(half[0]),
                glm::unpackHalf1x16(half[1]),
                glm::unpackHalf1x16(half[2]),
                glm::unpackHalf1x16(half[3])
            };
        }
    }
    m_readback_buffer->unmap();
    m_readback_buffer.reset();
    m_state = Scene_image_capture_state::complete;
    return m_state;
}

auto Scene_image_capture::get_pixels() const -> std::span<const glm::vec4>
{
    return std::span<const glm::vec4>{m_pixels};
}

} // namespace editor
