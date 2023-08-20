#pragma once

#include "erhe_renderer/buffer_writer.hpp"
#include "erhe_graphics/buffer.hpp"

#include <vector>

namespace erhe::renderer
{

class Multi_buffer
{
public:
    static constexpr std::size_t s_frame_resources_count = 4;

    Multi_buffer(
        erhe::graphics::Instance& graphics_instance,
        const std::string_view    name
    );

    void reset     ();
    void next_frame();
    void bind      (const erhe::renderer::Buffer_range& range);

    void allocate(
        gl::Buffer_target target,
        unsigned int      binding_point,
        std::size_t       size
    );

    void allocate(
        gl::Buffer_target target,
        std::size_t       size
    );

    [[nodiscard]] auto writer        () -> Buffer_writer&;
    [[nodiscard]] auto buffers       () -> std::vector<erhe::graphics::Buffer>&;
    [[nodiscard]] auto buffers       () const -> const std::vector<erhe::graphics::Buffer>&;
    [[nodiscard]] auto current_buffer() -> erhe::graphics::Buffer&;
    [[nodiscard]] auto name          () const -> const std::string&;

protected:
    erhe::graphics::Instance&           m_instance;
    unsigned int                        m_binding_point{0};
    std::vector<erhe::graphics::Buffer> m_buffers;
    std::size_t                         m_current_slot{0};
    Buffer_writer                       m_writer;
    std::string                         m_name;
};

} // namespace erhe::renderer
