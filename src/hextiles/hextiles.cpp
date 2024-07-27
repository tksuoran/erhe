#include "hextiles.hpp"
#include "hextiles_log.hpp"
#include "hextiles_settings.hpp"

#include "map_editor/map_editor.hpp"
#include "map_window.hpp"
#include "menu_window.hpp"
#include "new_game_window.hpp"
#include "tile_renderer.hpp"
#include "tiles.hpp"
#include "type_editors/type_editor.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_commands/commands_log.hpp"
#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_gl/gl_log.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/pipeline.hpp"
#include "erhe_imgui/imgui_log.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_log/log.hpp"
#include "erhe_renderer/pipeline_renderpass.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_renderer/text_renderer.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_window/renderdoc_capture.hpp"
#include "erhe_window/window_log.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_event_handler.hpp"
#include "erhe_ui/ui_log.hpp"

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
        , m_settings         {m_context_window}
        , m_commands         {}
        , m_graphics_instance{m_context_window}
        , m_text_renderer    {m_graphics_instance}
        , m_rendergraph      {m_graphics_instance}
        , m_imgui_renderer   {m_graphics_instance, m_settings.imgui}
        , m_imgui_windows    {m_imgui_renderer, &m_context_window, m_rendergraph, ""}
        , m_tiles            {}
        , m_tile_renderer    {m_graphics_instance, m_imgui_renderer, m_tiles}
        , m_map_window       {m_commands, m_graphics_instance, m_imgui_renderer, m_imgui_windows, m_text_renderer, m_tile_renderer}
        , m_menu_window      {m_commands, m_imgui_renderer, m_imgui_windows, *this, m_map_window, m_tiles, m_tile_renderer}
    {
        gl::clip_control(gl::Clip_control_origin::lower_left, gl::Clip_control_depth::zero_to_one);
        gl::enable      (gl::Enable_cap::framebuffer_srgb);

        //// auto& root_event_handler = m_context_window.get_root_window_event_handler();
        //// root_event_handler.attach(&m_imgui_windows, 2);
        //// root_event_handler.attach(&m_commands, 1);
    }

    void run()
    {
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
            tick();
            m_context_window.swap_buffers();
        }
    }

    void tick()
    {
        if (m_map_window.is_window_visible()) {
            m_map_window.render();
        }
        auto& input_events = m_context_window.get_input_events();
        m_imgui_windows .imgui_windows();
        m_commands.tick(input_events);
        m_rendergraph   .execute();
        m_imgui_renderer.next_frame();
        m_tile_renderer .next_frame();
    }

    auto on_window_close_event(const erhe::window::Window_close_event&) -> bool override
    {
        m_close_requested = true;
        return true;
    }

    erhe::window::Context_window   m_context_window;
    Hextiles_settings              m_settings;
    erhe::commands::Commands       m_commands;
    erhe::graphics::Instance       m_graphics_instance;
    erhe::renderer::Text_renderer  m_text_renderer;
    erhe::rendergraph::Rendergraph m_rendergraph;
    erhe::imgui::Imgui_renderer    m_imgui_renderer;
    erhe::imgui::Imgui_windows     m_imgui_windows;
    Tiles                          m_tiles;
    Tile_renderer                  m_tile_renderer;
    Map_window                     m_map_window;
    Menu_window                    m_menu_window;
    bool                           m_close_requested{false};
};

void run_hextiles()
{
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

    Hextiles hextiles{};
    hextiles.run();
}


} // namespace hextiles
