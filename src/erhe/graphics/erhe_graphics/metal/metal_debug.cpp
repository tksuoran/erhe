#include "erhe_graphics/device.hpp"
#include "erhe_graphics/metal/metal_render_pass.hpp"

#include <Metal/Metal.hpp>

namespace erhe::graphics {

bool Scoped_debug_group::s_enabled{false};

void Scoped_debug_group::begin()
{
    MTL::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    if (encoder != nullptr && !m_debug_label.empty()) {
        NS::String* label = NS::String::alloc()->init(
            m_debug_label.data(),
            NS::UTF8StringEncoding
        );
        encoder->pushDebugGroup(label);
        label->release();
    }
}

void Scoped_debug_group::end()
{
    MTL::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    if (encoder != nullptr) {
        encoder->popDebugGroup();
    }
}

} // namespace erhe::graphics
