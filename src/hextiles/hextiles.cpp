#include "hextiles.hpp"
#include "hextiles_log.hpp"
#include "hextiles_settings.hpp"

#include "erhe_verify/verify.hpp"

#include <SDL3/SDL.h>

#include "map_editor/map_editor.hpp"
#include "map_window.hpp"
#include "menu_window.hpp"
#include "tile_renderer.hpp"
#include "tiles.hpp"

#include "erhe_codegen/config_io.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_commands/commands_log.hpp"
#include "erhe_file/file.hpp"
#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_gl/gl_log.hpp"
#endif
#include "erhe_graph/graph_log.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/generated/graphics_config.hpp"
#include "erhe_graphics/generated/graphics_config_serialization.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_imgui/imgui_log.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/windows/log_window.hpp"
#include "erhe_imgui/windows/performance_window.hpp"
#include "erhe_imgui/scoped_imgui_context.hpp"
#include "erhe_imgui/window_imgui_host.hpp"
#include "erhe_log/log.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_renderer/text_renderer.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_window/window_log.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_event_handler.hpp"
#include "erhe_ui/ui_log.hpp"

#include <atomic>

#if defined(ERHE_OS_LINUX)
#   include <unistd.h>
#   include <limits.h>
#endif

namespace hextiles {

class Hextiles : public erhe::window::Input_event_handler
{
public:
    Hextiles()
        : m_graphics_config{
            erhe::codegen::load_config<Graphics_config>("config/hextiles/erhe_graphics.json")
        }
        // TODO m_window_config etc.
        , m_window{
            erhe::window::Window_configuration{
                .size                     = glm::ivec2{1920, 1080},
                .title                    = "erhe HexTiles by Timo Suoranta",
                .initialize_frame_capture = m_graphics_config.renderdoc_capture_support
            }
        }
        , m_settings{m_window}
        , m_commands{}

        , m_graphics_device{
            erhe::graphics::Surface_create_info{
                .context_window            = &m_window,
                .prefer_low_bandwidth      = false,
                .prefer_high_dynamic_range = false
            },
            erhe::codegen::load_config<Graphics_config>("config/hextiles/erhe_graphics.json"),
            [](erhe::graphics::Message_severity severity, const std::string& error_message, const std::string& callstack) {
                if (severity == erhe::graphics::Message_severity::error) {
                    std::string clipboard_text = error_message + "\n=== Callstack ===\n" + callstack;
                    SDL_SetClipboardText(clipboard_text.c_str());
                    ERHE_FATAL("Device error (copied to clipboard): %s", error_message.c_str());
                }
            }
        }

        , m_shader_error_callback_set{(
            m_graphics_device.set_shader_error_callback(
                [](const std::string& error_log, const std::string& shader_source, const std::string& callstack) {
                    std::string clipboard_text = "=== Shader Error ===\n" + error_log + "\n=== Shader Source ===\n" + shader_source + "\n=== Callstack ===\n" + callstack;
                    SDL_SetClipboardText(clipboard_text.c_str());
                    ERHE_FATAL("Shader compilation/linking failed (error and source copied to clipboard)");
                }
            ),
            m_graphics_device.set_trace_callback(
                [](const std::string& message) {
                    SDL_SetClipboardText(message.c_str());
                }
            ),
        true)}

        // Init-time command buffer. The constructor records all
        // construction-time GPU work (Tile_renderer index buffer upload,
        // tileset texture composition, Imgui_renderer font texture upload,
        // Text_renderer font texture upload) into this single cb. The
        // constructor body ends + submits the cb and wait_idles before
        // returning so every uploaded resource is GPU-ready by the time
        // the main loop starts.
        , m_init_command_buffer{[this]() -> erhe::graphics::Command_buffer& {
            const bool init_wait_frame_ok = m_graphics_device.wait_frame();
            ERHE_VERIFY(init_wait_frame_ok);
            erhe::graphics::Command_buffer& cb = m_graphics_device.get_command_buffer(0);
            cb.begin();
            return cb;
        }()}

        , m_text_renderer       {m_graphics_device, m_init_command_buffer}
        , m_rendergraph         {m_graphics_device}
        , m_imgui_renderer      {m_graphics_device, m_init_command_buffer, m_settings.imgui}
        , m_imgui_windows       {m_imgui_renderer, m_graphics_device, m_rendergraph, &m_window, "config/hextiles/"}
        , m_logs                {m_commands, m_imgui_renderer, "config/hextiles/logging.json"}
        , m_log_settings_window {m_imgui_renderer, m_imgui_windows, m_logs}
        , m_tail_log_window     {m_imgui_renderer, m_imgui_windows, m_logs}
        , m_frame_log_window    {m_imgui_renderer, m_imgui_windows, m_logs}
        , m_performance_window  {m_imgui_renderer, m_imgui_windows}
        , m_tiles               {}
        , m_tile_renderer       {m_graphics_device, m_init_command_buffer, m_imgui_renderer, m_tiles}
        , m_map_window          {m_commands, m_graphics_device, m_imgui_renderer, m_imgui_windows, m_text_renderer, m_tile_renderer}
        , m_menu_window         {m_commands, m_imgui_renderer, m_imgui_windows, *this, m_map_window, m_tiles, m_tile_renderer}
    {
        m_window.set_title(
            erhe::window::format_window_title("erhe HexTiles by Timo Suoranta", m_graphics_device.get_info().api_info)
        );

        //// auto& root_event_handler = m_window.get_root_window_event_handler();
        //// root_event_handler.attach(&m_imgui_windows, 2);
        //// root_event_handler.attach(&m_commands, 1);
        m_last_window_width  = m_window.get_width();
        m_last_window_height = m_window.get_height();

        m_window.register_redraw_callback(
            [this](){
                if (m_in_tick.load()) {
                    return;
                }
                if ((m_last_window_width < 1) || (m_last_window_height < 1)) {
                    return;
                }
                if (
                    (m_last_window_width  != m_window.get_width()) ||
                    (m_last_window_height != m_window.get_height())
                ) {
                    m_request_resize_pending.store(true);
                    m_last_window_width  = m_window.get_width();
                    m_last_window_height = m_window.get_height();
                }
                // TODO throttle redraws - if last redraw was less than live resize redraw threshold limit ago, don't redraw
                if ((m_last_window_width != 0) && (m_last_window_height != 0)) {
                    int64_t new_timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    tick(0.01f, new_timestamp_ns);
                }
            }
        );

        // Close the init-time command buffer opened in the member init
        // list, submit, and block until the GPU has consumed every
        // recorded upload + clear so the resources are ready for the
        // main render loop.
        m_init_command_buffer.end();
        erhe::graphics::Command_buffer* init_cbs[] = { &m_init_command_buffer };
        m_graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{init_cbs});
        m_graphics_device.wait_idle();

        // Advance the frame index so subsequent frames are paced on the
        // device timeline.
        const bool init_end_frame_ok = m_graphics_device.end_frame();
        ERHE_VERIFY(init_end_frame_ok);
    }

    std::optional<erhe::window::Input_event> m_window_resize_event{};
    int               m_last_window_width     {0};
    int               m_last_window_height    {0};
    uint32_t          m_swapchain_width       {0};
    uint32_t          m_swapchain_height      {0};
    std::atomic<bool> m_request_resize_pending{false};
    std::atomic<bool> m_in_tick              {false};

    auto on_window_resize_event(const erhe::window::Input_event& input_event) -> bool override
    {
        m_window_resize_event = input_event;
        return true;
    }

    std::mutex m_mutex;
    void run()
    {
        int64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
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

            int64_t new_timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            int64_t delta_time = new_timestamp_ns - timestamp_ns;
            double dt_s = static_cast<double>(delta_time) / 1'000'000'000.0f;
            tick(static_cast<float>(dt_s), timestamp_ns);
            timestamp_ns = new_timestamp_ns;
        }
    }

    void viewport_menu()
    {
        erhe::imgui::Imgui_host* imgui_host_ = m_imgui_windows.get_window_imgui_host().get(); // get window hosted viewport
        if (imgui_host_ == nullptr) {
            return;
        }
        erhe::imgui::Imgui_host& imgui_host = *imgui_host_;

        erhe::imgui::Scoped_imgui_context imgui_context{imgui_host};

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 10.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{10.0f, 2.0f});

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) {
                    int64_t timestamp_ns = 0; // TODO
                    m_window.handle_window_close_event(timestamp_ns);
                }
                ImGui::EndMenu();
            }
            if (true) {
                if (ImGui::BeginMenu("Developer")) {
                    m_imgui_windows.window_menu_entries(imgui_host, true);
                    ImGui::EndMenu();
                }
            }

            const auto& menu_bindings = m_commands.get_menu_bindings();
            for (const erhe::commands::Menu_binding& menu_binding : menu_bindings) {
                const std::string& menu_path = menu_binding.get_menu_path();
                std::size_t path_end = menu_path.length();
                std::size_t offset = 0;
                std::vector<std::string> menu_path_entries;
                while (true) {
                    std::size_t separator_position = menu_path.find_first_of('.', offset);
                    if (separator_position == std::string::npos) {
                        separator_position = path_end;
                    }
                    std::size_t span_length = separator_position - offset;
                    if (span_length > 1) {
                        const std::string menu_label = menu_path.substr(offset, span_length);
                        menu_path_entries.emplace_back(menu_label);
                    }
                    offset = separator_position + 1;
                    if (offset >= path_end) {
                        break;
                    }
                }
                bool activate = false;
                size_t begin_menu_count = 0;
                for (size_t i = 0, end = menu_path_entries.size(); i < end; ++i) {
                    const std::string& label = menu_path_entries[i];
                    if (i == end - 1) {
                        activate = ImGui::MenuItem(label.c_str());
                    } else {
                        if (!ImGui::BeginMenu(label.c_str())) {
                            break;
                        }
                        ++begin_menu_count;
                    }
                }
                for (size_t i = 0; i < begin_menu_count; ++i) {
                    ImGui::EndMenu();
                }
                if (activate) {
                    erhe::commands::Command* command = menu_binding.get_command();
                    if (command != nullptr) {
                        if (command->is_enabled()) {
                            command->try_call();
                        }
                    }
                }
            }

            if (ImGui::BeginMenu("Window")) {
                m_imgui_windows.window_menu_entries(imgui_host, false);

                //// ImGui::Separator();
                //// 
                if (ImGui::BeginMenu("ImGui")) {
                    ImGui::MenuItem("Demo",             "", &m_imgui_builtin_windows.demo);
                    ImGui::MenuItem("Metrics/Debugger", "", &m_imgui_builtin_windows.metrics);
                    ImGui::MenuItem("Debug Log",        "", &m_imgui_builtin_windows.debug_log);
                    ImGui::MenuItem("ID Stack Tool",    "", &m_imgui_builtin_windows.id_stack_tool);
                    ImGui::MenuItem("About",            "", &m_imgui_builtin_windows.about);
                    ImGui::MenuItem("Style Editor",     "", &m_imgui_builtin_windows.style_editor);
                    ImGui::MenuItem("User Guide",       "", &m_imgui_builtin_windows.user_guide);
                    ImGui::EndMenu();
                }

                ImGui::Separator();

                const auto& windows = m_imgui_windows.get_windows();
                if (ImGui::MenuItem("Close All")) {
                    for (const auto& window : windows) {
                        window->hide_window();
                    }
                }
                if (ImGui::MenuItem("Open All")) {
                    for (const auto& window : windows) {
                        window->show_window();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        ImGui::PopStyleVar(2);

        if (m_imgui_builtin_windows.demo) {
            ImGui::ShowDemoWindow(&m_imgui_builtin_windows.demo);
        }

        if (m_imgui_builtin_windows.metrics) {
            ImGui::ShowMetricsWindow(&m_imgui_builtin_windows.metrics);
        }

        if (m_imgui_builtin_windows.debug_log) {
            ImGui::ShowDebugLogWindow(&m_imgui_builtin_windows.debug_log);
        }

        if (m_imgui_builtin_windows.id_stack_tool) {
            ImGui::ShowIDStackToolWindow(&m_imgui_builtin_windows.id_stack_tool);
        }

        if (m_imgui_builtin_windows.about) {
            ImGui::ShowAboutWindow(&m_imgui_builtin_windows.about);
        }

        if (m_imgui_builtin_windows.style_editor) {
            ImGui::Begin("Dear ImGui Style Editor", &m_imgui_builtin_windows.style_editor);
            ImGui::ShowStyleEditor();
            ImGui::End();
        }
    }

    void tick(float dt_s, int64_t timestamp_ns)
    {
        m_in_tick.store(true);
        std::lock_guard<std::mutex> lock{m_mutex};

        const bool wait_ok = m_graphics_device.wait_frame();
        ERHE_VERIFY(wait_ok);

        const erhe::graphics::Frame_begin_info frame_begin_info{
            .resize_width   = static_cast<uint32_t>(m_last_window_width),
            .resize_height  = static_cast<uint32_t>(m_last_window_height),
            .request_resize = m_request_resize_pending.load()
        };
        m_request_resize_pending.store(false);

        // Allocate this frame's command buffer + drive the swapchain through
        // it (acquire + per-slot acquire/present semaphore setup).
        erhe::graphics::Command_buffer& command_buffer = m_graphics_device.get_command_buffer(0);
        erhe::graphics::Frame_state frame_state{};
        const bool wait_swap_ok  = command_buffer.wait_for_swapchain(frame_state);
        const bool should_render = wait_swap_ok && command_buffer.begin_swapchain(frame_begin_info, frame_state);

        if (should_render) {
            command_buffer.begin();

            std::vector<erhe::window::Input_event>& input_events = m_window.get_input_events();

            m_imgui_windows .process_events(dt_s, timestamp_ns);
            m_commands.tick(timestamp_ns, input_events);

            m_imgui_windows .begin_frame();
            viewport_menu();
            m_imgui_windows .draw_imgui_windows();
            m_imgui_windows .end_frame();

            if (m_map_window.is_window_visible()) {
                m_map_window.render(command_buffer);
            }
            m_rendergraph   .execute(command_buffer);
            m_imgui_renderer.next_frame();

            command_buffer.end();

            const erhe::graphics::Frame_end_info frame_end_info{
                .requested_display_time = 0 // TODO
            };
            command_buffer.end_swapchain(frame_end_info);

            // Submit + implicit present.
            erhe::graphics::Command_buffer* cbs[] = { &command_buffer };
            m_graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{cbs});
        }

        const bool end_frame_ok = m_graphics_device.end_frame();
        ERHE_VERIFY(end_frame_ok);
        m_in_tick.store(false);
    }

    auto on_window_close_event(const erhe::window::Input_event&) -> bool override
    {
        m_close_requested = true;
        return true;
    }

    Graphics_config                  m_graphics_config;
    erhe::window::Context_window     m_window;
    Hextiles_settings                m_settings;
    erhe::commands::Commands         m_commands;
    erhe::graphics::Device           m_graphics_device;
    bool                             m_shader_error_callback_set{false};
    erhe::graphics::Command_buffer&  m_init_command_buffer;
    erhe::renderer::Text_renderer    m_text_renderer;
    erhe::rendergraph::Rendergraph   m_rendergraph;
    erhe::imgui::Imgui_renderer      m_imgui_renderer;
    erhe::imgui::Imgui_windows       m_imgui_windows;
    erhe::imgui::Logs                m_logs;
    erhe::imgui::Log_settings_window m_log_settings_window;
    erhe::imgui::Tail_log_window     m_tail_log_window;
    erhe::imgui::Frame_log_window    m_frame_log_window;
    erhe::imgui::Performance_window  m_performance_window;

    Tiles                          m_tiles;
    Tile_renderer                  m_tile_renderer;
    Map_window                     m_map_window;
    Menu_window                    m_menu_window;
    bool                           m_close_requested{false};

    class Imgui_builtin_windows
    {
    public:
        bool demo         {false};
        bool metrics      {false};
        bool debug_log    {false};
        bool id_stack_tool{false};
        bool about        {false};
        bool style_editor {false};
        bool user_guide   {false};
    };
    Imgui_builtin_windows m_imgui_builtin_windows;
};

void run_hextiles()
{
    // Workaround for
    // https://intellij-support.jetbrains.com/hc/en-us/community/posts/27792220824466-CMake-C-git-project-How-to-share-working-directory-in-git
    erhe::file::ensure_working_directory_contains("config/hextiles/erhe_graphics.json");

    erhe::log::redirect_stderr_to_file("logs/stderr.txt");
    erhe::log::initialize_log_sinks();
    {
        std::optional<std::string> contents = erhe::file::read("logging config", "config/hextiles/logging.json");
        if (contents.has_value()) {
            erhe::log::load_log_configuration(contents.value());
        }
    }
#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
    gl::initialize_logging();
#endif
    erhe::commands::initialize_logging();
    erhe::graph::initialize_logging();
    erhe::graphics::initialize_logging();
    erhe::imgui::initialize_logging();
    erhe::renderer::initialize_logging();
    erhe::rendergraph::initialize_logging();
    erhe::window::initialize_logging();
    erhe::ui::initialize_logging();
    hextiles::initialize_logging();

    std::unique_ptr<Hextiles> hextiles = std::make_unique<Hextiles>();
    hextiles->run();
}


} // namespace hextiles
