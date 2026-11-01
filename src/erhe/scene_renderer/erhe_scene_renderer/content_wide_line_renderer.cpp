#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_scene_renderer/content_wide_line_interface.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

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
    Mesh_memory&                        mesh_memory,
    const erhe::scene::Mesh&            mesh,
    const std::size_t                   lod_level,
    const Primitive_interface_settings& settings,
    const float                         line_width,
    const uint32_t                      group,
    const Content_wide_line_draw_lists& draw_lists,
    Content_wide_line_feed&             out
)
{
    ++out.mesh_count;

    // The item flag bits are the mesh's, so every primitive of the mesh
    // takes the same color. Outside the id pass the bucket path's records
    // carry id offset 0 (the bucket Primitive_buffer::update() overload writes
    // id_offset_unused = 0 with no id ranges), so
    // id_offset resolves to the zero color here as there.
    const glm::vec4 color = (settings.color_source == Primitive_color_source::id_offset)
        ? glm::vec4{0.0f}
        : settings.get_pass_color(mesh.get_flag_bits());

    const glm::mat4                           world_from_node  = mesh.world_from_node();
    const std::shared_ptr<erhe::scene::Skin>& skin             = mesh.skin;
    const bool                                mesh_is_skinned  = (skin != nullptr);
    const uint32_t                            base_joint_index = mesh_is_skinned ? skin->skin_data.joint_buffer_index : 0u;

    // The edge lines of one edge / joint range pair; empty (count 0) when
    // the range is empty or has no buffer.
    const auto make_level_edges = [&](const erhe::primitive::Buffer_range& edge_range, const erhe::primitive::Buffer_range& joint_range) -> Content_wide_line_level_edges {
        Content_wide_line_level_edges edges{};
        if (edge_range.count == 0) {
            return edges;
        }
        erhe::graphics::Buffer* edge_buffer = mesh_memory.get_vertex_buffer(edge_range);
        if (edge_buffer == nullptr) {
            return edges;
        }
        edges.edge_buffer             = edge_buffer;
        edges.edge_buffer_byte_offset = edge_range.byte_offset;
        edges.edge_count              = edge_range.count / 2;
        if (mesh_is_skinned && (joint_range.count > 0)) {
            erhe::graphics::Buffer* joint_buffer = mesh_memory.get_vertex_buffer(joint_range);
            if (joint_buffer != nullptr) {
                edges.joint_buffer             = joint_buffer;
                edges.joint_buffer_byte_offset = joint_range.byte_offset;
                edges.joint_buffer_byte_size   = joint_range.count * joint_range.element_size;
            }
        }
        return edges;
    };

    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh.get_primitives();
    for (std::size_t primitive_index = 0, primitive_end = mesh_primitives.size(); primitive_index < primitive_end; ++primitive_index) {
        const erhe::scene::Mesh_primitive& mesh_primitive = mesh_primitives[primitive_index];
        if (!mesh_primitive.primitive) {
            continue;
        }
        // Level 0 edge lines come from the dedicated edge-line vertex buffer,
        // which is written from GEO mesh data and is independent of the main
        // buffer's vertex order - so the always-present variant is the right
        // source, and stays right even while an optimized variant is what gets
        // drawn. A level above 0 has its own edge lines on the variant the
        // draw lists draw it from (lod.hpp).
        const erhe::primitive::Buffer_mesh* original_mesh =
            mesh_primitive.primitive->get_renderable_mesh(erhe::primitive::Mesh_variant::original);
        const Content_wide_line_level_edges original_edges = (original_mesh != nullptr)
            ? make_level_edges(original_mesh->edge_line_vertex_buffer_range, original_mesh->edge_line_joint_buffer_range)
            : Content_wide_line_level_edges{};

        // The chain of the variant the draw lists draw.
        const erhe::primitive::Lod_chain* chain = nullptr;
        {
            const std::pair<erhe::primitive::Mesh_variant, const erhe::primitive::Buffer_mesh*> drawn =
                mesh_primitive.primitive->get_resolved_renderable_mesh(erhe::primitive::Mesh_variant::optimized, erhe::primitive::Primitive_mode::polygon_fill);
            if ((drawn.first == erhe::primitive::Mesh_variant::optimized) && (drawn.second != nullptr) && !drawn.second->lod_chain.is_empty()) {
                chain = &drawn.second->lod_chain;
            }
        }

        // Every level's edge lines, level 0 first; a level without its own
        // edge lines draws level 0's.
        const uint32_t first_level_edges = static_cast<uint32_t>(out.level_edges.size());
        uint32_t       level_edge_count  = 0;
        bool           any_level_edges   = false;
        if (chain != nullptr) {
            for (std::size_t level = 0; level < chain->level_count; ++level) {
                Content_wide_line_level_edges edges = original_edges;
                if (level > 0) {
                    const erhe::primitive::Lod_level& chain_level = chain->levels[level];
                    const Content_wide_line_level_edges own_edges = make_level_edges(chain_level.edge_line_vertex_range, chain_level.edge_line_joint_range);
                    if (own_edges.edge_count > 0) {
                        edges = own_edges;
                    }
                }
                any_level_edges = any_level_edges || (edges.edge_count > 0);
                out.level_edges.push_back(edges);
                ++level_edge_count;
            }
        }

        // The level the record is prepared with: lod_level clamped to the
        // chain, with its own edge lines, else the original variant's.
        const std::size_t default_level = ((chain != nullptr) && (lod_level > 0)) ? chain->get_clamped_level(lod_level) : std::size_t{0};
        const Content_wide_line_level_edges default_edges = (level_edge_count > 0)
            ? out.level_edges[first_level_edges + default_level]
            : original_edges;
        if ((default_edges.edge_count == 0) && !any_level_edges) {
            out.level_edges.resize(first_level_edges);
            continue;
        }
        Content_wide_line_record record{
            .edge_buffer              = default_edges.edge_buffer,
            .edge_buffer_byte_offset  = default_edges.edge_buffer_byte_offset,
            .edge_count               = default_edges.edge_count,
            .world_from_node          = world_from_node,
            .color                    = color,
            .line_width               = line_width,
            .group                    = group,
            .mesh_is_skinned          = mesh_is_skinned,
            .joint_buffer             = default_edges.joint_buffer,
            .joint_buffer_byte_offset = default_edges.joint_buffer_byte_offset,
            .joint_buffer_byte_size   = default_edges.joint_buffer_byte_size,
            .base_joint_index         = base_joint_index,
            .first_level_edges        = first_level_edges,
            .level_edge_count         = level_edge_count
        };
        if ((draw_lists.draw_list_scene != nullptr) && (level_edge_count > 0)) {
            record.color_entry = draw_lists.draw_list_scene->find_published_color_entry(draw_lists.frame_index, &mesh, primitive_index);
        }
        out.records.push_back(record);
    }
}

void Content_wide_line_renderer::add_feed(const Content_wide_line_feed& feed, const Content_wide_line_lod_view& lod_view)
{
    for (const Content_wide_line_record& record : feed.records) {
        if ((lod_view.draw_list_scene == nullptr) || (record.level_edge_count == 0) || !record.color_entry.is_valid()) {
            if (record.edge_count > 0) {
                add_record(record);
            }
            continue;
        }
        // The level the record's color entry draws in the view; its
        // template range is the level the record was prepared with.
        const Drawn_lod_level drawn = lod_view.draw_list_scene->get_published_entry_lod_level(lod_view.frame_index, lod_view.cull_view, record.color_entry);
        if (drawn.source == Drawn_lod_level_source::template_range) {
            if (record.edge_count > 0) {
                add_record(record);
            }
            continue;
        }
        // The entry's chain is the one it was registered with, the record's
        // the one of the variant at the prepare: the same, but for the
        // frame in which a worker publishes a new variant.
        const uint32_t    level = std::min(drawn.level, record.level_edge_count - 1u);
        const std::size_t index = static_cast<std::size_t>(record.first_level_edges) + level;
        ERHE_VERIFY(index < feed.level_edges.size());
        const Content_wide_line_level_edges& edges = feed.level_edges[index];
        if (edges.edge_count == 0) {
            continue;
        }
        Content_wide_line_record level_record = record;
        level_record.edge_buffer              = edges.edge_buffer;
        level_record.edge_buffer_byte_offset  = edges.edge_buffer_byte_offset;
        level_record.edge_count               = edges.edge_count;
        level_record.joint_buffer             = edges.joint_buffer;
        level_record.joint_buffer_byte_offset = edges.joint_buffer_byte_offset;
        level_record.joint_buffer_byte_size   = edges.joint_buffer_byte_size;
        add_record(level_record);
    }
}

void Content_wide_line_renderer::add_mesh(
    Mesh_memory&                        mesh_memory,
    const erhe::scene::Mesh&            mesh,
    const std::size_t                   lod_level,
    const Primitive_interface_settings& settings,
    const float                         line_width,
    const uint32_t                      group
)
{
    // Leaf scratch (AGENTS.md): add_feed() does not re-enter add_mesh().
    static thread_local Content_wide_line_feed s_feed;
    s_feed.clear();
    prepare_mesh(mesh_memory, mesh, lod_level, settings, line_width, group, Content_wide_line_draw_lists{}, s_feed);
    add_feed(s_feed, Content_wide_line_lod_view{});
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
