#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_utility/debug_label.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace erhe::graphics {

class Device;
class Gpu_timer;
class Render_command_encoder;
class Renderbuffer;
class Texture;
class Swapchain;
class Surface;

enum class Load_action : unsigned int {
    Dont_care = 0,
    Clear,
    Load
};

enum class Store_action : unsigned int {
    Dont_care = 0,
    Store,
    Multisample_resolve,
    Store_and_multisample_resolve
};

class Render_pass_attachment_descriptor final
{
public:
    Render_pass_attachment_descriptor();
    ~Render_pass_attachment_descriptor() noexcept;

    [[nodiscard]] auto is_defined     () const -> bool;
    [[nodiscard]] auto get_pixelformat() const -> erhe::dataformat::Format;

    unsigned int          texture_level  {0};
    unsigned int          texture_layer  {0};
    const Texture*        texture        {nullptr};
    std::array<double, 4> clear_value;
    Load_action           load_action    {Load_action::Clear};
    Store_action          store_action   {Store_action::Store};
    Texture*              resolve_texture{nullptr};
    unsigned int          resolve_level  {0};
    unsigned int          resolve_layer  {0};
    Resolve_mode          resolve_mode   {Resolve_mode::sample_zero};
    uint64_t              usage_before   {Image_usage_flag_bit_mask::invalid};
    uint64_t              usage_after    {Image_usage_flag_bit_mask::invalid};
    Image_layout          layout_before  {Image_layout::undefined};
    Image_layout          layout_after   {Image_layout::undefined};
};

class Render_pass_descriptor
{
public:
    Render_pass_descriptor();
    ~Render_pass_descriptor() noexcept;

    Swapchain*                                       swapchain           {nullptr};
    // Headless / surfaceless target: when `swapchain` is null and this is set
    // to a headless Surface, the Vulkan backend resolves and renders into that
    // surface's Emulated_swapchain_impl. Ignored when `swapchain` is non-null
    // (real WSI path) and on non-Vulkan backends.
    Surface*                                         surface             {nullptr};
    std::array<Render_pass_attachment_descriptor, 4> color_attachments   {};
    Render_pass_attachment_descriptor                depth_attachment    {};
    Render_pass_attachment_descriptor                stencil_attachment  {};
    int                                              render_target_width {0};
    int                                              render_target_height{0};
    // Vulkan multiview view mask. 0 = single-view (default). Non-zero
    // enables VK_KHR_multiview / VkSubpassDescription2.viewMask. The
    // attached color/depth textures must then be array textures whose
    // layer count covers the highest set bit + 1, and the framebuffer
    // is laid out with layers=1 per Vulkan spec (the multiview machinery
    // reads the actual per-view layers from the view mask). Ignored on
    // non-Vulkan backends (OpenGL multiview is not supported).
    uint32_t                                         view_mask           {0};
    // Fragment density map attachment for fixed foveated rendering (Vulkan
    // VK_EXT_fragment_density_map). When non-null, the Vulkan backend attaches
    // this texture as the render pass's fragment-density-map attachment. A bare
    // pointer (not a Render_pass_attachment_descriptor) deliberately keeps it
    // out of the color/depth load/store/layout-transition machinery: the
    // runtime hands the FDM image in FRAGMENT_DENSITY_MAP_OPTIMAL layout and it
    // must not be transitioned. Ignored on non-Vulkan backends.
    const Texture*                                   fragment_density_map_texture{nullptr};
    erhe::utility::Debug_label                       debug_label;
};

class Command_buffer;
class Render_pass_impl;
class Render_pass final
{
public:
    Render_pass   (Device& device, const Render_pass_descriptor& render_pass_descriptor);
    ~Render_pass  () noexcept;
    Render_pass   (const Render_pass&) = delete;
    void operator=(const Render_pass&) = delete;
    Render_pass   (Render_pass&&)      = delete;
    void operator=(Render_pass&&)      = delete;

    [[nodiscard]] auto get_sample_count        () const -> unsigned int;
    [[nodiscard]] auto get_render_target_width () const -> int;
    [[nodiscard]] auto get_render_target_height() const -> int;
    [[nodiscard]] auto get_swapchain           () const -> Swapchain*;
    [[nodiscard]] auto get_debug_label         () const -> erhe::utility::Debug_label;
    [[nodiscard]] auto get_descriptor          () const -> const Render_pass_descriptor&;
    [[nodiscard]] auto get_device              () -> Device&;
    [[nodiscard]] auto get_device              () const -> const Device&;
    [[nodiscard]] auto get_impl                () -> Render_pass_impl&;
    [[nodiscard]] auto get_impl                () const -> const Render_pass_impl&;

    // Timer registry: a Gpu_timer registers itself here at construction so
    // that its begin/end timestamps fire automatically on each pass scope.
    void register_gpu_timer  (Gpu_timer* timer);
    void unregister_gpu_timer(Gpu_timer* timer);

private:
    friend class Scoped_render_pass;
    // command_buffer    : the explicit Command_buffer that this render
    //                     pass should record into. Iteration target:
    //                     once the per-backend Command_buffer_impl wraps
    //                     a real command buffer, the impl will record
    //                     vkCmdBeginRenderPass2 / GL state into it.
    // render_pass_before: the immediately preceding render pass whose output
    //                     this pass consumes (or nullptr if none). Used by
    //                     the graphics backend to infer cross-submission
    //                     synchronization.
    // render_pass_after : the immediately succeeding render pass that will
    //                     consume this pass's output (or nullptr if none).
    //                     Used for debug-time validation of usage
    //                     continuity and potential future sync optimization.
    void start_render_pass(Command_buffer& command_buffer, Render_pass* render_pass_before, Render_pass* render_pass_after);
    void end_render_pass  (Render_pass* render_pass_after);

private:
    Device&                           m_device;
    Render_pass_descriptor            m_descriptor;
    std::unique_ptr<Render_pass_impl> m_impl;
    std::vector<Gpu_timer*>           m_gpu_timers;
    Command_buffer*                   m_active_command_buffer{nullptr};
};

class Scoped_render_pass final
{
public:
    // command_buffer is the explicit Command_buffer the render pass
    // records into.
    // render_pass_before / render_pass_after let the graphics backend
    // provide precise synchronization between render passes without
    // having to guess at the rendergraph topology. See
    // Render_pass::start_render_pass for details. nullptr means
    // "no known predecessor / successor".
    Scoped_render_pass(
        Render_pass&    render_pass,
        Command_buffer& command_buffer,
        Render_pass*    render_pass_before = nullptr,
        Render_pass*    render_pass_after  = nullptr
    );
    ~Scoped_render_pass() noexcept;
    Scoped_render_pass (const Scoped_render_pass&) = delete;
    void operator=(const Scoped_render_pass&) = delete;
    Scoped_render_pass(Scoped_render_pass&&) = delete;
    void operator=(Scoped_render_pass&&) = delete;

private:
    friend class Render_pass;

    Render_pass& m_render_pass;
    Render_pass* m_render_pass_after{nullptr};
};

} // namespace erhe::graphics
