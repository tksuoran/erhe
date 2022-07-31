#pragma once

#include "erhe/application/imgui_viewport.hpp"
#include "erhe/application/render_graph_node.hpp"
#include "erhe/components/components.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/viewport.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

#include <gsl/gsl>

struct ImFontAtlas;

namespace erhe::components
{
    class Components;
}

namespace erhe::graphics
{
    class Framebuffer;
    class OpenGL_state_tracker;
    class Texture;
}

namespace erhe::scene
{
    class Mesh;
}

namespace erhe::application
{

class View;
class Imgui_renderer;
class Imgui_window;
class Imgui_windows;

class Window_imgui_viewport
    : public Imgui_viewport
{
public:
    Window_imgui_viewport(
        const std::string_view        name,
        erhe::components::Components& components
    );

    void post_initialize(
        erhe::components::Components& components
    );

    // Implements Render_graph_node
    void execute_render_graph_node() override;

    // Implements Imgui_vewport
    [[nodiscard]] auto begin_imgui_frame() -> bool override;
    void end_imgui_frame() override;

private:
    void menu();

    std::shared_ptr<Imgui_renderer>          m_imgui_renderer;
    Imgui_windows*                           m_imgui_windows{nullptr};
    std::shared_ptr<erhe::application::View> m_view;
    std::shared_ptr<Window>                  m_window;
};

} // namespace erhe::application
