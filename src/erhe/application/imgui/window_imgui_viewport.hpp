#pragma once

#include "erhe/application/imgui/imgui_viewport.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"
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

/// <summary>
/// Imgui_viewport which displays Imgui_window instance into (glfw) Window.
/// </summary>
class Window_imgui_viewport
    : public Imgui_viewport
{
public:
    explicit Window_imgui_viewport(const std::string_view name);

    // Implements Rendergraph_node
    void execute_rendergraph_node() override;
    [[nodiscard]] auto get_producer_output_viewport(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> erhe::scene::Viewport override;

    // Implements Imgui_vewport
    [[nodiscard]] auto begin_imgui_frame() -> bool override;
    void end_imgui_frame     () override;
};

} // namespace erhe::application
