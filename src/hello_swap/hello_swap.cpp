#include "hello_swap.hpp"

#include "hello_swap_log.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_file/file.hpp"
#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
#   include "erhe_gl/gl_log.hpp"
#endif
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_log/log.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_math/math_log.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_window/renderdoc_capture.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_event_handler.hpp"
#include "erhe_window/window_log.hpp"

#include <atomic>

namespace hello_swap {

class Hello_swap : public erhe::window::Input_event_handler
{
public:
    Hello_swap()
        : m_window{
            erhe::window::Window_configuration{
                .use_depth = false,
                .size      = glm::ivec2{1920, 1080},
                .title     = "erhe hello_swap"
            }
        }
        , m_graphics_device{
            erhe::graphics::Surface_create_info{
                .context_window            = &m_window,
                .prefer_low_bandwidth      = false,
                .prefer_high_dynamic_range = false
            }
        }
    {
        m_last_window_width  = m_window.get_width();
        m_last_window_height = m_window.get_height();

        m_window.register_redraw_callback(
            [this](){
                // TODO Also skip if last frame is not older than some threshold
                if (!m_first_frame_rendered || m_in_tick.load()) {
                    return;
                }

                if (
                    (m_last_window_width  != m_window.get_width ()) ||
                    (m_last_window_height != m_window.get_height())
                ) {
                    m_request_resize_pending.store(true);
                    m_last_window_width  = m_window.get_width();
                    m_last_window_height = m_window.get_height();
                    log_swap->info("resize - changed, setting m_request_resize_pending");
                    tick();
                } else {
                    log_swap->info("resize - unchanged");
                }
            }
        );
    }

    std::optional<erhe::window::Input_event> m_window_resize_event{};
    int               m_last_window_width     {0};
    int               m_last_window_height    {0};
    std::atomic<bool> m_request_resize_pending{false};

    auto on_window_resize_event(const erhe::window::Input_event& input_event) -> bool override
    {
        log_swap->info("on_window_resize_event()");
        m_window_resize_event = input_event;
        return true;
    }

    auto on_window_close_event(const erhe::window::Input_event&) -> bool override
    {
        log_swap->info("on_window_close_event()");
        m_close_requested = true;
        return true;
    }

    auto on_window_refresh_event(const erhe::window::Input_event&) -> bool override
    {
        log_swap->info("on_window_refresh_event()");
        return true;
    }

    std::mutex        m_mutex;
    bool              m_first_frame_rendered{false};
    std::atomic<bool> m_in_tick{false};
    void run()
    {
        m_current_time = std::chrono::steady_clock::now();
        while (!m_close_requested) {
            m_window.poll_events();
            auto& input_events = m_window.get_input_events();
            for (erhe::window::Input_event& input_event : input_events) {
                static_cast<void>(this->dispatch_input_event(input_event));
            }
            if (m_window_resize_event.has_value()) {
                m_request_resize_pending.store(true);
                m_window_resize_event.reset();
                m_last_window_width  = m_window.get_width();
                m_last_window_height = m_window.get_height();
            }

            tick();

            m_first_frame_rendered = true;
        }
    }

    void tick()
    {
        m_in_tick.store(true);

        std::lock_guard<std::mutex> lock{m_mutex};

        erhe::graphics::Frame_state frame_state{};
        const bool wait_ok = m_graphics_device.wait_frame(frame_state);
        ERHE_VERIFY(wait_ok);

        // TODO use predicted display time
        const auto tick_end_time = std::chrono::steady_clock::now();

        // Update fixed steps
        {
            const auto new_time   = std::chrono::steady_clock::now();
            const auto duration   = new_time - m_current_time;
            double     frame_time = std::chrono::duration<double, std::ratio<1>>(duration).count();

            if (frame_time > 0.25) {
                frame_time = 0.25;
            }

            m_current_time = new_time;
            m_time_accumulator += frame_time;
            const double dt = 1.0 / 240.0;
            while (m_time_accumulator >= dt) {
                m_time_accumulator -= dt;
                m_time += dt;
            }
        }

        const int width  = m_window.get_width();
        const int height = m_window.get_height();

        const erhe::graphics::Frame_begin_info frame_begin_info{
            .resize_width   = static_cast<uint32_t>(m_last_window_width),
            .resize_height  = static_cast<uint32_t>(m_last_window_height),
            .request_resize = m_request_resize_pending.load()
        };
        m_request_resize_pending.store(false);

        const bool begin_frame_ok = m_graphics_device.begin_frame(frame_begin_info);
        ERHE_VERIFY(begin_frame_ok);

        update_render_pass(width, height);

        {
            erhe::graphics::Render_command_encoder render_encoder = m_graphics_device.make_render_command_encoder(*m_render_pass.get());
        }

        const erhe::graphics::Frame_end_info frame_end_info{
            .requested_display_time = 0 // TODO
        };

        const bool end_frame_ok = m_graphics_device.end_frame(frame_end_info);
        ERHE_VERIFY(end_frame_ok);

        m_in_tick.store(false);
    }

private:
    void update_render_pass(int width, int height)
    {
        if (
            m_render_pass &&
            (m_render_pass->get_render_target_width () == width) &&
            (m_render_pass->get_render_target_height() == height)
        ) {
            return;
        }

        m_render_pass.reset();
        erhe::graphics::Render_pass_descriptor render_pass_descriptor;
        render_pass_descriptor.swapchain             = m_graphics_device.get_surface()->get_swapchain();
        render_pass_descriptor.color_attachments[0].load_action    = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.color_attachments[0].clear_value[0] = 0.02;
        render_pass_descriptor.color_attachments[0].clear_value[1] = 0.02;
        render_pass_descriptor.color_attachments[0].clear_value[2] = 0.1;
        render_pass_descriptor.color_attachments[0].clear_value[3] = 1.0;
        render_pass_descriptor.depth_attachment    .load_action    = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.stencil_attachment  .load_action    = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.render_target_width  = width;
        render_pass_descriptor.render_target_height = height;
        render_pass_descriptor.debug_label          = "Example Render_pass";
        m_render_pass = std::make_unique<erhe::graphics::Render_pass>(m_graphics_device, render_pass_descriptor);
    }

    erhe::window::Context_window                 m_window;
    erhe::graphics::Device                       m_graphics_device;
    std::unique_ptr<erhe::graphics::Render_pass> m_render_pass;

    bool                                  m_close_requested{false};
    std::chrono::steady_clock::time_point m_current_time;
    double                                m_time_accumulator{0.0};
    double                                m_time            {0.0};
    uint64_t                              m_frame_number    {0};
    bool                                  m_mouse_pressed   {false};
};

void run()
{
    // Workaround for
    // https://intellij-support.jetbrains.com/hc/en-us/community/posts/27792220824466-CMake-C-git-project-How-to-share-working-directory-in-git
    erhe::file::ensure_working_directory_contains(erhe::c_erhe_config_file_path);

    erhe::log::initialize_log_sinks();

    hello_swap::initialize_logging();
#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
    gl::initialize_logging();
#endif
    erhe::graphics::initialize_logging();
    erhe::math::initialize_logging();
    erhe::window::initialize_logging();
    // TODO: RenderDoc vulkan support
    // erhe::window::initialize_frame_capture();

    Hello_swap hello_swap{};
    hello_swap.run();
}

} // namespace hello_swap

