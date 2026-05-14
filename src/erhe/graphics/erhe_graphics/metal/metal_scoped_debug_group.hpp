#pragma once

#include "erhe_utility/debug_label.hpp"

namespace MTL {
    class RenderCommandEncoder;
}

namespace erhe::graphics {

class Command_buffer;

class Scoped_debug_group_impl final
{
public:
    Scoped_debug_group_impl(Command_buffer& command_buffer, erhe::utility::Debug_label debug_label);
    ~Scoped_debug_group_impl() noexcept;

    static bool s_enabled; // set by Device_impl during init

private:
    erhe::utility::Debug_label  m_debug_label;
    // Cache the encoder we pushed onto so the destructor pops on the
    // exact same encoder, not whichever encoder is active at the time
    // ~Scoped_debug_group_impl runs. Without this caching, switching
    // encoders between push/pop would mismatch debug groups.
    MTL::RenderCommandEncoder*  m_pushed_encoder{nullptr};
};

} // namespace erhe::graphics
