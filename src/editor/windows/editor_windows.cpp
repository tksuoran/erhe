#include "windows/editor_windows.hpp"

#include "app_context.hpp"
#include "geometry_graph/geometry_graph_window.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "texture_graph/graph_texture.hpp"
#include "texture_graph/texture_graph_window.hpp"
#include "windows/properties.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_item/item.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace editor {

namespace {

// Drop (and destroy) the extra instances whose window was closed by the user.
// erase() releases the last shared_ptr, running ~Imgui_window ->
// unregister_imgui_window; the caller guarantees this runs outside ImGui
// iteration.
template <typename T>
void prune_closed(std::vector<std::shared_ptr<T>>& windows)
{
    windows.erase(
        std::remove_if(
            windows.begin(),
            windows.end(),
            [](const std::shared_ptr<T>& window) {
                return !window->is_window_visible();
            }
        ),
        windows.end()
    );
}

} // anonymous namespace

Editor_windows::Editor_windows(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context
)
    : m_imgui_renderer{imgui_renderer}
    , m_imgui_windows {imgui_windows}
    , m_app_context   {app_context}
{
}

void Editor_windows::open_properties_window(const std::shared_ptr<erhe::Item_base>& target)
{
    // Defer construction to flush_queue() (outside ImGui iteration): open_*
    // is called from context menus / double-click (inside iteration) and
    // from off-thread MCP.
    m_imgui_windows.queue(
        [this, target]() {
            const std::string title = fmt::format("Properties [{}]", ++m_properties_counter);
            std::shared_ptr<Properties> window = std::make_shared<Properties>(
                m_imgui_renderer, m_imgui_windows, m_app_context, title, std::string_view{}
            );
            window->set_target(target);
            window->show_window();
            m_properties_windows.push_back(window);
        }
    );
}

void Editor_windows::open_geometry_graph_window(const std::shared_ptr<Graph_mesh>& target)
{
    m_imgui_windows.queue(
        [this, target]() {
            const std::string title = fmt::format("Geometry Graph [{}]", ++m_geometry_graph_counter);
            std::shared_ptr<Geometry_graph_window> window = std::make_shared<Geometry_graph_window>(
                m_imgui_renderer, m_imgui_windows, m_app_context, title, std::string_view{}
            );
            window->set_target(target);
            window->show_window();
            m_geometry_graph_windows.push_back(window);
        }
    );
}

void Editor_windows::open_texture_graph_window(const std::shared_ptr<Graph_texture>& target)
{
    m_imgui_windows.queue(
        [this, target]() {
            const std::string title = fmt::format("Texture Graph [{}]", ++m_texture_graph_counter);
            std::shared_ptr<Texture_graph_window> window = std::make_shared<Texture_graph_window>(
                m_imgui_renderer, m_imgui_windows, m_app_context, title, std::string_view{}
            );
            window->set_target(target);
            window->show_window();
            m_texture_graph_windows.push_back(window);
        }
    );
}

void Editor_windows::update_once_per_frame()
{
    prune_closed(m_properties_windows);
    prune_closed(m_geometry_graph_windows);
    prune_closed(m_texture_graph_windows);
}

} // namespace editor
