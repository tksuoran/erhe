#pragma once

#include "erhe_utility/debug_label.hpp"
#include "erhe_utility/pimpl_ptr.hpp"

#include <string_view>

class Device;

namespace erhe::graphics {

class Command_buffer;
class Device;
class Scoped_debug_group_impl;
class Scoped_queue_debug_group_impl;

// RAII GPU debug-marker scope. RenderDoc / Vulkan debug-utils captures
// need the begin/end label calls on the SAME VkCommandBuffer as the
// draws they bracket, so every constructor takes a Command_buffer. On
// GL / Metal the cb argument is unused (GL labels target the active
// context, Metal labels target the active MTL::RenderCommandEncoder
// looked up via the cb's Render_pass_impl).
//
// Contract: the command buffer must stay in the recording state for the
// scope's entire lifetime -- the end label is recorded into the same cb
// as the begin label. On Vulkan, Command_buffer_impl::end() verifies no
// cb-level scope is still open. A region that outlives the cb (spans an
// end + submit) cannot be expressed as a cb label at all; use
// Scoped_queue_debug_group for those.
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

// RAII queue-level debug-marker scope for regions that span command
// buffers. Every submit issued between construction and destruction
// falls inside the region (Vulkan: vkQueueBeginDebugUtilsLabelEXT /
// vkQueueEndDebugUtilsLabelEXT on the graphics queue). Use for regions
// that a single command buffer cannot contain -- e.g. the XR frame,
// where the rendergraph cb is ended and submitted mid-frame and the
// frame continues in fresh cbs. Useless for regions whose submits
// happen only after the scope closes (the desktop frame submits after
// Rendergraph::execute() returns), so callers must pick cb-level or
// queue-level to match where the submits actually are.
//
// Threading: queue debug-label calls must be externally synchronized
// with the queue. Construct and destroy only on the thread that
// submits (the main thread).
class Scoped_queue_debug_group final
{
public:
    template<std::size_t N>
    Scoped_queue_debug_group(Device& device, const char (&debug_label)[N])
        : Scoped_queue_debug_group{device, erhe::utility::Debug_label{std::string_view{debug_label, N - 1}}}
    {
    }
    Scoped_queue_debug_group(Device& device, erhe::utility::Debug_label debug_label);

    ~Scoped_queue_debug_group() noexcept;

private:
    erhe::utility::pimpl_ptr<Scoped_queue_debug_group_impl, 128, 16> m_impl;
};

} // namespace erhe::graphics
