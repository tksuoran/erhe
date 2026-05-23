#pragma once

#include "erhe_utility/debug_label.hpp"
#include "erhe_utility/pimpl_ptr.hpp"

#include <string_view>

class Device;

namespace erhe::graphics {

class Command_buffer;
class Scoped_debug_group_impl;

// RAII GPU debug-marker scope. RenderDoc / Vulkan debug-utils captures
// need the begin/end label calls on the SAME VkCommandBuffer as the
// draws they bracket, so every constructor takes a Command_buffer. On
// GL / Metal the cb argument is unused (GL labels target the active
// context, Metal labels target the active MTL::RenderCommandEncoder
// looked up via the cb's Render_pass_impl).
class Scoped_debug_group final
{
public:
    template<std::size_t N>
    Scoped_debug_group(Command_buffer& command_buffer, const char (&debug_label)[N])
        : Scoped_debug_group{command_buffer, erhe::utility::Debug_label{std::string_view{debug_label, N - 1}}}
    {
    }
    Scoped_debug_group(Command_buffer& command_buffer, erhe::utility::Debug_label debug_label);

    ~Scoped_debug_group() noexcept;

private:
    erhe::utility::pimpl_ptr<Scoped_debug_group_impl, 128, 16> m_impl;
};

} // namespace erhe::graphics
