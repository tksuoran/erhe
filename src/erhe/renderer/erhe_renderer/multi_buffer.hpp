#pragma once

#include "erhe_renderer/buffer_writer.hpp"
#include "erhe_graphics/buffer.hpp"

#include <etl/vector.h>

namespace erhe::renderer {

class Multi_buffer
{
public:
    static constexpr std::size_t s_frame_resources_count = 4;

    Multi_buffer(erhe::graphics::Instance& graphics_instance, const std::string_view name);

    void reset     ();
    void next_frame();
    void bind      (const erhe::renderer::Buffer_range& range);
    void allocate  (gl::Buffer_target target, unsigned int binding_point, std::size_t size);
    void allocate  (gl::Buffer_target target, std::size_t size);

    [[nodiscard]] auto get_writer        () -> Buffer_writer&;
    [[nodiscard]] auto get_buffers       () -> etl::vector<erhe::graphics::Buffer, s_frame_resources_count>&;
    [[nodiscard]] auto get_buffers       () const -> const etl::vector<erhe::graphics::Buffer, s_frame_resources_count>&;
    [[nodiscard]] auto get_current_buffer() -> erhe::graphics::Buffer&;
    [[nodiscard]] auto get_name          () const -> const std::string&;

private:
    erhe::graphics::Instance&                                    m_instance;
    unsigned int                                                 m_binding_point{0};
    etl::vector<erhe::graphics::Buffer, s_frame_resources_count> m_buffers;
    etl::vector<Buffer_writer, s_frame_resources_count>          m_writers;
    std::size_t                                                  m_current_slot{0};
    std::string                                                  m_name;
};

} // namespace erhe::renderer
