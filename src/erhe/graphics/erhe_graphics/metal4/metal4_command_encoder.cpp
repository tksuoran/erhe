#include "erhe_graphics/metal4/metal4_command_encoder.hpp"
#include "erhe_graphics/metal4/metal4_render_pass.hpp"
#include "erhe_graphics/metal4/metal4_buffer.hpp"
#include "erhe_graphics/buffer.hpp"

#include <Metal/Metal.hpp>

namespace erhe::graphics {

Command_encoder_impl::Command_encoder_impl(Device& device, Command_buffer& command_buffer)
    : m_device        {device}
    , m_command_buffer{command_buffer}
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

    // With explicit resource bindings, GLSL binding = Metal buffer index
    // (identity mapping), set in the active pass's argument table, which
    // the vertex and fragment stages share.
    MTL4::ArgumentTable* argument_table = Render_pass_impl::get_active_argument_table();
    if (argument_table == nullptr) {
        return;
    }
    argument_table->setAddress(mtl_buffer->gpuAddress() + static_cast<uint64_t>(offset), static_cast<NS::UInteger>(index));
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

    MTL4::ArgumentTable* argument_table = Render_pass_impl::get_active_argument_table();
    if (argument_table == nullptr) {
        return;
    }
    argument_table->setAddress(mtl_buffer->gpuAddress(), 0);
}

auto Command_encoder_impl::get_draw_indirect_buffer() const -> MTL::Buffer*
{
    return m_draw_indirect_buffer;
}

} // namespace erhe::graphics
