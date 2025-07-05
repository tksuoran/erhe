#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <memory>

namespace erhe::application { class Rendergraph; }

namespace editor {

class App_scenes;

class Rendergraph_window
    : public erhe::imgui::Imgui_window
{
public:
    static constexpr std::string_view c_title{"Render Graph"};

    Rendergraph_window();

    // Implements Imgui_window
    void imgui() override;

private:
    App_scenes&                     m_app_scenes;
    erhe::rendergraph::Rendergraph& m_render_graph;
};

}
