#pragma once

#include "erhe/application/renderers/buffer_writer.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/primitive/enums.hpp"

#include <gsl/span>

#include <memory>

namespace erhe::primitive
{
    class Material;
}

namespace editor
{

class Programs;
class Program_interface;
class Shader_resources;

class Multi_buffer
{
public:
    static constexpr std::size_t s_frame_resources_count = 4;

    explicit Multi_buffer(const std::string_view name)
        : m_name{name}
    {
    }

    void next_frame();
    void bind();

    void allocate(
        const gl::Buffer_target target,
        const unsigned int      binding_point,
        const std::size_t       size,
        const std::string&      name
    );

    [[nodiscard]] auto writer        () -> erhe::application::Buffer_writer&;
    [[nodiscard]] auto current_buffer() -> erhe::graphics::Buffer&;

protected:
    //std::size_t                         m_stride{0};
    //std::size_t                         m_max_entry_count{0};

    unsigned int                        m_binding_point{0};
    std::vector<erhe::graphics::Buffer> m_buffers;
    std::size_t                         m_current_slot{0};
    erhe::application::Buffer_writer    m_writer;
    std::string                         m_name;
};

} // namespace editor
