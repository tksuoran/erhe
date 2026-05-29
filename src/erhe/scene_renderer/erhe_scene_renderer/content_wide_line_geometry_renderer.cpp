#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_scene_renderer/content_wide_line_interface.hpp"
#include "erhe_scene_renderer/content_wide_line_view_writer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_verify/verify.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace erhe::scene_renderer {

namespace {

// Geometry-shader backend: draws edge_line_indices through the regular
// mesh vertex stream + index buffer, with content_edge_lines.{vert,
// geom,frag}. The geometry stage expands each line into a screen-
// space-oriented quad. No compute step, no triangle SSBO -- compute()
// is a no-op on this backend.
//
// Two shader variants are bound per-dispatch: the skinned variant
// (compiled against vertex_format_skinned, with
// ERHE_ATTRIBUTE_a_joint_weights_0 defined so the vert shader takes
// its skinning branch) for meshes that have a skin, and the not-
// skinned variant otherwise. Each variant must be paired with the
// matching Vertex_input_state since the two formats have different
// stream-0 strides.
class Content_wide_line_geometry_renderer final : public Content_wide_line_renderer
{
public:
    Content_wide_line_geometry_renderer(
        erhe::graphics::Device&        graphics_device,
        Content_wide_line_interface&   interface_,
        erhe::graphics::Shader_stages* geometry_shader_stages_not_skinned,
        erhe::graphics::Shader_stages* geometry_shader_stages_skinned
    );

    void compute(erhe::graphics::Compute_command_encoder& command_encoder) override;
    void render (
        erhe::graphics::Render_command_encoder& render_encoder,
        erhe::graphics::Base_render_pipeline&   pipeline_state,
        erhe::graphics::Color_blend_state*      color_blend_state,
        uint32_t                                group,
        bool                                    multiview
    ) override;

protected:
    void add_primitive(
        Mesh_memory&                        mesh_memory,
        const erhe::primitive::Buffer_mesh& buffer_mesh,
        const glm::mat4&                    world_from_node,
        const glm::vec4&                    color,
        float                               line_width,
        uint32_t                            group,
        bool                                mesh_is_skinned,
        uint32_t                            base_joint_index
    ) override;

    void release_backend_state() override;

private:
    class Vertex_stream_binding
    {
    public:
        erhe::graphics::Buffer* buffer{nullptr};
        std::uintptr_t          byte_offset{0};
    };

    class Dispatch_entry
    {
    public:
        glm::mat4   world_from_node{1.0f};
        glm::vec4   color          {1.0f};
        float       line_width     {1.0f};
        uint32_t    group          {0};
        uint32_t    edge_count     {0};   // index_count / 2
        bool        skinned        {false};
        uint32_t    base_joint_index{0};

        // Regular mesh index buffer + slice describing edge_line_indices.
        erhe::graphics::Buffer*  index_buffer            {nullptr};
        std::uintptr_t           first_index_byte_offset {0};
        uint32_t                 index_count             {0};
        erhe::dataformat::Format index_type              {erhe::dataformat::Format::format_undefined};

        // Regular mesh vertex stream(s), one per stream in the mesh's
        // Vertex_format. The byte_offset bakes in base_vertex so the
        // draw call itself has no base_vertex parameter.
        std::vector<Vertex_stream_binding> vertex_streams;
        const erhe::graphics::Vertex_input_state* vertex_input_state{nullptr};

        // Filled at render() time; released in release_backend_state().
        erhe::graphics::Ring_buffer_range view_buffer_range{};
        bool                              view_buffer_range_held{false};
    };

    [[nodiscard]] auto pick_shader_stages(bool skinned) const -> erhe::graphics::Shader_stages*;
    [[nodiscard]] auto pick_bind_group_layout(bool skinned) const -> erhe::graphics::Bind_group_layout*;

    erhe::graphics::Shader_stages* m_geometry_shader_stages_not_skinned{nullptr};
    erhe::graphics::Shader_stages* m_geometry_shader_stages_skinned    {nullptr};

    std::vector<Dispatch_entry>    m_dispatches;
};

Content_wide_line_geometry_renderer::Content_wide_line_geometry_renderer(
    erhe::graphics::Device&        graphics_device,
    Content_wide_line_interface&   interface_,
    erhe::graphics::Shader_stages* geometry_shader_stages_not_skinned,
    erhe::graphics::Shader_stages* geometry_shader_stages_skinned
)
    : Content_wide_line_renderer       {graphics_device, interface_}
    , m_geometry_shader_stages_not_skinned{geometry_shader_stages_not_skinned}
    , m_geometry_shader_stages_skinned    {geometry_shader_stages_skinned}
{
    ERHE_VERIFY(m_geometry_shader_stages_not_skinned != nullptr);
    ERHE_VERIFY(m_geometry_shader_stages_not_skinned->is_valid());
}

auto Content_wide_line_geometry_renderer::pick_shader_stages(const bool skinned) const -> erhe::graphics::Shader_stages*
{
    if (skinned && (m_geometry_shader_stages_skinned != nullptr) && m_geometry_shader_stages_skinned->is_valid()) {
        return m_geometry_shader_stages_skinned;
    }
    return m_geometry_shader_stages_not_skinned;
}

auto Content_wide_line_geometry_renderer::pick_bind_group_layout(const bool skinned) const -> erhe::graphics::Bind_group_layout*
{
    if (skinned && m_interface.geometry_bind_group_layout_skinned != nullptr) {
        return m_interface.geometry_bind_group_layout_skinned.get();
    }
    return &m_interface.geometry_bind_group_layout_not_skinned;
}

void Content_wide_line_geometry_renderer::add_primitive(
    Mesh_memory&                        mesh_memory,
    const erhe::primitive::Buffer_mesh& buffer_mesh,
    const glm::mat4&                    world_from_node,
    const glm::vec4&                    color,
    const float                         line_width,
    const uint32_t                      group,
    const bool                          mesh_is_skinned,
    const uint32_t                      base_joint_index
)
{
    const erhe::primitive::Index_range& edge_indices = buffer_mesh.edge_line_indices;
    if (edge_indices.index_count == 0) {
        return;
    }

    // Skinned mesh without a skinned shader variant -> can't render
    // (the skinned mesh's vertex stream has a different stride than the
    // not-skinned format the alternative shader expects). Silently drop;
    // matches the compute backend's "skinned data without skinned
    // pipeline = skipped" pattern at content_wide_line_compute_renderer.cpp.
    const bool skinned_path_available = (m_geometry_shader_stages_skinned != nullptr) && m_geometry_shader_stages_skinned->is_valid();
    if (mesh_is_skinned && !skinned_path_available) {
        return;
    }

    erhe::graphics::Buffer* index_buffer = mesh_memory.get_index_buffer(buffer_mesh.index_buffer_range);
    if (index_buffer == nullptr) {
        return;
    }

    const erhe::dataformat::Format index_type = mesh_memory.get_index_format(
        erhe::scene_renderer::Pool_buffer_identity{
            buffer_mesh.index_buffer_range.pool_id,
            buffer_mesh.index_buffer_range.buffer_id
        }
    );

    // base_index from the pool's byte_offset / element_size; first_index
    // is the offset of edge_line_indices inside the mesh's index slice.
    const std::size_t base_index_byte_offset =
        buffer_mesh.index_buffer_range.byte_offset +
        static_cast<std::size_t>(edge_indices.first_index) * buffer_mesh.index_buffer_range.element_size;

    Dispatch_entry entry;
    entry.world_from_node         = world_from_node;
    entry.color                   = color;
    entry.line_width              = line_width;
    entry.group                   = group;
    entry.edge_count              = static_cast<uint32_t>(edge_indices.index_count / 2);
    entry.skinned                 = mesh_is_skinned;
    entry.base_joint_index        = mesh_is_skinned ? base_joint_index : 0u;
    entry.index_buffer            = index_buffer;
    entry.first_index_byte_offset = base_index_byte_offset;
    entry.index_count             = static_cast<uint32_t>(edge_indices.index_count);
    entry.index_type              = index_type;

    entry.vertex_streams.reserve(buffer_mesh.vertex_buffer_ranges.size());
    for (const erhe::primitive::Buffer_range& stream_range : buffer_mesh.vertex_buffer_ranges) {
        erhe::graphics::Buffer* stream_buffer = mesh_memory.get_vertex_buffer(stream_range);
        if (stream_buffer == nullptr) {
            return;  // mesh not fully resident; skip the whole entry
        }
        entry.vertex_streams.push_back(Vertex_stream_binding{
            .buffer      = stream_buffer,
            .byte_offset = stream_range.byte_offset
        });
    }

    const Vertex_input_entry& vertex_input_entry = mesh_memory.get_vertex_input(buffer_mesh.vertex_input_key);
    entry.vertex_input_state = vertex_input_entry.vertex_input.get();

    m_dispatches.push_back(std::move(entry));
}

void Content_wide_line_geometry_renderer::compute(erhe::graphics::Compute_command_encoder& command_encoder)
{
    // Geometry-shader backend does its expansion in the render encoder;
    // there is no compute work to issue here. The argument is
    // intentionally accepted (and ignored) so the caller can call
    // renderer->compute() unconditionally regardless of backend.
    static_cast<void>(command_encoder);
}

void Content_wide_line_geometry_renderer::render(
    erhe::graphics::Render_command_encoder& render_encoder,
    erhe::graphics::Base_render_pipeline&   pipeline_state,
    erhe::graphics::Color_blend_state*      color_blend_state,
    const uint32_t                          group,
    const bool                              multiview
)
{
    // Geometry-shader backend does not currently support multiview
    // (content_edge_lines.{vert,geom} are compiled without
    // ERHE_MULTIVIEW). VR / headset always goes through the compute
    // backend; this path is desktop-only.
    ERHE_VERIFY(!multiview);
    static_cast<void>(multiview);

    if (m_dispatches.empty()) {
        return;
    }

    erhe::graphics::Scoped_debug_group debug_scope{
        render_encoder.get_command_buffer(),
        "Content_wide_line_geometry_renderer::render"
    };

    const Dispatch_per_frame_params& frame_params = get_frame_params();
    const std::size_t                view_size    = m_interface.view_block.get_size_bytes();

    erhe::graphics::Ring_buffer_client* joint_buffer_client = get_joint_buffer_client();
    erhe::graphics::Ring_buffer_range&  joint_buffer_range  = get_joint_buffer_range();

    bool                                      current_skinned         = false;
    bool                                      current_skinned_valid   = false;
    erhe::graphics::Shader_stages*            prev_shader_stages      = nullptr;
    const erhe::graphics::Vertex_input_state* prev_vertex_input_state = nullptr;

    for (Dispatch_entry& dispatch : m_dispatches) {
        if (dispatch.group != group) {
            continue;
        }

        erhe::graphics::Shader_stages*            shader_stages = pick_shader_stages    (dispatch.skinned);
        erhe::graphics::Bind_group_layout*        layout        = pick_bind_group_layout(dispatch.skinned);
        if ((shader_stages == nullptr) || !shader_stages->is_valid()) {
            continue;
        }

        // Rebuild + rebind the pipeline whenever shader stages or vertex
        // input flip. set_render_pipeline_state delegates to the encoder's
        // internal pipeline cache, so the same combination doesn't pay
        // a rebuild cost on repeated dispatches.
        if (
            (shader_stages              != prev_shader_stages     ) ||
            (dispatch.vertex_input_state != prev_vertex_input_state)
        ) {
            erhe::graphics::Render_pipeline_state temp_state{
                erhe::graphics::Render_pipeline_data{
                    .debug_label          = pipeline_state.data.debug_label,
                    .shader_stages        = shader_stages,
                    .vertex_input         = dispatch.vertex_input_state,
                    // The geometry shader expects line primitives as
                    // input regardless of the caller's pipeline state.
                    .input_assembly       = erhe::graphics::Input_assembly_state::line,
                    .multisample          = pipeline_state.data.multisample,
                    .viewport_depth_range = pipeline_state.data.viewport_depth_range,
                    .rasterization        = pipeline_state.data.rasterization,
                    .depth_stencil        = pipeline_state.data.depth_stencil,
                    .color_blend          = (color_blend_state != nullptr)
                        ? *color_blend_state
                        : erhe::graphics::Color_blend_state::color_blend_disabled
                }
            };
            render_encoder.set_bind_group_layout(layout);
            render_encoder.set_render_pipeline_state(temp_state);
            prev_shader_stages      = shader_stages;
            prev_vertex_input_state = dispatch.vertex_input_state;
            current_skinned_valid   = false;  // joint binding state invalidated
        }

        // Bind the cached joint UBO for skinned dispatches. Repeat the
        // bind only when the skinned-ness state flips because the joint
        // UBO range is the same for all skinned dispatches in a frame.
        if (dispatch.skinned) {
            if (joint_buffer_client == nullptr) {
                continue;  // caller did not provide joint data; degrade silently
            }
            if (!current_skinned_valid || !current_skinned) {
                joint_buffer_client->bind(render_encoder, joint_buffer_range);
                current_skinned       = true;
                current_skinned_valid = true;
            }
        } else if (!current_skinned_valid || current_skinned) {
            current_skinned       = false;
            current_skinned_valid = true;
        }

        // Acquire + write + bind the view UBO for this dispatch. Stash
        // the range on the dispatch so release_backend_state() can drop
        // it after the GPU is done.
        erhe::graphics::Ring_buffer_range view_buffer_range = m_view_buffer.acquire(
            erhe::graphics::Ring_buffer_usage::CPU_write,
            view_size
        );
        write_view_block(
            view_buffer_range.get_span(),
            m_interface.offsets,
            frame_params,
            dispatch.world_from_node,
            dispatch.color,
            dispatch.line_width,
            dispatch.edge_count,
            0u,  // stride_per_view: unused by the geometry-shader vertex stage
            dispatch.base_joint_index
        );
        view_buffer_range.bytes_written(view_size);
        view_buffer_range.close();
        m_view_buffer.bind(render_encoder, view_buffer_range);
        dispatch.view_buffer_range      = std::move(view_buffer_range);
        dispatch.view_buffer_range_held = true;

        // Bind the regular mesh index + vertex streams. base_vertex is
        // baked into each stream's byte_offset so the indexed draw call
        // itself has no base_vertex parameter.
        render_encoder.set_index_buffer(dispatch.index_buffer);
        for (std::size_t stream_index = 0; stream_index < dispatch.vertex_streams.size(); ++stream_index) {
            const Vertex_stream_binding& binding = dispatch.vertex_streams[stream_index];
            render_encoder.set_vertex_buffer(
                binding.buffer,
                binding.byte_offset,
                static_cast<uint32_t>(stream_index)
            );
        }

        render_encoder.draw_indexed_primitives(
            erhe::graphics::Primitive_type::line,
            static_cast<std::uintptr_t>(dispatch.index_count),
            dispatch.index_type,
            dispatch.first_index_byte_offset
        );
    }
}

void Content_wide_line_geometry_renderer::release_backend_state()
{
    for (Dispatch_entry& dispatch : m_dispatches) {
        if (dispatch.view_buffer_range_held) {
            dispatch.view_buffer_range.release();
            dispatch.view_buffer_range_held = false;
        }
    }
    m_dispatches.clear();
}

} // anonymous namespace

auto make_content_wide_line_geometry_renderer(
    erhe::graphics::Device&        graphics_device,
    Content_wide_line_interface&   interface_,
    erhe::graphics::Shader_stages* geometry_shader_stages_not_skinned,
    erhe::graphics::Shader_stages* geometry_shader_stages_skinned
) -> std::unique_ptr<Content_wide_line_renderer>
{
    if ((geometry_shader_stages_not_skinned == nullptr) || !geometry_shader_stages_not_skinned->is_valid()) {
        return nullptr;
    }
    return std::make_unique<Content_wide_line_geometry_renderer>(
        graphics_device,
        interface_,
        geometry_shader_stages_not_skinned,
        geometry_shader_stages_skinned
    );
}

} // namespace erhe::scene_renderer
