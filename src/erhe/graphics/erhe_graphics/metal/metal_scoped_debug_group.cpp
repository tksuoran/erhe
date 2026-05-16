#include "erhe_graphics/metal/metal_scoped_debug_group.hpp"
#include "erhe_graphics/metal/metal_render_pass.hpp"
#include "erhe_graphics/graphics_log.hpp"

#include <Metal/Metal.hpp>

namespace erhe::graphics {

bool Scoped_debug_group_impl::s_enabled{false};

// Metal routes labels to the active MTL::RenderCommandEncoder via
// Render_pass_impl::get_active_mtl_encoder(); the explicit cb argument
// is not consulted (the encoder owns the cb).
Scoped_debug_group_impl::Scoped_debug_group_impl(Command_buffer&, erhe::utility::Debug_label debug_label)
    : m_debug_label{std::move(debug_label)}
{
    MTL::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    if (encoder != nullptr && !m_debug_label.empty()) {
        NS::String* label = NS::String::alloc()->init(
            m_debug_label.data(),
            NS::UTF8StringEncoding
        );
        encoder->pushDebugGroup(label);
        label->release();
        // Cache the encoder we pushed onto so the destructor pops on
        // the same encoder even if Render_pass_impl::get_active_mtl_encoder
        // returns a different one by then.
        m_pushed_encoder = encoder;
    }
}

Scoped_debug_group_impl::~Scoped_debug_group_impl() noexcept
{
    if (m_pushed_encoder != nullptr) {
        m_pushed_encoder->popDebugGroup();
    }
}

} // namespace erhe::graphics
