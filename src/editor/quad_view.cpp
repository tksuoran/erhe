#include "quad_view.hpp"

#include "app_context.hpp"
#include "rendertarget_imgui_host.hpp"
#include "rendertarget_mesh.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_item/item.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_utility/debug_label.hpp"
#include "erhe_verify/verify.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/generated/headset_config.hpp"
#   include "erhe_xr/headset.hpp"
#   include "erhe_xr/xr.hpp"
#   include "erhe_xr/xr_quad_layer.hpp"
#endif

#include <glm/gtc/matrix_inverse.hpp>

#include <string>

namespace editor {

Quad_view::Quad_view(
    App_context&                       context,
    erhe::graphics::Device&            graphics_device,
    erhe::graphics::Command_buffer&    command_buffer,
    erhe::scene_renderer::Mesh_memory& mesh_memory,
    erhe::imgui::Imgui_renderer&       imgui_renderer,
    erhe::rendergraph::Rendergraph&    rendergraph,
    Scene_root&                        scene_root,
    Headset_view*                      headset_view,
    int                                width,
    int                                height,
    float                              pixels_per_meter,
    std::string_view                   debug_label,
    const bool                         imgui_ini
)
    : m_context     {context}
    , m_headset_view{headset_view}
    , m_pixel_width {width}
    , m_pixel_height{height}
    , m_local_width {static_cast<float>(width)  / pixels_per_meter}
    , m_local_height{static_cast<float>(height) / pixels_per_meter}
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    // Path B: use an OpenXR quad composition layer when a headset session is
    // present, its config enables composition quad layers, and the quad
    // swapchain can be created. Otherwise fall back to the scene-mesh path.
    // (get_headset() is non-null only when an OpenXR session is active, so this
    // is implicitly false on desktop.)
    if (headset_view != nullptr) {
        erhe::xr::Headset* headset = headset_view->get_headset();
        if ((headset != nullptr) && headset->get_configuration().composition_quad_layers) {
            m_quad_layer = headset->create_quad_layer(
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height),
                std::string{debug_label}
            );
            if (m_quad_layer && m_quad_layer->is_valid()) {
                m_uses_composition_layer = true;
            } else {
                m_quad_layer.reset();
            }
        }
    }
#endif

    if (!m_uses_composition_layer) {
        // Path A: scene-mesh rendertarget.
        m_rendertarget_mesh = std::make_shared<Rendertarget_mesh>(
            graphics_device,
            command_buffer,
            mesh_memory,
            width,
            height,
            pixels_per_meter
        );
        m_rendertarget_mesh->layer_id = scene_root.layers().rendertarget()->id;
        m_rendertarget_mesh->enable_flag_bits(
            erhe::Item_flags::visible |
            erhe::Item_flags::show_in_developer_ui
        );

        m_rendertarget_node = std::make_shared<erhe::scene::Node>(std::string{debug_label} + " RT node");
        m_rendertarget_node->attach(m_rendertarget_mesh);
        m_rendertarget_node->enable_flag_bits(erhe::Item_flags::show_in_developer_ui);
    }

    m_imgui_host = std::make_shared<Rendertarget_imgui_host>(
        imgui_renderer,
        rendergraph,
        context,
        *this,
        erhe::utility::Debug_label{debug_label},
        imgui_ini
    );
}

Quad_view::~Quad_view() noexcept = default;

auto Quad_view::uses_composition_layer() const -> bool
{
    return m_uses_composition_layer;
}

auto Quad_view::get_imgui_host() const -> Rendertarget_imgui_host*
{
    return m_imgui_host.get();
}

auto Quad_view::get_imgui_host_shared() const -> std::shared_ptr<Rendertarget_imgui_host>
{
    return m_imgui_host;
}

auto Quad_view::get_rendertarget_mesh() const -> Rendertarget_mesh*
{
    return m_rendertarget_mesh.get();
}

auto Quad_view::get_node() const -> erhe::scene::Node*
{
    return m_rendertarget_node.get();
}

auto Quad_view::get_world_from_quad() const -> glm::mat4
{
    return m_world_from_quad;
}

auto Quad_view::intersect_ray(glm::vec3 origin_in_world, glm::vec3 direction_in_world) const -> std::optional<glm::vec3>
{
    // Quad rectangle lies in the local XY plane (z = 0), centered, spanning
    // [-local_width/2, local_width/2] x [-local_height/2, local_height/2]. This
    // matches world_to_window()'s convention.
    const glm::mat4 quad_from_world  = glm::inverse(m_world_from_quad);
    const glm::vec3 origin_in_quad   = glm::vec3{quad_from_world * glm::vec4{origin_in_world,    1.0f}};
    const glm::vec3 direction_in_quad= glm::vec3{quad_from_world * glm::vec4{direction_in_world, 0.0f}};
    const std::optional<float> hit = erhe::math::intersect_plane<float>(
        glm::vec3{0.0f, 0.0f, 1.0f},
        glm::vec3{0.0f, 0.0f, 0.0f},
        origin_in_quad,
        direction_in_quad
    );
    if (!hit.has_value() || (hit.value() < 0.0f)) {
        return {};
    }
    const glm::vec3 position_in_quad = origin_in_quad + hit.value() * direction_in_quad;
    const float half_width  = 0.5f * m_local_width;
    const float half_height = 0.5f * m_local_height;
    if (
        (position_in_quad.x < -half_width ) || (position_in_quad.x > half_width ) ||
        (position_in_quad.y < -half_height) || (position_in_quad.y > half_height)
    ) {
        return {};
    }
    return glm::vec3{m_world_from_quad * glm::vec4{position_in_quad, 1.0f}};
}

#if defined(ERHE_XR_LIBRARY_OPENXR)
namespace {

// Convert a scene-world transform into a stage-space XrPosef for the quad layer.
// Scene-world equals the XR stage reference space offset by the headset view's
// camera offset (every XR pose adds it; the projection layer omits it), so the
// offset is subtracted here to match the projection layer's reference frame.
[[nodiscard]] auto stage_pose_from_world(const glm::mat4& world_from_quad, const glm::vec3& camera_offset) -> XrPosef
{
    glm::mat4 stage_from_quad = world_from_quad;
    stage_from_quad[3] -= glm::vec4{camera_offset, 0.0f};
    return erhe::xr::from_glm(stage_from_quad);
}

} // namespace
#endif

void Quad_view::apply_transform()
{
    // Effective transform = base * uniform scale about the quad center. Built
    // explicitly to avoid depending on glm transform headers.
    glm::mat4 scale_matrix{1.0f};
    scale_matrix[0][0] = m_scale;
    scale_matrix[1][1] = m_scale;
    scale_matrix[2][2] = m_scale;
    m_world_from_quad = m_base_world_from_quad * scale_matrix;

    if (m_rendertarget_node) {
        m_rendertarget_node->set_world_from_node(m_world_from_quad);
    }
#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (m_quad_layer) {
        const glm::vec3 camera_offset = (m_headset_view != nullptr) ? m_headset_view->get_camera_offset() : glm::vec3{0.0f};
        // The quad layer takes an (unscaled) pose plus an explicit size, so the
        // scale goes into the size rather than the pose matrix.
        m_quad_layer->set_pose(stage_pose_from_world(m_base_world_from_quad, camera_offset));
        m_quad_layer->set_size(XrExtent2Df{m_local_width * m_scale, m_local_height * m_scale});
    }
#endif
}

void Quad_view::set_world_from_quad(const std::shared_ptr<erhe::scene::Node>& parent_node, const glm::mat4& world_from_quad)
{
    m_base_world_from_quad = world_from_quad;
    if (m_rendertarget_node) {
        // set_parent with a null parent detaches the node; callers rely on this
        // (e.g. Hotbar detaches when there is no valid hovered scene view).
        m_rendertarget_node->set_parent(parent_node);
    }
    apply_transform();
}

void Quad_view::set_world_from_node(const glm::mat4& world_from_quad)
{
    m_base_world_from_quad = world_from_quad;
    apply_transform();
}

void Quad_view::set_scale(float scale)
{
    m_scale = scale;
    apply_transform();
}

void Quad_view::set_visible(bool visible)
{
    if (m_rendertarget_node) {
        m_rendertarget_node->set_visible(visible);
    }
    if (m_rendertarget_mesh) {
        m_rendertarget_mesh->set_visible(visible);
    }
#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (m_quad_layer) {
        m_quad_layer->set_visible(visible);
    }
#endif
    if (m_imgui_host) {
        m_imgui_host->set_enabled(visible);
    }
}

void Quad_view::set_double_sided(bool double_sided)
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    // Path B only: the quad layer submits a back-facing copy. In Path A the
    // scene-mesh rendertarget's two-sidedness is governed by its material,
    // which this does not change.
    if (m_quad_layer) {
        m_quad_layer->set_double_sided(double_sided);
    }
#else
    static_cast<void>(double_sided);
#endif
}

void Quad_view::set_depth_tested(bool depth_tested)
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (m_quad_layer) {
        m_quad_layer->set_depth_tested(depth_tested);
    }
#else
    static_cast<void>(depth_tested);
#endif
}

void Quad_view::set_composition_order(int order)
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (m_quad_layer) {
        m_quad_layer->set_composition_order(order);
    }
#else
    static_cast<void>(order);
#endif
}

auto Quad_view::get_width() const -> float
{
    return static_cast<float>(m_pixel_width);
}

auto Quad_view::get_height() const -> float
{
    return static_cast<float>(m_pixel_height);
}

auto Quad_view::acquire_render_pass(erhe::graphics::Command_buffer&) -> erhe::graphics::Render_pass*
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (m_uses_composition_layer) {
        if (!m_quad_layer) {
            return nullptr;
        }
        erhe::graphics::Texture* texture = m_quad_layer->acquire();
        if (texture == nullptr) {
            return nullptr;
        }
        const auto it = m_quad_render_passes.find(texture);
        if (it != m_quad_render_passes.end()) {
            return it->second.get();
        }
        // Build a render pass over this swapchain image. ImGui is composited
        // over passthrough / the projection layer, so clear to transparent and
        // rely on per-element ImGui alpha. OpenXR Vulkan2 runtimes hand the
        // color image to the application in COLOR_ATTACHMENT_OPTIMAL and expect
        // it back in the same layout.
        erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
        render_pass_descriptor.color_attachments[0].texture       = texture;
        render_pass_descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.color_attachments[0].clear_value[0] = 0.0f;
        render_pass_descriptor.color_attachments[0].clear_value[1] = 0.0f;
        render_pass_descriptor.color_attachments[0].clear_value[2] = 0.0f;
        render_pass_descriptor.color_attachments[0].clear_value[3] = 0.0f;
        render_pass_descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        render_pass_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
        render_pass_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::color_attachment_optimal;
        render_pass_descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
        render_pass_descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::color_attachment_optimal;
        render_pass_descriptor.render_target_width                = m_pixel_width;
        render_pass_descriptor.render_target_height               = m_pixel_height;
        render_pass_descriptor.debug_label                        = "Quad composition layer";
        std::shared_ptr<erhe::graphics::Render_pass> render_pass = std::make_shared<erhe::graphics::Render_pass>(*m_context.graphics_device, render_pass_descriptor);
        erhe::graphics::Render_pass* result = render_pass.get();
        m_quad_render_passes.emplace(texture, std::move(render_pass));
        return result;
    }
#endif
    return (m_rendertarget_mesh != nullptr) ? m_rendertarget_mesh->get_render_pass() : nullptr;
}

void Quad_view::finish_render(erhe::graphics::Command_buffer& command_buffer, App_context& context)
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (m_uses_composition_layer) {
        if (m_quad_layer) {
            m_quad_layer->release();
        }
        return;
    }
#endif
    if (m_rendertarget_mesh != nullptr) {
        m_rendertarget_mesh->render_done(command_buffer, context);
    }
}

auto Quad_view::get_output_texture() const -> std::shared_ptr<erhe::graphics::Texture>
{
    // Path B does not expose a texture through the rendergraph: the quad layer
    // is composited directly by the runtime and the rendergraph edge to the
    // headset node exists only for execution ordering.
    return (m_rendertarget_mesh != nullptr) ? m_rendertarget_mesh->get_texture() : nullptr;
}

auto Quad_view::is_quad_visible() const -> bool
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (m_uses_composition_layer) {
        return (m_quad_layer != nullptr) && m_quad_layer->is_visible();
    }
#endif
    return (m_rendertarget_node != nullptr) && m_rendertarget_node->is_visible();
}

auto Quad_view::get_pointer() const -> std::optional<glm::vec2>
{
    // Path B has no desktop pointer; the controller ray drives the mouse via
    // world_to_window() in the host's OpenXR branch.
    return (m_rendertarget_mesh != nullptr) ? m_rendertarget_mesh->get_pointer() : std::nullopt;
}

auto Quad_view::world_to_window(glm::vec3 world_position) const -> std::optional<glm::vec2>
{
    // Not guarded by ERHE_XR_LIBRARY_OPENXR: m_uses_composition_layer is always
    // false without OpenXR, and this branch only reads members that exist in
    // every build, so it is dead-but-valid there.
    if (m_uses_composition_layer) {
        // Mirror Rendertarget_mesh::get_world_to_window() against the quad's
        // world transform: map the world point into the quad's local plane,
        // normalize by the quad size, and convert to texture pixel coords.
        const glm::mat4 quad_from_world = glm::inverse(m_world_from_quad);
        const glm::vec3 position_in_quad = glm::vec3{quad_from_world * glm::vec4{world_position, 1.0f}};
        const glm::vec2 a{
            position_in_quad.x / m_local_width,
            position_in_quad.y / m_local_height
        };
        const glm::vec2 b{
             a.x + 0.5f,
            -a.y + 0.5f
        };
        if ((b.x < 0.0f) || (b.y < 0.0f) || (b.x > 1.0f) || (b.y > 1.0f)) {
            return {};
        }
        return glm::vec2{
            static_cast<float>(m_pixel_width)  * b.x,
            static_cast<float>(m_pixel_height) * b.y
        };
    }
    return (m_rendertarget_mesh != nullptr) ? m_rendertarget_mesh->get_world_to_window(world_position) : std::nullopt;
}

auto Quad_view::get_plane_world_origin() const -> glm::vec3
{
    if (m_uses_composition_layer) {
        return glm::vec3{m_world_from_quad[3]};
    }
    return (m_rendertarget_node != nullptr) ? glm::vec3{m_rendertarget_node->position_in_world()} : glm::vec3{0.0f};
}

auto Quad_view::get_plane_world_normal() const -> glm::vec3
{
    if (m_uses_composition_layer) {
        return glm::normalize(glm::vec3{m_world_from_quad[2]});
    }
    return (m_rendertarget_node != nullptr) ? glm::vec3{m_rendertarget_node->direction_in_world()} : glm::vec3{0.0f, 1.0f, 0.0f};
}

#if defined(ERHE_XR_LIBRARY_OPENXR)
void Quad_view::update_headset_hand_tracking()
{
    if (m_rendertarget_mesh != nullptr) {
        m_rendertarget_mesh->update_headset_hand_tracking();
    }
}
#endif

} // namespace editor
