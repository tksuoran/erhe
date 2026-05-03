#include "init_status_display.hpp"
#include "editor_log.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_renderer/text_renderer.hpp"
#include "erhe_ui/rectangle.hpp"
#include "erhe_utility/debug_label.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_window/window.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#  include "erhe_xr/headset.hpp"
#  include "erhe_xr/xr.hpp"
#  include "erhe_xr/xr_session.hpp"
#endif

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace editor {

namespace {

constexpr uint32_t c_text_color_abgr = 0xFFFFFFFFu; // opaque white

} // namespace

Init_status_display::Init_status_display(
    erhe::graphics::Device&           graphics_device,
    erhe::graphics::Command_buffer*&  init_command_buffer,
    erhe::window::Context_window&     window,
    erhe::renderer::Text_renderer&    text_renderer,
    const bool                        enabled,
    erhe::xr::Headset*                headset
)
    : m_graphics_device    {graphics_device}
    , m_init_command_buffer{init_command_buffer}
    , m_window             {window}
    , m_text_renderer      {text_renderer}
    , m_headset            {headset}
    , m_enabled            {enabled}
{
}

Init_status_display::~Init_status_display() noexcept = default;

void Init_status_display::set_line(const std::size_t line_index, const std::string_view text)
{
    if (m_lines.size() <= line_index) {
        m_lines.resize(line_index + 1);
    }
    m_lines.at(line_index) = text;
    render_present();
}

void Init_status_display::set_clear_color(const std::array<double, 4> color)
{
    m_clear_color = color;
}

void Init_status_display::render_text_overlay(
    erhe::graphics::Command_buffer& command_buffer,
    erhe::graphics::Texture* const  color_texture,
    erhe::graphics::Texture* const  depth_stencil_texture,
    const int                       width,
    const int                       height,
    const unsigned int              view_mask,
    const unsigned int              texture_layer,
    const char* const               debug_label
)
{
    erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
    render_pass_descriptor.color_attachments[0].texture       = color_texture;
    render_pass_descriptor.color_attachments[0].texture_layer = texture_layer;
    render_pass_descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    render_pass_descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    render_pass_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
    render_pass_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::color_attachment_optimal;
    render_pass_descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
    render_pass_descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::color_attachment_optimal;
    render_pass_descriptor.color_attachments[0].clear_value   = m_clear_color;
    if (depth_stencil_texture != nullptr) {
        render_pass_descriptor.depth_attachment.texture       = depth_stencil_texture;
        render_pass_descriptor.depth_attachment.texture_layer = texture_layer;
        render_pass_descriptor.depth_attachment.load_action   = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.depth_attachment.store_action  = erhe::graphics::Store_action::Store;
        render_pass_descriptor.depth_attachment.usage_before  = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
        render_pass_descriptor.depth_attachment.layout_before = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
        render_pass_descriptor.depth_attachment.usage_after   = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
        render_pass_descriptor.depth_attachment.layout_after  = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    }
    render_pass_descriptor.render_target_width  = width;
    render_pass_descriptor.render_target_height = height;
    render_pass_descriptor.view_mask            = view_mask;
    render_pass_descriptor.debug_label          = erhe::utility::Debug_label{std::string_view{debug_label}};

    erhe::graphics::Render_pass            xr_render_pass{m_graphics_device, render_pass_descriptor};
    erhe::graphics::Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(command_buffer);
    {
        erhe::graphics::Scoped_render_pass scoped{xr_render_pass, command_buffer};

        encoder.set_viewport_rect(0, 0, width, height);
        encoder.set_scissor_rect (0, 0, width, height);

        const erhe::math::Viewport viewport{0, 0, width, height};

        const float       font_size   = m_text_renderer.font_size();
        const float       line_height = font_size * 1.5f;
        const float       center_y    = static_cast<float>(height) * 0.5f;
        const std::size_t line_count  = m_lines.size();

        const bool top_left = (
            m_graphics_device.get_info().coordinate_conventions.framebuffer_origin
            == erhe::math::Framebuffer_origin::top_left
        );
        const float dir = top_left ? +1.0f : -1.0f;

        for (std::size_t i = 0; i < line_count; ++i) {
            const std::string& line = m_lines[i];
            if (line.empty()) {
                continue;
            }
            const erhe::ui::Rectangle bounds = m_text_renderer.measure(line);
            const float text_width  = static_cast<float>(bounds.size().x);
            const float x           = (static_cast<float>(width) - text_width) * 0.5f;
            const float offset_from_center =
                (static_cast<float>(i) - (static_cast<float>(line_count - 1) * 0.5f)) * line_height;
            const float y = center_y + dir * (offset_from_center + line_height * 0.5f);
            m_text_renderer.print(glm::vec3{x, y, 0.0f}, c_text_color_abgr, line);
            log_startup->info("Init: {}", line);
        }

        m_text_renderer.render(encoder, xr_render_pass, viewport);
    }
}

void Init_status_display::render_present()
{
    if (!m_enabled) {
        return;
    }

#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (m_headset != nullptr) {
        render_present_xr();
        return;
    }
#endif

    render_present_desktop();
}

void Init_status_display::render_present_desktop()
{
    erhe::graphics::Surface* const surface = m_graphics_device.get_surface();
    if (surface == nullptr) {
        return;
    }
    erhe::graphics::Swapchain* const swapchain = surface->get_swapchain();
    if (swapchain == nullptr) {
        return;
    }

    // Step 1: end + submit the currently-open init cb so we can run a real
    // swapchain frame underneath. The caller (Editor::Editor()) opened an
    // init cb in the same slot via wait_frame -> get_command_buffer ->
    // begin().
    ERHE_VERIFY(m_init_command_buffer != nullptr);
    {
        erhe::graphics::Command_buffer* const init_cb = m_init_command_buffer;
        init_cb->end();
        erhe::graphics::Command_buffer* init_cbs[] = { init_cb };
        m_graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{init_cbs});
        const bool init_end_ok = m_graphics_device.end_frame();
        ERHE_VERIFY(init_end_ok);
    }
    m_init_command_buffer = nullptr;

    // Step 2: drive a real swapchain frame, mirroring Editor::tick().
    const int width  = m_window.get_width();
    const int height = m_window.get_height();

    const bool wait_ok = m_graphics_device.wait_frame();
    ERHE_VERIFY(wait_ok);

    const erhe::graphics::Frame_begin_info frame_begin_info{
        .resize_width   = static_cast<uint32_t>(width),
        .resize_height  = static_cast<uint32_t>(height),
        .request_resize = false
    };

    erhe::graphics::Command_buffer& swap_cb = m_graphics_device.get_command_buffer(0);
    erhe::graphics::Frame_state frame_state{};
    const bool wait_swap_ok  = swap_cb.wait_for_swapchain(frame_state);
    const bool should_render = wait_swap_ok && swap_cb.begin_swapchain(frame_begin_info, frame_state);

    if (should_render && (width > 0) && (height > 0)) {
        swap_cb.begin();
        erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
        render_pass_descriptor.swapchain                          = swapchain;
        render_pass_descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.color_attachments[0].clear_value   = m_clear_color;
        render_pass_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::present;
        render_pass_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::present_src;
        render_pass_descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::present;
        render_pass_descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::present_src;
        render_pass_descriptor.render_target_width                = width;
        render_pass_descriptor.render_target_height               = height;
        render_pass_descriptor.debug_label                        = "Init_status_display";

        erhe::graphics::Render_pass        render_pass{m_graphics_device, render_pass_descriptor};
        erhe::graphics::Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(swap_cb);
        {
            erhe::graphics::Scoped_render_pass scoped{render_pass, swap_cb};

            // Without these the swapchain pass inherits dynamic state from
            // prior use, which has been observed in RenderDoc to leave
            // scissor[0] at 0,0,0,0 and rasterize nothing.
            encoder.set_viewport_rect(0, 0, width, height);
            encoder.set_scissor_rect (0, 0, width, height);

            const erhe::math::Viewport viewport{0, 0, width, height};

            const float       font_size   = m_text_renderer.font_size();
            const float       line_height = font_size * 1.5f;
            const float       center_y    = static_cast<float>(height) * 0.5f;
            const std::size_t line_count  = m_lines.size();

            const bool top_left = (
                m_graphics_device.get_info().coordinate_conventions.framebuffer_origin
                == erhe::math::Framebuffer_origin::top_left
            );
            const float dir = top_left ? +1.0f : -1.0f;

            for (std::size_t i = 0; i < line_count; ++i) {
                const std::string& line = m_lines[i];
                if (line.empty()) {
                    continue;
                }
                const erhe::ui::Rectangle bounds = m_text_renderer.measure(line);
                const float text_width  = static_cast<float>(bounds.size().x);
                const float x           = (static_cast<float>(width) - text_width) * 0.5f;
                const float offset_from_center =
                    (static_cast<float>(i) - (static_cast<float>(line_count - 1) * 0.5f)) * line_height;
                const float y = center_y + dir * (offset_from_center + line_height * 0.5f);
                m_text_renderer.print(glm::vec3{x, y, 0.0f}, c_text_color_abgr, line);
                log_startup->info("Init: {}",line);
            }

            m_text_renderer.render(encoder, render_pass, viewport);

        }

        swap_cb.end();
        swap_cb.end_swapchain(erhe::graphics::Frame_end_info{});
        erhe::graphics::Command_buffer* swap_cbs[] = { &swap_cb };
        m_graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{swap_cbs});
    }
    const bool end_ok = m_graphics_device.end_frame();
    ERHE_VERIFY(end_ok);

    // Step 3: re-open the init device frame so the rest of init has a cb
    // to record into. Reseat the pointer the editor holds to the fresh cb.
    const bool reopen_wait_ok = m_graphics_device.wait_frame();
    ERHE_VERIFY(reopen_wait_ok);
    erhe::graphics::Command_buffer& new_init_cb = m_graphics_device.get_command_buffer(0);
    new_init_cb.begin();
    m_init_command_buffer = &new_init_cb;
}

#if defined(ERHE_XR_LIBRARY_OPENXR)
void Init_status_display::render_present_xr()
{
    ERHE_VERIFY(m_headset != nullptr);

    // Step 1: end + submit the currently-open init cb so we can drive an
    // XR frame on top. xr_session::render_frame_multiview() takes a setup
    // cb and ends + submits it itself, so we hand off m_init_command_buffer
    // (already in recording state) and reseat the pointer to nullptr until
    // we open a fresh one in Step 3.
    ERHE_VERIFY(m_init_command_buffer != nullptr);
    erhe::graphics::Command_buffer* const init_cb = m_init_command_buffer;
    m_init_command_buffer = nullptr;

    // Step 2: drive an XR frame. Pump events first so the runtime can
    // transition the session through IDLE -> READY (which triggers the
    // application's xrBeginSession). Then begin the frame; if the
    // runtime says begin_ok && should_render, render a single layered
    // multiview pass with the loading text overlaid in screen space.
    // Otherwise call end_frame(rendered = false) to keep the runtime
    // happy and submit init_cb on its own (no XR rendering this tick).
    bool xr_rendered = false;
    if (m_headset->poll_events()) {
        const erhe::xr::Frame_timing timing = m_headset->begin_frame_();
        if (timing.begin_ok) {
            erhe::xr::Xr_session* const session = m_headset->get_xr_session();
            const bool multiview_ok = (session != nullptr) && session->is_multiview_enabled();
            // Init_status_display's multiview branch is currently disabled
            // because Text_renderer builds its pipeline against a
            // non-multiview render pass (view_mask = 0); drawing it into
            // a multiview render pass trips
            // VUID-vkCmdDraw-renderPass-02684 (render-pass /
            // pipeline view-mask incompatibility). The per-eye path
            // below works on every backend, including Vulkan + Quest.
            // Flip kAllowMultiview to true once Text_renderer ships a
            // multiview-aware pipeline variant.
            constexpr bool kAllowMultiview = false;
            if (kAllowMultiview && timing.should_render && multiview_ok) {
                auto callback = [this](const erhe::xr::Render_views_frame& frame, erhe::graphics::Command_buffer& views_cb) -> bool {
                    erhe::graphics::Texture* const color_texture         = frame.shared_color_texture;
                    erhe::graphics::Texture* const depth_stencil_texture = frame.shared_depth_stencil_texture;
                    if (color_texture == nullptr) {
                        return false;
                    }
                    render_text_overlay(
                        views_cb, color_texture, depth_stencil_texture,
                        static_cast<int>(frame.width), static_cast<int>(frame.height),
                        frame.view_mask, 0u, "Init_status_display (XR multiview)"
                    );
                    return true;
                };
                xr_rendered = m_headset->render_multiview(*init_cb, callback);
                // render_frame_multiview() ends + submits init_cb and
                // the views cb internally but leaves the graphics
                // device frame in the in-flight state -- it is the
                // caller's job to call Device::end_frame() to balance
                // the wait_frame() / get_command_buffer() pair the
                // editor opened before this constructor body ran.
                const bool xr_end_ok = m_graphics_device.end_frame();
                ERHE_VERIFY(xr_end_ok);
            } else if (timing.should_render && multiview_ok) {
                // Multiview swapchain available, but multiview rendering
                // disabled (kAllowMultiview false above; Text_renderer's
                // pipeline is not multiview-compatible). Acquire the
                // shared multiview swapchain and render each eye into
                // its own array layer with a separate non-multiview
                // (view_mask = 0) render pass. Same swapchain submission
                // shape as the multiview path, but Text_renderer's
                // existing pipeline works because each pass is single-view.
                auto callback = [this](const erhe::xr::Render_views_frame& frame, erhe::graphics::Command_buffer& views_cb) -> bool {
                    erhe::graphics::Texture* const color_texture         = frame.shared_color_texture;
                    erhe::graphics::Texture* const depth_stencil_texture = frame.shared_depth_stencil_texture;
                    if (color_texture == nullptr) {
                        return false;
                    }
                    for (std::size_t i = 0; i < frame.views.size(); ++i) {
                        render_text_overlay(
                            views_cb, color_texture, depth_stencil_texture,
                            static_cast<int>(frame.width), static_cast<int>(frame.height),
                            0u, static_cast<unsigned int>(i),
                            "Init_status_display (XR multiview per-layer)"
                        );
                    }
                    return true;
                };
                xr_rendered = m_headset->render_multiview(*init_cb, callback);
                const bool xr_end_ok = m_graphics_device.end_frame();
                ERHE_VERIFY(xr_end_ok);
            } else if (timing.should_render) {
                // Per-eye fallback for graphics APIs that do not support
                // multiview (currently the OpenGL backend). The callback
                // is invoked once per view; render the same text overlay
                // into each eye's swapchain image. view_mask=0 means a
                // plain single-layer render pass.
                auto callback = [this](erhe::xr::Render_view& view, erhe::graphics::Command_buffer& view_cb) -> bool {
                    if (view.color_texture == nullptr) {
                        return false;
                    }
                    render_text_overlay(
                        view_cb, view.color_texture, view.depth_stencil_texture,
                        static_cast<int>(view.width), static_cast<int>(view.height),
                        0u, 0u, "Init_status_display (XR per-eye)"
                    );
                    return true;
                };
                xr_rendered = m_headset->render(*init_cb, callback);
                // Same lifecycle as the multiview branch: render() ends +
                // submits init_cb and per-view command buffers itself;
                // the device frame still needs end_frame() to balance the
                // outer wait_frame()/get_command_buffer() pair.
                const bool xr_end_ok = m_graphics_device.end_frame();
                ERHE_VERIFY(xr_end_ok);
            } else {
                // Session not yet RUNNING (state is IDLE / SYNCHRONIZED /
                // VISIBLE), or runtime says don't render this tick. The
                // init_cb still has the rest-of-init's recorded work, so
                // submit it on its own and call xrEndFrame with
                // rendered = false. Without xrEndFrame after a successful
                // xrBeginFrame the runtime would refuse the next
                // begin_frame.
                init_cb->end();
                erhe::graphics::Command_buffer* init_cbs[] = { init_cb };
                m_graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{init_cbs});
                const bool init_end_ok = m_graphics_device.end_frame();
                ERHE_VERIFY(init_end_ok);
            }
            m_headset->end_frame(xr_rendered);
        } else {
            // begin_frame failed (session not yet ready). Just submit
            // init_cb and don't try to call end_frame -- pairing
            // xrEndFrame with a failed xrBeginFrame would error.
            init_cb->end();
            erhe::graphics::Command_buffer* init_cbs[] = { init_cb };
            m_graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{init_cbs});
            const bool init_end_ok = m_graphics_device.end_frame();
            ERHE_VERIFY(init_end_ok);
        }
    } else {
        // poll_events failed. Same fallback as above.
        init_cb->end();
        erhe::graphics::Command_buffer* init_cbs[] = { init_cb };
        m_graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{init_cbs});
        const bool init_end_ok = m_graphics_device.end_frame();
        ERHE_VERIFY(init_end_ok);
    }

    // Step 3: re-open the init device frame so the rest of init has a cb
    // to record into.
    const bool reopen_wait_ok = m_graphics_device.wait_frame();
    ERHE_VERIFY(reopen_wait_ok);
    erhe::graphics::Command_buffer& new_init_cb = m_graphics_device.get_command_buffer(0);
    new_init_cb.begin();
    m_init_command_buffer = &new_init_cb;
}
#else
void Init_status_display::render_present_xr()
{
    // OpenXR support compiled out. Fall back to the desktop path so the
    // class is usable in unit tests / null backend builds.
    render_present_desktop();
}
#endif

} // namespace editor
