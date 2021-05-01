#include "erhe/graphics_experimental/render_group.hpp"

#include "erhe/graphics/log.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/multi_buffer.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/span.hpp"

namespace erhe::graphics
{

using erhe::log::Log;
using glm::vec2;
using glm::vec4;
using glm::mat4;
using std::make_unique;


auto operator==(const Draw_key& lhs, const Draw_key& rhs) noexcept
-> bool
{
    return (lhs.program          == rhs.program         ) &&
           (lhs.primitive_type   == rhs.primitive_type  ) &&
           (lhs.is_blend_enabled == rhs.is_blend_enabled) &&
           (lhs.texture_bindings == rhs.texture_bindings);
}

auto operator!=(const Draw_key& lhs, const Draw_key& rhs) noexcept
-> bool
{
    return !(lhs == rhs);
}

auto Draw_key_hash::operator()(const Draw_key& draw_key) const noexcept
-> size_t
{
    // TODO(tksuoran@gmail.com): Xor is not so good, use hash combine instead.
    return
        erhe::graphics::Shader_stages_hash{}(*(draw_key.program))
        ^ Texture_bindings_hash{}(draw_key.texture_bindings);
}

Render_group::Render_group()
    : erhe::components::Component{"Render_group"}
{
}

void Render_group::connect(std::shared_ptr<erhe::graphics::Command_buffer>       command_buffer,
                           std::shared_ptr<erhe::graphics::OpenGL_state_tracker> pipeline_state_tracker)
{
    m_command_buffer         = command_buffer;
    m_pipeline_state_tracker = pipeline_state_tracker;
    initialization_depends_on(command_buffer);
    initialization_depends_on(pipeline_state_tracker);

    Ensures(m_command_buffer);
    Ensures(m_pipeline_state_tracker);
}

void Render_group::initialize_component()
{
    Expects(m_pipeline_state_tracker);

    log_render_group.trace("Render_group::initialize_component()");
    Log::Indenter indenter;

    m_draw_indirect_buffers = make_unique<Multi_buffer>(gl::Buffer_target::draw_indirect_buffer,
                                                        2000,
                                                        sizeof(gl::Draw_elements_indirect_command),
                                                        false);

    m_uniform_buffers = make_unique<Multi_buffer>(gl::Buffer_target::uniform_buffer,
                                                  64 * 1024,
                                                  1,
                                                  false);

    Ensures(m_draw_indirect_buffers);
    Ensures(m_uniform_buffers);
}

void Render_group::end_frame()
{
    log_render_group.trace("{}\n", __func__);
    Log::Indenter indenter;

    Expects(m_draw_indirect_buffers);

    m_draw_indirect_buffers->advance();
}

auto Render_group::find_or_create_batch(const Draw_key& key)
-> Batch&
{
    log_render_group.trace("{}\n", __func__);
    Log::Indenter indenter;

    // if (key.is_blend_enabled)
    // {
    //
    // }
    // else
    {
        //using Draw_opaque_collection = std::unordered_map<Draw_key, Batch, Draw_key_hash>;
        auto fi = m_opaque_draws.find(key);
        if (fi == m_opaque_draws.end())
        {
            // No matching key found, create new Batch
            auto e = m_opaque_draws.emplace(key, Batch(m_draw_offset, batch_draw_capacity));
            m_draw_offset += batch_draw_capacity;
            auto& batch = e.first->second;
            return batch;
        }

        // Matching key found, add draw indirect record to batch
        auto& batch = fi->second;
        return batch;
    }
}

auto
Render_group::make_draw(const Draw_key& key,
                        uint32_t        index_count,
                        uint32_t        first_index,
                        uint32_t        base_vertex)
-> Render_group::draw_index_t
{
    log_render_group.trace("{}(index_count = {}, first_index = {}, base_vertex = {}\n",
                           __func__, index_count, first_index, base_vertex);
    Log::Indenter indenter;

    Expects(m_draw_indirect_buffers);

    // Find batch matching the draw key and add draw indirect record to that Batch
    auto& batch = find_or_create_batch(key);
    auto draw_index = batch.draw_offset + batch.draw_count;
    ++batch.draw_count;

    gl::Draw_elements_indirect_command command;
    command.index_count    = index_count;
    command.instance_count = 1;
    command.first_index    = first_index;
    command.base_vertex    = base_vertex;
    command.base_instance  = 0;

    m_draw_indirect_buffers->write(draw_index * sizeof(gl::Draw_elements_indirect_command), command);

    return draw_index;
}

auto
Render_group::make_draw(const Draw_key& key)
-> Render_group::draw_index_t
{
    log_render_group.trace("{}\n", __func__);
    Log::Indenter indenter;

    Expects(m_draw_indirect_buffers);

    // Find batch matching the draw key and add draw indirect record to that Batch
    auto& batch = find_or_create_batch(key);
    auto draw_index = batch.draw_offset + batch.draw_count;
    ++batch.draw_count;

    // NOTE Remove this?
    gl::Draw_elements_indirect_command command;
    command.index_count    = 0;
    command.instance_count = 1;
    command.first_index    = 0;
    command.base_vertex    = 0;
    command.base_instance  = 0;

    m_draw_indirect_buffers->write(draw_index * sizeof(gl::Draw_elements_indirect_command), command);

    return draw_index;
}

void Render_group::update_draw(draw_index_t draw_index,
                               uint32_t     index_count,
                               uint32_t     first_index,
                               uint32_t     base_vertex) const
{
    log_render_group.trace("{}(index_count = {}, first_index = {}, base_vertex = {})\n",
                           __func__, index_count, first_index, base_vertex);
    Log::Indenter indenter;

    Expects(m_draw_indirect_buffers);

    gl::Draw_elements_indirect_command draw;
    draw.index_count    = index_count;
    draw.instance_count = 1;
    draw.first_index    = first_index;
    draw.base_vertex    = base_vertex;
    draw.base_instance  = 0;

    m_draw_indirect_buffers->write(draw_index * sizeof(gl::Draw_elements_indirect_command), draw);
}

void Render_group::update_draw(draw_index_t                       draw_index,
                               gl::Draw_elements_indirect_command draw) const
{
    log_render_group.trace("{}(draw_index = {})\n",
                           __func__, draw_index);
    Log::Indenter indenter;

    Expects(m_draw_indirect_buffers);

    m_draw_indirect_buffers->write(draw_index * sizeof(gl::Draw_elements_indirect_command), draw);
}

void Render_group::apply_draw_key_state(const Draw_key& key)
{
    // Apply program
    log_render_group.trace("{}\n", __func__);
    Log::Indenter indenter;

    log_render_group.trace("program = {}\n", key.program->name());

    //m_pipeline->use_shader_stages(key.program);

    // Apply texture and sampler bindings
    for (size_t i = 0; i < key.texture_bindings.texture_units.size(); ++i)
    {
        const auto& binding = key.texture_bindings.texture_units[i];
        if (binding.texture != nullptr)
        {
            log_render_group.trace("\ttexture unit {} uses texture {}\n", i, binding.texture->gl_name());
            gl::bind_texture_unit(static_cast<GLuint>(i), binding.texture->gl_name());
        }
        if (binding.sampler != nullptr)
        {
            log_render_group.trace("\ttexture unit {} uses sampler {}\n", i, binding.sampler->gl_name());
            gl::bind_sampler(static_cast<GLuint>(i), binding.sampler->gl_name());
        }
    }
}

void Render_group::draw_spans()
{
    log_render_group.trace("{}\n", __func__);
    Log::Indenter indenter;

    // Expects(m_pipeline);
    // Expects(m_draw_indirect_buffers);

    // m_pipeline->track.execute(&m_base_render_state);

    // auto* uniform_buffer = m_uniform_buffers->current_buffer();
    // log_render_group.trace("using uniform buffer {}\n", uniform_buffer->gl_name());

    // GUI uses exactly one uniform block, at offset 0
    // uniform_buffer->bind_range_bytes(m_uniform_block.binding_point(),
    //                                  0,
    //                                  m_uniform_block.size_bytes());
    // TODO(tksuoran@gmail.com): bind uniform buffer ranges

    // auto* draw_indirect_buffer = m_draw_indirect_buffers->current_buffer();
    // log_render_group.trace("using draw indirect buffer {}\n", draw_indirect_buffer->gl_name());

    // m_pipeline->bind_buffer(draw_indirect_buffer);

    draw_opaque_spans();

    // m_pipeline->unbind_buffer(gl::Buffer_target::draw_indirect_buffer);

    // m_pipeline->track.blend.execute(&m_blend_premultiplied);
    // draw_blend_premultiplied_spans();
}

void Render_group::draw_opaque_spans()
{
    log_render_group.trace("{}\n", __func__);
    Log::Indenter indenter;

    for (auto& i : m_opaque_draws)
    {
        const auto& key = i.first;
        auto& span = i.second;

        apply_draw_key_state(key);

        const auto* offset = reinterpret_cast<const void* >(span.draw_offset * sizeof(gl::Draw_elements_indirect_command));
        auto byte_count = static_cast<GLsizei>(sizeof(gl::Draw_elements_indirect_command));
        gl::multi_draw_elements_indirect(key.primitive_type,
                                         gl::Draw_elements_type::unsigned_short,
                                         offset,
                                         static_cast<GLsizei>(span.draw_count),
                                         byte_count);
    }
}

#if 0
void Render_group::draw_blend_premultiplied_spans()
{
    for (auto& i : m_blend_premultiplied_draws)
    {
        auto& key   = i.first;
        auto& spans = i.second;

        apply_draw_key_state(key);

        // Do drawcalls
        for (auto& span : spans)
        {
            gl::multi_draw_elements_indirect(key.primitive_type,
                                             gl::Draw_elements_type::unsigned_short,
                                             span.offset_draws * sizeof(gl::Draw_elements_indirect_command),
                                             span.count_draws,
                                             sizeof(gl::Draw_elements_indirect_command));
        }
    }
}
#endif

} // namespace erhe::graphics
