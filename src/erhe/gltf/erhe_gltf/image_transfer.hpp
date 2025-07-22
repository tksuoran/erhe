#pragma once

#include "erhe_graphics/gl_objects.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace erhe::graphics { class Device; }

namespace erhe::gltf {

class Image_transfer
{
public:
    class Slot
    {
    public:
        explicit Slot(erhe::graphics::Device& graphics_device);

        [[nodiscard]] auto begin_span_for(int width, int height, erhe::dataformat::Format pixelformat) -> std::span<std::uint8_t>;
        [[nodiscard]] auto gl_name() -> unsigned int
        {
            return m_pbo.gl_name();
        }

        void end(bool flush);

    private:
        void map();
        void unmap();

        erhe::graphics::Device&    m_graphics_device;
        gl::Buffer_storage_mask    m_storage_mask;
        gl::Map_buffer_access_mask m_access_mask;

        std::span<std::uint8_t>   m_span;
        std::size_t               m_capacity{0};
        erhe::graphics::Gl_buffer m_pbo;
    };

    explicit Image_transfer(erhe::graphics::Device& graphics_device);

    [[nodiscard]] auto get_slot() -> Slot&;

private:
    std::size_t         m_index{0};
    std::array<Slot, 4> m_slots;
};

} // namespace erhe::gltf
