#include "erhe_graphics/metal/metal_scoped_debug_group.hpp"
#include "erhe_graphics/debug_label_buffer.hpp"
#include "erhe_graphics/metal/metal_render_pass.hpp"
#include "erhe_graphics/graphics_log.hpp"

#include <Metal/Metal.hpp>

namespace erhe::graphics {

bool Scoped_debug_group_impl::s_enabled{false};

// Metal routes labels to the active MTL::RenderCommandEncoder via
// Render_pass_impl::get_active_mtl_encoder(); the explicit cb argument
// is not consulted (the encoder owns the cb).
Scoped_debug_group_impl::Scoped_debug_group_impl(Command_buffer&, const std::string_view debug_label)
{
    MTL::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    if (encoder != nullptr && !debug_label.empty()) {
        const Debug_label_buffer label_buffer{debug_label};
        NS::String* label = NS::String::alloc()->init(
            label_buffer.c_str(),
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

// Metal has no queue-level debug label API; see header.
Scoped_queue_debug_group_impl::Scoped_queue_debug_group_impl(Device&, std::string_view)
{
}

Scoped_queue_debug_group_impl::~Scoped_queue_debug_group_impl() noexcept = default;

} // namespace erhe::graphics
