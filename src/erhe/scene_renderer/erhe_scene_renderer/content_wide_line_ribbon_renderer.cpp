#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_scene_renderer/content_wide_line_interface.hpp"
#include "erhe_scene_renderer/content_wide_line_view_writer.hpp"

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_verify/verify.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace erhe::scene_renderer {

namespace {

// Bytes per edge-line endpoint: struct edge_line_vertex { vec4 position; vec4 normal; }.
constexpr std::size_t c_edge_line_vertex_byte_count = 8 * sizeof(float);

// Vertex-pulling backend: content_line_ribbon.vert expands every edge into
// a 6-vertex quad from the mesh's edge-line vertex buffer, so a frame writes
// one view block per record and nothing per edge (doc/erhe/scene_renderer.md).
class Content_wide_line_ribbon_renderer final : public Content_wide_line_renderer
{
public:
    Content_wide_line_ribbon_renderer(
        erhe::graphics::Device&              graphics_device,
        Content_wide_line_interface&         interface_,
        const Content_wide_line_shader_set&  shaders
    );

    void render(
        erhe::graphics::Render_command_encoder& render_encoder,
        erhe::graphics::Base_render_pipeline&   pipeline_state,
        erhe::graphics::Color_blend_state*      color_blend_state,
        uint32_t                                group,
        bool                                    multiview
    ) override;

protected:
    void add_record(const Content_wide_line_record& record) override;
    void release_backend_state() override;

private:
    class Draw_entry
    {
    public:
        erhe::graphics::Buffer*           edge_buffer             {nullptr};
        std::size_t                       edge_buffer_byte_offset {0};
        std::size_t                       edge_count              {0};
        glm::mat4                         world_from_node         {1.0f};
        glm::vec4                         color                   {1.0f};
        float                             line_width              {1.0f};
        uint32_t                          group                   {0};
        bool                              skinned                 {false};
        erhe::graphics::Buffer*           joint_buffer            {nullptr};
        std::size_t                       joint_buffer_byte_offset{0};
        std::size_t                       joint_buffer_byte_size  {0};
        uint32_t                          base_joint_index        {0};
        // Written by the first render() that draws the entry, held until end_frame().
        erhe::graphics::Ring_buffer_range view_buffer_range       {};
        bool                              view_written            {false};
    };

    void write_view(Draw_entry& entry);

    Content_wide_line_shader_set       m_shaders;
    // Empty but non-null: OpenGL core profile draws nothing with VAO 0 bound.
    erhe::graphics::Vertex_input_state m_empty_vertex_input;
    std::vector<Draw_entry>            m_entries;
};

Content_wide_line_ribbon_renderer::Content_wide_line_ribbon_renderer(
    erhe::graphics::Device&             graphics_device,
    Content_wide_line_interface&        interface_,
    const Content_wide_line_shader_set& shaders
)
    : Content_wide_line_renderer{graphics_device, interface_}
    , m_shaders                 {shaders}
    , m_empty_vertex_input      {graphics_device, erhe::graphics::Vertex_input_state_data{}}
{
    ERHE_VERIFY((m_shaders.single_view != nullptr) && m_shaders.single_view->is_valid());
}

void Content_wide_line_ribbon_renderer::add_record(const Content_wide_line_record& record)
{
    // A skinned mesh without per-endpoint joint data, or without the skinned
    // shaders, draws its bind pose.
    const bool skinned =
        record.mesh_is_skinned &&
        (record.joint_buffer != nullptr) &&
        (m_shaders.single_view_skinned != nullptr) &&
        (m_interface.skinned_bind_group_layout != nullptr);

    m_entries.push_back(Draw_entry{
        .edge_buffer              = record.edge_buffer,
        .edge_buffer_byte_offset  = record.edge_buffer_byte_offset,
        .edge_count               = record.edge_count,
        .world_from_node          = record.world_from_node,
        .color                    = record.color,
        .line_width               = record.line_width,
        .group                    = record.group,
        .skinned                  = skinned,
        .joint_buffer             = skinned ? record.joint_buffer : nullptr,
        .joint_buffer_byte_offset = skinned ? record.joint_buffer_byte_offset : 0u,
        .joint_buffer_byte_size   = skinned ? record.joint_buffer_byte_size : 0u,
        .base_joint_index         = skinned ? record.base_joint_index : 0u
    });
}

void Content_wide_line_ribbon_renderer::write_view(Draw_entry& entry)
{
    const std::size_t view_size = m_interface.view_block.get_size_bytes();
    entry.view_buffer_range = m_view_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, view_size);
    write_view_block(
        entry.view_buffer_range.get_span(),
        m_interface.offsets,
        get_frame_params(),
        entry.world_from_node,
        entry.color,
        entry.line_width,
        static_cast<uint32_t>(entry.edge_count),
        entry.base_joint_index,
        // The edge-line stream is never quantized (doc/erhe/vertex_position_quantization.md 4.3).
        Position_quantization{}
    );
    entry.view_buffer_range.bytes_written(view_size);
    entry.view_buffer_range.close();
    entry.view_written = true;
}

void Content_wide_line_ribbon_renderer::render(
    erhe::graphics::Render_command_encoder& render_encoder,
    erhe::graphics::Base_render_pipeline&   pipeline_state,
    erhe::graphics::Color_blend_state*      color_blend_state,
    const uint32_t                          group,
    const bool                              multiview
)
{
    if (m_entries.empty()) {
        return;
    }

    erhe::graphics::Scoped_debug_group debug_scope{
        render_encoder.get_command_buffer(),
        "Content_wide_line_ribbon_renderer::render"
    };

    erhe::graphics::Ring_buffer_client* joint_buffer_client = get_joint_buffer_client();
    erhe::graphics::Ring_buffer_range&  joint_buffer_range  = get_joint_buffer_range();

    // Back-face culling stays off: the ribbon's screen-space winding says
    // nothing about the surface's facing.
    erhe::graphics::Rasterization_state rasterization = pipeline_state.data.rasterization;
    rasterization.face_cull_enable = false;

    const auto make_state = [&](erhe::graphics::Shader_stages* shader_stages) -> erhe::graphics::Render_pipeline_state {
        return erhe::graphics::Render_pipeline_state{
            erhe::graphics::Render_pipeline_data{
                .debug_label          = pipeline_state.data.debug_label,
                .shader_stages        = shader_stages,
                .vertex_input         = &m_empty_vertex_input,
                // The caller's state was built for line topology.
                .input_assembly       = erhe::graphics::Input_assembly_state::triangle,
                .multisample          = pipeline_state.data.multisample,
                .viewport_depth_range = pipeline_state.data.viewport_depth_range,
                .rasterization        = rasterization,
                .depth_stencil        = pipeline_state.data.depth_stencil,
                .color_blend          = (color_blend_state != nullptr)
                    ? *color_blend_state
                    : erhe::graphics::Color_blend_state::color_blend_disabled
            }
        };
    };

    erhe::graphics::Shader_stages* const plain_stages   = multiview ? m_shaders.multiview         : m_shaders.single_view;
    erhe::graphics::Shader_stages* const skinned_stages = multiview ? m_shaders.multiview_skinned : m_shaders.single_view_skinned;
    const bool plain_valid   = (plain_stages   != nullptr) && plain_stages->is_valid();
    const bool skinned_valid = (skinned_stages != nullptr) && skinned_stages->is_valid() && (joint_buffer_client != nullptr);

    // 0: nothing bound yet, 1: plain, 2: skinned. A layout switch invalidates
    // the bound descriptors, so the joint block is rebound on each switch.
    int bound_variant = 0;
    for (Draw_entry& entry : m_entries) {
        if ((entry.group != group) || (entry.edge_count == 0)) {
            continue;
        }
        // A skinned entry without the skinned shaders draws its bind pose.
        const bool skinned = entry.skinned && skinned_valid;
        if (!skinned && !plain_valid) {
            continue;
        }
        const int variant = skinned ? 2 : 1;
        if (variant != bound_variant) {
            if (skinned) {
                render_encoder.set_bind_group_layout(m_interface.skinned_bind_group_layout.get());
                render_encoder.set_render_pipeline_state(make_state(skinned_stages));
                joint_buffer_client->bind(render_encoder, joint_buffer_range);
            } else {
                render_encoder.set_bind_group_layout(m_interface.bind_group_layout.get());
                render_encoder.set_render_pipeline_state(make_state(plain_stages));
            }
            bound_variant = variant;
        }
        if (!entry.view_written) {
            write_view(entry);
        }
        render_encoder.set_buffer(
            erhe::graphics::Buffer_target::storage,
            entry.edge_buffer,
            entry.edge_buffer_byte_offset,
            entry.edge_count * 2 * c_edge_line_vertex_byte_count,
            m_interface.edge_line_vertex_buffer_block->get_binding_point()
        );
        if (skinned) {
            render_encoder.set_buffer(
                erhe::graphics::Buffer_target::storage,
                entry.joint_buffer,
                entry.joint_buffer_byte_offset,
                entry.joint_buffer_byte_size,
                m_interface.edge_line_joint_vertex_buffer_block->get_binding_point()
            );
        }
        m_view_buffer.bind(render_encoder, entry.view_buffer_range);
        render_encoder.draw_primitives(
            erhe::graphics::Primitive_type::triangle,
            0,
            static_cast<uint32_t>(6 * entry.edge_count)
        );
    }
}

void Content_wide_line_ribbon_renderer::release_backend_state()
{
    for (Draw_entry& entry : m_entries) {
        if (entry.view_written) {
            entry.view_buffer_range.release();
        }
    }
    m_entries.clear();
}

} // anonymous namespace

auto make_content_wide_line_renderer(
    erhe::graphics::Device&             graphics_device,
    Content_wide_line_interface&        interface_,
    const Content_wide_line_shader_set& shaders
) -> std::unique_ptr<Content_wide_line_renderer>
{
    if ((shaders.single_view == nullptr) || !shaders.single_view->is_valid()) {
        return nullptr;
    }
    return std::make_unique<Content_wide_line_ribbon_renderer>(graphics_device, interface_, shaders);
}

} // namespace erhe::scene_renderer
