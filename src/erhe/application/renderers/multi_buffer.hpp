#pragma once

#include "erhe/application/renderers/buffer_writer.hpp"
#include "erhe/graphics/buffer.hpp"

#include <memory>

namespace erhe::application
{

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
        const std::size_t       size
    );

    void allocate(
        const gl::Buffer_target target,
        const std::size_t       size
    );

    [[nodiscard]] auto writer        () -> Buffer_writer&;
    [[nodiscard]] auto buffers       () -> std::vector<erhe::graphics::Buffer>&;
    [[nodiscard]] auto buffers       () const -> const std::vector<erhe::graphics::Buffer>&;
    [[nodiscard]] auto current_buffer() -> erhe::graphics::Buffer&;
    [[nodiscard]] auto name          () const -> const std::string&;

protected:
    unsigned int                        m_binding_point{0};
    std::vector<erhe::graphics::Buffer> m_buffers;
    std::size_t                         m_current_slot{0};
    Buffer_writer                       m_writer;
    std::string                         m_name;
};

} // namespace erhe::application
