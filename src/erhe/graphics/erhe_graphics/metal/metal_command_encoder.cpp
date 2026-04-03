#include "erhe_graphics/metal/metal_command_encoder.hpp"
#include "erhe_graphics/metal/metal_render_pass.hpp"
#include "erhe_graphics/metal/metal_buffer.hpp"
#include "erhe_graphics/buffer.hpp"

#include <Metal/Metal.hpp>

namespace erhe::graphics {

Command_encoder_impl::Command_encoder_impl(Device& device)
    : m_device{device}
{
}

Command_encoder_impl::~Command_encoder_impl() noexcept
{
}

void Command_encoder_impl::set_buffer(
    const Buffer_target  buffer_target,
    const Buffer* const  buffer,
    const std::uintptr_t offset,
    const std::uintptr_t length,
    const std::uintptr_t index
)
{
    static_cast<void>(length);

    if (buffer == nullptr) {
        return;
    }

    MTL::Buffer* mtl_buffer = buffer->get_impl().get_mtl_buffer();
    if (mtl_buffer == nullptr) {
        return;
    }

    switch (buffer_target) {
        case Buffer_target::draw_indirect: {
            m_draw_indirect_buffer = mtl_buffer;
            return;
        }
        case Buffer_target::index: {
            return;
        }
        default: break;
    }

    // With explicit resource bindings, GLSL binding = Metal buffer index (identity mapping).
    // No binding map lookup needed.
    MTL::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    if (encoder == nullptr) {
        return;
    }

    NS::UInteger mtl_index  = static_cast<NS::UInteger>(index);
    NS::UInteger mtl_offset = static_cast<NS::UInteger>(offset);
    encoder->setVertexBuffer  (mtl_buffer, mtl_offset, mtl_index);
    encoder->setFragmentBuffer(mtl_buffer, mtl_offset, mtl_index);
}

void Command_encoder_impl::set_buffer(const Buffer_target buffer_target, const Buffer* const buffer)
{
    if (buffer == nullptr) {
        return;
    }

    MTL::Buffer* mtl_buffer = buffer->get_impl().get_mtl_buffer();
    if (mtl_buffer == nullptr) {
        return;
    }

    switch (buffer_target) {
        case Buffer_target::draw_indirect: {
            m_draw_indirect_buffer = mtl_buffer;
            return;
        }
        case Buffer_target::index: {
            return;
        }
        default: break;
    }

    MTL::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    if (encoder == nullptr) {
        return;
    }

    encoder->setVertexBuffer  (mtl_buffer, 0, 0);
    encoder->setFragmentBuffer(mtl_buffer, 0, 0);
}

auto Command_encoder_impl::get_draw_indirect_buffer() const -> MTL::Buffer*
{
    return m_draw_indirect_buffer;
}

} // namespace erhe::graphics
