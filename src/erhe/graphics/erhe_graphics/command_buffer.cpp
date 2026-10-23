#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_verify/verify.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_graphics/gl/gl_command_buffer.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_VULKAN)
# include "erhe_graphics/vulkan/vulkan_command_buffer.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_METAL4)
# include "erhe_graphics/metal4/metal4_command_buffer.hpp"
#elif defined(ERHE_GRAPHICS_API_METAL)
# include "erhe_graphics/metal/metal_command_buffer.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_NONE)
# include "erhe_graphics/null/null_command_buffer.hpp"
#endif

namespace erhe::graphics {

namespace {

enum class Barrier_side : unsigned int { before, after };

// One side of a texture_barrier (doc/erhe/graphics.md "Resource barriers"):
// none alone, and on usage_before only; otherwise at least one access, only
// access and modifier bits, a stage qualifier only with a shader access,
// discard on usage_before only. user_synchronized (a render pass descriptor
// modifier) and transient_attachment (a creation flag) are no barrier usage.
[[nodiscard]] auto is_valid_texture_barrier_usage(const std::uint64_t usage, const Barrier_side side) -> bool
{
    using Bits = Image_usage_flag_bit_mask;
    if (usage == Bits::none) {
        return side == Barrier_side::before;
    }
    constexpr std::uint64_t accesses =
        Bits::transfer_src     | Bits::transfer_dst             | Bits::sampled          |
        Bits::storage          | Bits::color_attachment         | Bits::depth_stencil_attachment |
        Bits::input_attachment | Bits::host_transfer            | Bits::present          |
        Bits::fragment_density_map;
    constexpr std::uint64_t shader_accesses = Bits::sampled | Bits::storage | Bits::input_attachment;
    constexpr std::uint64_t modifiers       = Bits::shader_stages | Bits::discard;
    if ((usage & accesses) == 0) {
        return false;
    }
    if ((usage & ~(accesses | modifiers)) != 0) {
        return false;
    }
    if (((usage & Bits::shader_stages) != 0) && ((usage & shader_accesses) == 0)) {
        return false;
    }
    if (((usage & Bits::discard) != 0) && (side != Barrier_side::before)) {
        return false;
    }
    return true;
}

// One side of a buffer_barrier: the rules of the texture side, without
// discard; the stage qualifiers go with uniform, storage_* and
// acceleration_structure_read.
[[nodiscard]] auto is_valid_buffer_barrier_usage(const std::uint64_t usage, const Barrier_side side) -> bool
{
    using Bits = Buffer_usage_flag_bit_mask;
    if (usage == Bits::none) {
        return side == Barrier_side::before;
    }
    constexpr std::uint64_t accesses =
        Bits::vertex       | Bits::index         | Bits::indirect     | Bits::uniform   |
        Bits::storage_read | Bits::storage_write | Bits::transfer_src | Bits::transfer_dst |
        Bits::host_read    | Bits::host_write    |
        Bits::acceleration_structure_build_input | Bits::acceleration_structure_read | Bits::acceleration_structure_write;
    constexpr std::uint64_t shader_accesses =
        Bits::uniform | Bits::storage_read | Bits::storage_write | Bits::acceleration_structure_read;
    if ((usage & accesses) == 0) {
        return false;
    }
    if ((usage & ~(accesses | Bits::shader_stages)) != 0) {
        return false;
    }
    if (((usage & Bits::shader_stages) != 0) && ((usage & shader_accesses) == 0)) {
        return false;
    }
    return true;
}

} // anonymous namespace

Command_buffer::Command_buffer(Device& device, erhe::utility::Debug_label debug_label)
    : m_impl{std::make_unique<Command_buffer_impl>(device, std::move(debug_label))}
{
}

Command_buffer::~Command_buffer() noexcept = default;
Command_buffer::Command_buffer(Command_buffer&& other) noexcept = default;
auto Command_buffer::operator=(Command_buffer&& other) noexcept -> Command_buffer& = default;

auto Command_buffer::get_active_render_pass() const noexcept -> Render_pass*
{
    return m_active_render_pass;
}

void Command_buffer::set_active_render_pass(Render_pass* const render_pass) noexcept
{
    m_active_render_pass = render_pass;
}

auto Command_buffer::get_debug_label() const noexcept -> erhe::utility::Debug_label
{
    return m_impl->get_debug_label();
}

auto Command_buffer::get_frame_id() const noexcept -> std::uint64_t
{
    return m_impl->get_frame_id();
}

void Command_buffer::begin()
{
    m_impl->begin();
    m_recording = true;
}

void Command_buffer::end()
{
    m_impl->end();
    m_recording = false;
}

auto Command_buffer::is_recording() const noexcept -> bool
{
    return m_recording;
}

auto Command_buffer::wait_for_swapchain(Frame_state& out_frame_state) -> bool
{
    return m_impl->wait_for_swapchain(out_frame_state);
}

auto Command_buffer::begin_swapchain(const Frame_begin_info& frame_begin_info, Frame_state& out_frame_state) -> bool
{
    return m_impl->begin_swapchain(frame_begin_info, out_frame_state);
}

void Command_buffer::end_swapchain(const Frame_end_info& frame_end_info)
{
    m_impl->end_swapchain(frame_end_info);
}

void Command_buffer::signal_gpu_completion_to(Command_buffer& consumer)
{
    m_impl->signal_gpu_completion_to(consumer);
}

void Command_buffer::wait_for_gpu_completion_signal()
{
    m_impl->wait_for_gpu_completion_signal();
}

void Command_buffer::signal_cpu_completion()
{
    m_impl->signal_cpu_completion();
}

void Command_buffer::wait_for_cpu_completion_of(Command_buffer& producer)
{
    m_impl->wait_for_cpu_completion_of(producer);
}

void Command_buffer::upload_to_buffer(const Buffer& buffer, std::size_t offset, const void* data, std::size_t length)
{
    m_impl->upload_to_buffer(buffer, offset, data, length);
}

void Command_buffer::upload_to_texture(
    const Texture&           texture,
    int                      level,
    int                      x,
    int                      y,
    int                      width,
    int                      height,
    erhe::dataformat::Format pixelformat,
    const void*              data,
    int                      row_stride
)
{
    m_impl->upload_to_texture(texture, level, x, y, width, height, pixelformat, data, row_stride);
}

void Command_buffer::clear_texture(const Texture& texture, std::array<double, 4> clear_value)
{
    m_impl->clear_texture(texture, clear_value);
}

void Command_buffer::transition_texture_layout(const Texture& texture, Image_layout new_layout)
{
    m_impl->transition_texture_layout(texture, new_layout);
}

void Command_buffer::texture_barrier(
    const Texture&                   texture,
    const Texture_subresource_range& range,
    const std::uint64_t              usage_before,
    const std::uint64_t              usage_after,
    const Barrier_scope              scope
)
{
    // Barriers inside a render pass would need a subpass self-dependency
    // (Vulkan) and are not expressible between encoders (Metal 4).
    ERHE_VERIFY(m_active_render_pass == nullptr);
    ERHE_VERIFY(is_valid_texture_barrier_usage(usage_before, Barrier_side::before));
    ERHE_VERIFY(is_valid_texture_barrier_usage(usage_after,  Barrier_side::after));
    m_impl->texture_barrier(texture, range, usage_before, usage_after, scope);
}

void Command_buffer::texture_barrier(
    const Texture&                   texture,
    const Texture_subresource_range& range,
    const std::uint64_t              usage_before,
    const std::uint64_t              usage_after
)
{
    texture_barrier(texture, range, usage_before, usage_after, Barrier_scope::command_buffer);
}

void Command_buffer::texture_barrier(const Texture& texture, const std::uint64_t usage_before, const std::uint64_t usage_after, const Barrier_scope scope)
{
    texture_barrier(texture, Texture_subresource_range{}, usage_before, usage_after, scope);
}

void Command_buffer::texture_barrier(const Texture& texture, const std::uint64_t usage_before, const std::uint64_t usage_after)
{
    texture_barrier(texture, Texture_subresource_range{}, usage_before, usage_after, Barrier_scope::command_buffer);
}

void Command_buffer::buffer_barrier(const Buffer& buffer, const std::uint64_t usage_before, const std::uint64_t usage_after, const Barrier_scope scope)
{
    ERHE_VERIFY(m_active_render_pass == nullptr);
    ERHE_VERIFY(is_valid_buffer_barrier_usage(usage_before, Barrier_side::before));
    ERHE_VERIFY(is_valid_buffer_barrier_usage(usage_after,  Barrier_side::after));
    m_impl->buffer_barrier(buffer, usage_before, usage_after, scope);
}

void Command_buffer::buffer_barrier(const Buffer& buffer, const std::uint64_t usage_before, const std::uint64_t usage_after)
{
    buffer_barrier(buffer, usage_before, usage_after, Barrier_scope::command_buffer);
}

void Command_buffer::cmd_texture_barrier(std::uint64_t usage_before, std::uint64_t usage_after)
{
    m_impl->cmd_texture_barrier(usage_before, usage_after);
}

void Command_buffer::memory_barrier(Memory_barrier_mask barriers)
{
    m_impl->memory_barrier(barriers);
}

auto Command_buffer::get_impl() -> Command_buffer_impl&
{
    return *m_impl.get();
}
auto Command_buffer::get_impl() const -> const Command_buffer_impl&
{
    return *m_impl.get();
}

} // namespace erhe::graphics
