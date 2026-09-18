#pragma once

#include "tools/tool.hpp"

#include "erhe_renderer/primitive_renderer.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace editor {

class App_context;
class Draw_mode;
class Tools;

// Draws the proxy a prim's draw mode asks for, per viewport
// (doc/erhe/usd_compatibility.md, "Draw modes"). A background tool rather than a
// window: it has no state and no UI, it only submits lines for the draw-mode
// attachments the rendered scene registered with its Scene_root.
//
// `bounds` is the extent box and `origin` the three axis lines from the
// prim's origin, both in the prim's own space and in the draw-mode color.
// `cards` needs nothing here: its proxy is generated quad geometry the
// attachment owns as a child prim, which the ordinary content passes render.
class Draw_mode_renderer : public Tool
{
public:
    Draw_mode_renderer(App_context& context, Tools& tools);

    // Implements Tool
    void tool_render(const Render_context& context) override;

private:
    void submit_bounds(const glm::vec3& min, const glm::vec3& max);
    void submit_origin(const glm::vec3& min, const glm::vec3& max);

    // Persistent scratch: cleared at the point of use, capacity kept, so a
    // steady frame allocates nothing.
    std::vector<erhe::renderer::Line> m_lines;
};

} // namespace editor
