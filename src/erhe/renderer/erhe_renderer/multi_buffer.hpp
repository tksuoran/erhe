#pragma once

#include "erhe_renderer/buffer_writer.hpp"

#include "igl/Buffer.h"

#include <vector>

namespace igl {
    class IDevice;
    class IBuffer;
}

namespace erhe::renderer {

class Multi_buffer
{
public:
    static constexpr std::size_t s_frame_resources_count = 4;

    Multi_buffer(
        igl::IDevice&          device,
        const std::string_view name
    );

    void reset     ();
    void next_frame();
    void bind      (const erhe::renderer::Buffer_range& range);

    void allocate(
        igl::BufferDesc::BufferType buffer_type,
        unsigned int                binding_point,
        std::size_t                 size
    );

    void allocate(
        igl::BufferDesc::BufferType buffer_type,
        std::size_t                 size
    );

    [[nodiscard]] auto writer        () -> Buffer_writer&;
    [[nodiscard]] auto buffers       () -> std::vector<std::shared_ptr<igl::IBuffer>>&;
    [[nodiscard]] auto buffers       () const -> const std::vector<std::shared_ptr<igl::IBuffer>>&;
    [[nodiscard]] auto current_buffer() -> const std::shared_ptr<igl::IBuffer>&;
    [[nodiscard]] auto name          () const -> const std::string&;

protected:
    igl::IDevice&                              m_device;
    unsigned int                               m_binding_point{0};
    std::vector<std::shared_ptr<igl::IBuffer>> m_buffers;
    std::size_t                                m_current_slot{0};
    Buffer_writer                              m_writer;
    std::string                                m_name;
};

} // namespace erhe::renderer
