#pragma once

#include "erhe/imgui/imgui_window.hpp"

#include <memory>

namespace erhe::application
{
    class Rendergraph;
}

namespace editor
{

class Editor_scenes;

class Rendergraph_window
    : public erhe::imgui::Imgui_window
{
public:
    static constexpr std::string_view c_title{"Render Graph"};

    Rendergraph_window();

    // Implements Imgui_window
    void imgui() override;

private:
    Editor_scenes&                  m_editor_scenes;
    erhe::rendergraph::Rendergraph& m_render_graph;
};

} // namespace editor
