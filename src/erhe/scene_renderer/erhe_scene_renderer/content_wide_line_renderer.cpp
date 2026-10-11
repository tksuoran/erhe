#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_scene_renderer/content_wide_line_interface.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

Content_wide_line_renderer::Content_wide_line_renderer(
    erhe::graphics::Device&      graphics_device,
    Content_wide_line_interface& interface_
)
    : m_graphics_device{graphics_device}
    , m_interface      {interface_}
    , m_view_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::uniform,
        "Content_wide_line_renderer::m_view_buffer",
        interface_.view_block.get_binding_point()
    }
{
}

Content_wide_line_renderer::~Content_wide_line_renderer() noexcept = default;

auto Content_wide_line_renderer::get_interface() -> Content_wide_line_interface&
{
    return m_interface;
}

auto Content_wide_line_renderer::get_frame_params() const -> const Dispatch_per_frame_params&
{
    ERHE_VERIFY(m_view_params_set);
    return m_frame_params;
}

auto Content_wide_line_renderer::get_joint_buffer_client() -> erhe::graphics::Ring_buffer_client*
{
    return m_joint_buffer_client;
}

auto Content_wide_line_renderer::get_joint_buffer_range() -> erhe::graphics::Ring_buffer_range&
{
    return m_joint_buffer_range;
}

void Content_wide_line_renderer::begin_frame()
{
    // The previous frame must have called end_frame(); both flags should
    // already be false and no joint range should be held.
    ERHE_VERIFY(!m_view_params_set);
    ERHE_VERIFY(!m_joint_buffer_set);
    m_per_view_cameras.clear();
}

void Content_wide_line_renderer::set_view_params(
    std::span<const Camera_view_input>        views,
    const Content_wide_line_tent_settings&    tent_settings,
    const bool                                reverse_depth,
    const erhe::math::Depth_range             depth_range,
    const erhe::math::Coordinate_conventions& conventions
)
{
    ERHE_VERIFY(!views.empty());
    ERHE_VERIFY(views.size() <= static_cast<std::size_t>(m_interface.view_count));

    // Build per-view camera data once for the whole frame. Pad the tail
    // with copies of the first entry when views is shorter than the
    // shader's compile-time view_count so the per-view loop in the
    // compute / vertex shader never reads uninitialised UBO entries.
    m_per_view_cameras.clear();
    m_per_view_cameras.reserve(static_cast<std::size_t>(m_interface.view_count));
    for (const Camera_view_input& view : views) {
        ERHE_VERIFY(view.camera != nullptr);
        m_per_view_cameras.push_back(build_per_view_camera(
            *view.camera, view.viewport, view.pixel_scale, reverse_depth, depth_range, conventions
        ));
    }
    while (m_per_view_cameras.size() < static_cast<std::size_t>(m_interface.view_count)) {
        m_per_view_cameras.push_back(m_per_view_cameras.front());
    }

    const bool  top_left  = (m_graphics_device.get_info().coordinate_conventions.framebuffer_origin == erhe::math::Framebuffer_origin::top_left);
    const float vp_y_sign = top_left ? -1.0f : 1.0f;

    // window_to_ndc_scale mirrors Debug_renderer: zero_to_one (reverse) depth has
    // window == ndc z (scale 1), minus_one_to_one has window = 0.5*ndc + 0.5
    // (scale 2). The tent measures the depth ULP in window space and converts
    // with this factor.
    m_frame_params = Dispatch_per_frame_params{
        .per_view_cameras     = m_per_view_cameras,
        .view_count_runtime   = static_cast<uint32_t>(views.size()),
        .vp_y_sign            = vp_y_sign,
        .clip_depth_direction = reverse_depth ? -1.0f : 1.0f,
        .line_bias_margin     = tent_settings.line_bias_margin,
        .window_to_ndc_scale  = reverse_depth ? 1.0f : 2.0f,
        .use_tent             = tent_settings.use_tent ? 1u : 0u,
        .line_bias_clamp      = tent_settings.line_bias_clamp,
    };
    m_view_params_set = true;
}

void Content_wide_line_renderer::set_joint_buffer(
    erhe::graphics::Ring_buffer_client* joint_buffer_client,
    erhe::graphics::Ring_buffer_range&& joint_buffer_range
)
{
    ERHE_VERIFY(!m_joint_buffer_set);
    m_joint_buffer_client = joint_buffer_client;
    m_joint_buffer_range  = std::move(joint_buffer_range);
    m_joint_buffer_set    = true;
}

void Content_wide_line_renderer::prepare_mesh(
    Mesh_memory&             mesh_memory,
    const erhe::scene::Mesh& mesh,
    const glm::vec4&         color,
    const float              line_width,
    const uint32_t           group,
    Content_wide_line_feed&  out
)
{
    ++out.mesh_count;

    const glm::mat4                           world_from_node  = mesh.world_from_node();
    const std::shared_ptr<erhe::scene::Skin>& skin             = mesh.skin;
    const bool                                mesh_is_skinned  = (skin != nullptr);
    const uint32_t                            base_joint_index = mesh_is_skinned ? skin->skin_data.joint_buffer_index : 0u;

    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh.get_primitives()) {
        if (!mesh_primitive.primitive) {
            continue;
        }
        // Edge lines come from the dedicated edge-line vertex buffer, which is
        // written from GEO mesh data and is independent of the main buffer's
        // vertex order - so the always-present variant is the right source, and
        // stays right even while an optimized variant is what gets drawn.
        const erhe::primitive::Buffer_mesh* buffer_mesh =
            mesh_primitive.primitive->get_renderable_mesh(erhe::primitive::Mesh_variant::original);
        if (buffer_mesh == nullptr) {
            continue;
        }
        const erhe::primitive::Buffer_range& edge_range = buffer_mesh->edge_line_vertex_buffer_range;
        if (edge_range.count == 0) {
            continue;
        }
        erhe::graphics::Buffer* edge_buffer = mesh_memory.get_vertex_buffer(edge_range);
        if (edge_buffer == nullptr) {
            continue;
        }
        Content_wide_line_record record{
            .edge_buffer             = edge_buffer,
            .edge_buffer_byte_offset = edge_range.byte_offset,
            .edge_count              = edge_range.count / 2,
            .world_from_node         = world_from_node,
            .color                   = color,
            .line_width              = line_width,
            .group                   = group,
            .mesh_is_skinned         = mesh_is_skinned,
            .base_joint_index        = base_joint_index
        };
        const erhe::primitive::Buffer_range& joint_range = buffer_mesh->edge_line_joint_buffer_range;
        if (mesh_is_skinned && (joint_range.count > 0)) {
            erhe::graphics::Buffer* joint_buffer = mesh_memory.get_vertex_buffer(joint_range);
            if (joint_buffer != nullptr) {
                record.joint_buffer             = joint_buffer;
                record.joint_buffer_byte_offset = joint_range.byte_offset;
                record.joint_buffer_byte_size   = joint_range.count * joint_range.element_size;
            }
        }
        out.records.push_back(record);
    }
}

void Content_wide_line_renderer::add_feed(const Content_wide_line_feed& feed)
{
    for (const Content_wide_line_record& record : feed.records) {
        add_record(record);
    }
}

void Content_wide_line_renderer::add_mesh(
    Mesh_memory&             mesh_memory,
    const erhe::scene::Mesh& mesh,
    const glm::vec4&         color,
    const float              line_width,
    const uint32_t           group
)
{
    // Leaf scratch (AGENTS.md): add_feed() does not re-enter add_mesh().
    static thread_local Content_wide_line_feed s_feed;
    s_feed.clear();
    prepare_mesh(mesh_memory, mesh, color, line_width, group, s_feed);
    add_feed(s_feed);
    s_feed.clear();
}

void Content_wide_line_renderer::end_frame()
{
    if (m_joint_buffer_set) {
        m_joint_buffer_range.release();
        m_joint_buffer_range  = erhe::graphics::Ring_buffer_range{};
        m_joint_buffer_client = nullptr;
        m_joint_buffer_set    = false;
    }
    m_view_params_set = false;
    release_backend_state();

    // No cross-frame memory_barrier is emitted here. Across device frames
    // the triangle vertex buffer is accessed through a Ring_buffer_client
    // that hands out disjoint byte ranges per dispatch: frame N writes
    // range R_N, frame N-1 reads range R_(N-1), R_N != R_(N-1). Vulkan
    // tracks hazards per byte range, so even if the GPU overlaps frame
    // N-1's draws with frame N's compute across submit boundaries there
    // is no WAR on the same bytes. Range reuse is gated by the ring
    // buffer's per-frame bookkeeping, which is informed by the frame
    // fence (so a range is only returned to the free pool after its
    // frame is known complete on the GPU).
}

} // namespace erhe::scene_renderer
