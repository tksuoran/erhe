#pragma once

#include "erhe/components/component.hpp"
#include "erhe/graphics/gl_objects.hpp"

#include <gsl/span>

namespace erhe::graphics
{

class Buffer;

class Image_transfer
    : public erhe::components::Component
{
public:
    struct Slot
    {
        Slot();

        auto span_for(int                 width,
                      int                 height,
                      gl::Internal_format internal_format)
        -> gsl::span<std::byte>;

        auto gl_name()
        -> unsigned int
        {
            return pbo.gl_name();
        }

        gsl::span<std::byte> span;
        size_t               width     {0};
        size_t               height    {0};
        size_t               row_stride{0};
        size_t               capacity  {0};
        Gl_buffer            pbo;
        gl::Internal_format  internal_format{gl::Internal_format::rgba8};
        gl::Pixel_format     format         {gl::Pixel_format::rgba};
        gl::Pixel_type       type           {gl::Pixel_type::unsigned_byte};
    };

    static constexpr const char* c_name = "erhe::graphics::ImageTransfer";
    Image_transfer() : erhe::components::Component{c_name} {}
    virtual ~Image_transfer() = default;

    auto get_slot()
    -> Slot&
    {
        m_index = (m_index + 1) % m_slots.size();
        return m_slots[m_index];;
    }

private:
    size_t              m_index{0};
    std::array<Slot, 4> m_slots;
};

} // namespace erhe::graphics
