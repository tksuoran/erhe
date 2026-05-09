#pragma once

#include "erhe_utility/debug_label.hpp"
#include "erhe_utility/pimpl_ptr.hpp"

#include <string_view>

class Device;

namespace erhe::graphics {

class Command_buffer;
class Scoped_debug_group_impl;
class Scoped_debug_group final
{
public:
    template<std::size_t N>
    explicit Scoped_debug_group(const char (&debug_label)[N])
        : Scoped_debug_group{erhe::utility::Debug_label{std::string_view{debug_label, N - 1}}}
    {
    }

    explicit Scoped_debug_group(erhe::utility::Debug_label debug_label);

    // Command-buffer-targeted overload. RenderDoc / Vulkan debug-utils
    // captures need the begin/end label calls on the SAME VkCommandBuffer
    // as the draws they bracket; the queue-level fallback used by the
    // single-arg overload above appears separated from the draws in
    // captures because the queue records them at submit time. Pass the
    // active cb (e.g. via Render_command_encoder::get_command_buffer()) to
    // get cb-scoped labels on Vulkan; on GL / Metal the cb argument is
    // unused and the existing context- / encoder-scoped behavior is
    // preserved.
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
