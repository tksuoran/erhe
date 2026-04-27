#include "init_status_display.hpp"
#include "editor_log.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_renderer/text_renderer.hpp"
#include "erhe_ui/rectangle.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_window/window.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace editor {

namespace {

constexpr uint32_t c_text_color_abgr = 0xFFFFFFFFu; // opaque white

constexpr std::array<double, 4> c_clear_color{0.01, 0.01, 0.01, 1.0};

} // namespace

Init_status_display::Init_status_display(
    erhe::graphics::Device&           graphics_device,
    erhe::graphics::Command_buffer*&  init_command_buffer,
    erhe::window::Context_window&     window,
    erhe::renderer::Text_renderer&    text_renderer,
    const bool                        enabled
)
    : m_graphics_device    {graphics_device}
    , m_init_command_buffer{init_command_buffer}
    , m_window             {window}
    , m_text_renderer      {text_renderer}
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

void Init_status_display::render_present()
{
    if (!m_enabled) {
        return;
    }
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
        render_pass_descriptor.color_attachments[0].clear_value   = c_clear_color;
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

} // namespace editor
