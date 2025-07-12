#include "hextiles.hpp"
#include "hextiles_log.hpp"
#include "hextiles_settings.hpp"

#include "map_editor/map_editor.hpp"
#include "map_window.hpp"
#include "menu_window.hpp"
#include "tile_renderer.hpp"
#include "tiles.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_commands/commands_log.hpp"
#include "erhe_gl/gl_log.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/device.hpp"
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
#include "erhe_window/renderdoc_capture.hpp"
#include "erhe_window/window_log.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_event_handler.hpp"
#include "erhe_ui/ui_log.hpp"

#if defined(ERHE_OS_LINUX)
#   include <unistd.h>
#   include <limits.h>
#endif

namespace hextiles {

class Hextiles : public erhe::window::Input_event_handler
{
public:
    Hextiles()
        : m_context_window{
            erhe::window::Window_configuration{
                .gl_major          = 4,
                .gl_minor          = 6,
                .width             = 1920,
                .height            = 1080,
                .msaa_sample_count = 0,
                .title             = "erhe HexTiles by Timo Suoranta"
            }
        }
        , m_settings            {m_context_window}
        , m_commands            {}
        , m_graphics_device     {m_context_window}
        , m_text_renderer       {m_graphics_device}
        , m_rendergraph         {m_graphics_device}
        , m_imgui_renderer      {m_graphics_device, m_settings.imgui}
        , m_imgui_windows       {m_imgui_renderer, m_graphics_device, &m_context_window, m_rendergraph, "windows.ini"}
        , m_logs                {m_commands, m_imgui_renderer}
        , m_log_settings_window {m_imgui_renderer, m_imgui_windows, m_logs}
        , m_tail_log_window     {m_imgui_renderer, m_imgui_windows, m_logs}
        , m_frame_log_window    {m_imgui_renderer, m_imgui_windows, m_logs}
        , m_performance_window  {m_imgui_renderer, m_imgui_windows}
        , m_tiles               {}
        , m_tile_renderer       {m_graphics_device, m_imgui_renderer, m_tiles}
        , m_map_window          {m_commands, m_graphics_device, m_imgui_renderer, m_imgui_windows, m_text_renderer, m_tile_renderer}
        , m_menu_window         {m_commands, m_imgui_renderer, m_imgui_windows, *this, m_map_window, m_tiles, m_tile_renderer}
    {
        gl::clip_control(gl::Clip_control_origin::lower_left, gl::Clip_control_depth::zero_to_one);
        gl::enable      (gl::Enable_cap::framebuffer_srgb);

        //// auto& root_event_handler = m_context_window.get_root_window_event_handler();
        //// root_event_handler.attach(&m_imgui_windows, 2);
        //// root_event_handler.attach(&m_commands, 1);
    }

    void run()
    {
        int64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        while (!m_close_requested) {
            m_context_window.poll_events();
            auto& input_events = m_context_window.get_input_events();
            for (erhe::window::Input_event& input_event : input_events) {
                dispatch_input_event(input_event);
                if (!input_event.handled) {
                    m_imgui_windows.dispatch_input_event(input_event);
                }
                if (!input_event.handled) {
                    m_commands.dispatch_input_event(input_event);
                }
            }
            int64_t new_timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            int64_t delta_time = new_timestamp_ns - timestamp_ns;
            double dt_s = static_cast<double>(delta_time) / 1'000'000'000.0f;
            tick(static_cast<float>(dt_s), timestamp_ns);
            m_graphics_device.end_of_frame();
            m_context_window.swap_buffers();
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
                    m_context_window.handle_window_close_event(timestamp_ns);
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
        if (m_map_window.is_window_visible()) {
            m_map_window.render();
        }
        auto& input_events = m_context_window.get_input_events();
        m_imgui_windows .process_events(dt_s, timestamp_ns);
        m_imgui_windows .begin_frame();
        viewport_menu();
        m_imgui_windows .draw_imgui_windows();
        m_imgui_windows .end_frame();
        m_commands.tick(timestamp_ns, input_events);
        m_rendergraph   .execute();
        m_imgui_renderer.next_frame();
    }

    auto on_window_close_event(const erhe::window::Input_event&) -> bool override
    {
        m_close_requested = true;
        return true;
    }

    erhe::window::Context_window     m_context_window;
    Hextiles_settings                m_settings;
    erhe::commands::Commands         m_commands;
    erhe::graphics::Device           m_graphics_device;
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
    {
        std::error_code error_code{};
        bool found = std::filesystem::exists("erhe.ini", error_code);
        if (!found) {
            std::string path_string{};
            std::filesystem::path path = std::filesystem::current_path();
            path_string = path.string();
            fprintf(stdout, "erhe.ini not found.\nCurrent working directory is %s\n", path_string.c_str());
#if defined(ERHE_OS_LINUX)
            char self_path[PATH_MAX];
            ssize_t length = readlink("/proc/self/exe", self_path, PATH_MAX - 1);
            if (length > 0) {
                self_path[length] = '\0';
                fprintf(stdout, "Executable is %s\n", self_path);
            }
#endif

            for (int i = 0; i < 4; ++i) {
                path = path.parent_path();
                std::filesystem::current_path(path, error_code);
                path = std::filesystem::current_path();
                path_string = path.string();
                fprintf(stdout, "Current working directory is %s\n", path_string.c_str());
            }

            path = path / std::filesystem::path("src/hextiles");
            std::filesystem::current_path(path, error_code);
            path = std::filesystem::current_path();
            path_string = path.string();
            fprintf(stdout, "Current working directory is %s\n", path_string.c_str());
        }
    }

    erhe::log::initialize_log_sinks();
    gl::initialize_logging();
    erhe::commands::initialize_logging();
    erhe::graphics::initialize_logging();
    erhe::imgui::initialize_logging();
    erhe::renderer::initialize_logging();
    erhe::rendergraph::initialize_logging();
    erhe::window::initialize_logging();
    erhe::ui::initialize_logging();
    erhe::window::initialize_frame_capture();
    hextiles::initialize_logging();

    std::unique_ptr<Hextiles> hextiles = std::make_unique<Hextiles>();
    hextiles->run();
}


} // namespace hextiles
