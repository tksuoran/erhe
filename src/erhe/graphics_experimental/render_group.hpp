#ifndef render_group_hpp_erhe_graphics
#define render_group_hpp_erhe_graphics

#include "erhe/graphics_experimental/command_buffer.hpp"
//#include "erhe/graphics/multi_buffer.hpp"

#include "erhe/graphics/sampler.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"

#include "erhe/components/component.hpp"

namespace erhe::graphics
{

class Buffer;
class Pipeline;
class Shader_stages;


class Draw_key
{
public:
    Draw_key() = delete;

    explicit Draw_key(gsl::not_null<erhe::graphics::Shader_stages*> program)
        : program{program}
    {
    }

    void set_texture(size_t unit, gsl::not_null<erhe::graphics::Texture*> texture)
    {
        Expects(unit < texture_bindings.texture_units.size());

        texture_bindings.texture_units[unit].texture = texture;
    }

    void set_sampler(size_t unit, gsl::not_null<erhe::graphics::Sampler*> sampler)
    {
        Expects(unit < texture_bindings.texture_units.size());

        texture_bindings.texture_units[unit].sampler = sampler;
    }

    gsl::not_null<erhe::graphics::Shader_stages*> program;
    gl::Primitive_type                            primitive_type  {gl::Primitive_type::triangles};
    bool                                          is_blend_enabled{false};
    Texture_bindings                              texture_bindings;
};

auto operator==(const Draw_key& lhs, const Draw_key& rhs) noexcept
-> bool;

auto operator!=(const Draw_key& lhs, const Draw_key& rhs) noexcept
-> bool;

class Draw_key_hash
{
public:
    auto operator()(const Draw_key& draw_key) const noexcept
    -> size_t;
};

// Shader_resource       buffer element size = m_uniform_block size in bytes
// Draw indirect buffer element size = sizeof(gl::Draw_elements_indirect_command)

/// Batch is a single draw call to multiDrawElements.
/// A range from draw indirect buffer is allocated for each Batch.
/// draw_count increases when draw calls are added to Batch.
/// Draw_span maps to single call to glMultiDrawElementsIndirect().
/// For each span, a range up, covering capacity_draws number of draws,
/// is allocated from draw_indirect buffer
class Batch
{
public:
    Batch(size_t draw_offset, size_t draw_capacity) noexcept
        : draw_offset  {draw_offset}
        , draw_capacity{draw_capacity}
    {
    }

    size_t draw_offset  {0};  // Start offset of range allocated from draw_indirect_buffer.
    size_t draw_count   {0};  // Number of draw calls added to the Batch.
    size_t draw_capacity{0};  // Size of range allocated from draw_indirect_buffer, also
                              // maximum number of draw calls (draw_count).
};

// Opaque draw calls can be collected to span with the same program and texture bindings;
// texture bindings with unused slots can be filled in with later draw calls.
// For each draw call:
//  - find matching existing key.
//  - if not found, create new key
//  - insert draw call to existing span if can, or add new span
using Draw_opaque_collection = std::unordered_map<Draw_key, Batch, Draw_key_hash>;

// Draw calls with blending must be rendered strictly in order.
// (in theory, this applies to overlapping draw calls only).
using Draw_blend_premultiplied_collection = std::vector<std::pair<Draw_key, Batch>>;

class Render_group
    : public erhe::components::Component
{
public:
    using draw_index_t = size_t;

    static constexpr draw_index_t batch_draw_capacity  =   200; // How many draws in Batch
    static constexpr draw_index_t buffer_draw_capacity = 20000; // How many draws in draw indirect / uniform buffer

    Render_group();

    virtual ~Render_group() = default;

    void connect(std::shared_ptr<erhe::graphics::Command_buffer>       command_buffer,
                 std::shared_ptr<erhe::graphics::OpenGL_state_tracker> pipeline_state_tracker);

    void initialize_component() override;

    void end_frame();

    /// Allocates draw call and updates the draw indirect record
    auto make_draw(const Draw_key& key,
                   uint32_t        index_count,
                   uint32_t        first_index,
                   uint32_t        base_vertex)
    -> draw_index_t;

    /// Allocates a draw call, but does not update the draw draw indirect record
    auto make_draw(const Draw_key& key)
    -> draw_index_t;

    /// Updates given draw call
    void update_draw(draw_index_t draw_index,
                     uint32_t     index_count,
                     uint32_t     first_index,
                     uint32_t     base_vertex) const;

    void update_draw(draw_index_t                       draw_index,
                     gl::Draw_elements_indirect_command draw) const;

    template <typename TDrawSource>
    void update(TDrawSource& draw_source)
    {
        update_draw(draw_source.draw_index(), draw_source.draw());
    }

    auto uniform_buffer()
    -> gsl::not_null<erhe::graphics::Multi_buffer*>
    {
        return m_uniform_buffers.get();
    }

    void draw_spans();

    void draw_opaque_spans();

    void draw_blend_premultiplied_spans();

protected:
    auto find_or_create_batch(const Draw_key& key)
    -> Batch&;

    void apply_draw_key_state(const Draw_key& key);

private:
    std::shared_ptr<erhe::graphics::Command_buffer>       m_command_buffer;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;

    Draw_opaque_collection                        m_opaque_draws;     /// Draw indirect commands (opaque)
    Draw_blend_premultiplied_collection           m_blend_premultiplied_draws;
    draw_index_t                                  m_draw_offset{0};   /// Next unused range for draw indirect commands

    erhe::graphics::Shader_resource               m_default_uniform_block; // containing sampler uniforms

    std::unique_ptr<erhe::graphics::Multi_buffer> m_draw_indirect_buffers;
    std::unique_ptr<erhe::graphics::Multi_buffer> m_uniform_buffers;
};

} // namespace erhe::graphics

#endif
