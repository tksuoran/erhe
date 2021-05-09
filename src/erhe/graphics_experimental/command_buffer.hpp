#ifndef command_buffer_hpp_erhe_graphics
#define command_buffer_hpp_erhe_graphics

#include "erhe/components/component.hpp"

#include "erhe/graphics_experimental/texture_bindings.hpp"

#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"

namespace erhe::graphics
{

class Buffer;
class Texture;
class Sampler;
struct Pipeline;
class OpenGL_state_tracker;


struct Buffer_binding
{
    gl::Buffer_target buffer_target;
    unsigned int      binding_point;
};

struct Uniform_block_binding
{
    unsigned int binding_point;
    unsigned int gl_name;
    size_t       offset;
    size_t       byte_count;
};

struct Descriptor_buffer_info
{
    Buffer*  buffer;
    size_t   byte_offset;
    size_t   byte_count;
};


class Command_buffer
    : public erhe::components::Component
{
public:
    static constexpr const char* c_name = "erhe::graphics::Command_buffer";
    Command_buffer() : erhe::components::Component{c_name} {}

    // Implements Component
    void connect() override;

    // vkCmdBindPipeline
    void bind_pipeline(Pipeline* pipeline);

    void bind_texture_unit(unsigned int unit, Texture* texture);

    // vkCmdBindDescriptorSets
    void bind_sampler(unsigned int unit, Sampler* sampler);

    // vkCmdBindDescriptorSets
    void bind_buffer(unsigned int binding_point,
                     Buffer*      buffer,
                     size_t       byte_offset,
                     size_t       byte_count);

    // vkCmdBindDescriptorSets
    void bind_buffer(Buffer*  buffer);

    // vkCmdBindDescriptorSets
    void unbind_buffer(gl::Buffer_target target);


    // vkCmdDraw
    void draw_arrays(gl::Draw_arrays_indirect_command draw);

    // vkCmdDraw
    void draw_arrays(size_t draw_offset);

    // vkCmdDrawIndexed
    void draw_elements(gl::Draw_elements_type             index_type,
                       gl::Draw_elements_indirect_command draw);

    // vkCmdDrawIndexed
    void draw_elements(gl::Draw_elements_type index_type,
                       size_t                 draw_offset);

    auto opengl_state_tracker()
    -> erhe::graphics::OpenGL_state_tracker*;

    void end();

private:
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;

    Pipeline*          m_pipeline{nullptr};

    Texture_bindings   m_texture_bindings;
};

} // namespace erhe::graphics

#endif
