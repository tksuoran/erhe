#pragma once

#include "erhe_graphics/gl_objects.hpp"

#include <span>

#include <memory>

namespace erhe::graphics
{
    class Instance;
    class Scoped_gl_context;
}

namespace example
{

class Image_transfer
{
public:
    class Slot
    {
    public:
        explicit Slot(erhe::graphics::Instance& graphics_instance);

        [[nodiscard]] auto begin_span_for(
            const int                 width,
            const int                 height,
            const gl::Internal_format internal_format
        ) -> std::span<std::byte>;

        [[nodiscard]] auto gl_name() -> unsigned int
        {
            return m_pbo.gl_name();
        }

        void end();

        void begin_write();
        void end_write  (std::size_t byte_offset, std::size_t byte_count);

    private:
        void map();
        void unmap();

        erhe::graphics::Instance&  m_graphics_instance;
        gl::Buffer_storage_mask    m_storage_mask;
        gl::Map_buffer_access_mask m_access_mask;

        std::span<std::byte>      m_span;
        std::size_t               m_capacity{0};
        erhe::graphics::Gl_buffer m_pbo;
    };

    explicit Image_transfer(erhe::graphics::Instance& graphics_instance);

    [[nodiscard]] auto get_slot() -> Slot&;

private:
    std::size_t         m_index{0};
    std::array<Slot, 4> m_slots;
};

} // namespace example
