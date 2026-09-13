#include "scene/draw_mode_renderer.hpp"

#include "renderers/render_context.hpp"
#include "scene/draw_mode.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"

#include "erhe_scene/draw_mode_description.hpp"
#include "erhe_scene/node.hpp"

#include <algorithm>

namespace editor {

namespace {

// The stencil reference the editor's depth-tested overlay lines use.
constexpr unsigned int c_stencil_reference = 2;
constexpr float        c_line_thickness    = 2.0f;

// The axis lines of `origin` are a fraction of the extent, which is what the
// imaging adapter sizes them from.
constexpr float c_origin_axis_fraction = 0.5f;

} // anonymous namespace

Draw_mode_renderer::Draw_mode_renderer(App_context& context, Tools& tools)
    : Tool{context, tools, Tool_flags::background}
{
    set_description("Draw Mode");
}

void Draw_mode_renderer::submit_bounds(const glm::vec3& min, const glm::vec3& max)
{
    const glm::vec3 corner[8] = {
        glm::vec3{min.x, min.y, min.z},
        glm::vec3{max.x, min.y, min.z},
        glm::vec3{max.x, max.y, min.z},
        glm::vec3{min.x, max.y, min.z},
        glm::vec3{min.x, min.y, max.z},
        glm::vec3{max.x, min.y, max.z},
        glm::vec3{max.x, max.y, max.z},
        glm::vec3{min.x, max.y, max.z}
    };
    static constexpr int edge[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };
    for (const int (&e)[2] : edge) {
        m_lines.push_back(erhe::renderer::Line{corner[e[0]], corner[e[1]]});
    }
}

void Draw_mode_renderer::submit_origin(const glm::vec3& min, const glm::vec3& max)
{
    const glm::vec3 size   = max - min;
    const float     length = c_origin_axis_fraction * std::max(std::max(size.x, size.y), size.z);
    if (!(length > 0.0f)) {
        return;
    }
    m_lines.push_back(erhe::renderer::Line{glm::vec3{0.0f}, glm::vec3{length, 0.0f, 0.0f}});
    m_lines.push_back(erhe::renderer::Line{glm::vec3{0.0f}, glm::vec3{0.0f, length, 0.0f}});
    m_lines.push_back(erhe::renderer::Line{glm::vec3{0.0f}, glm::vec3{0.0f, 0.0f, length}});
}

void Draw_mode_renderer::tool_render(const Render_context& context)
{
    const std::shared_ptr<Scene_root> scene_root = context.scene_view.get_scene_root();
    if (!scene_root) {
        return;
    }
    const std::vector<std::shared_ptr<Draw_mode>>& draw_modes = scene_root->get_draw_modes();
    if (draw_modes.empty()) {
        return;
    }

    // Depth-tested, occluded parts hidden: the adapter draws the proxy as the
    // geometry it stands for, not as an overlay.
    erhe::renderer::Primitive_renderer line_renderer = context.get(
        {erhe::graphics::Primitive_type::line, c_stencil_reference, true, false}
    );
    line_renderer.set_thickness(c_line_thickness);

    for (const std::shared_ptr<Draw_mode>& draw_mode : draw_modes) {
        if (!draw_mode || !draw_mode->is_active() || !draw_mode->is_visible()) {
            continue;
        }
        const erhe::scene::Node* const node = draw_mode->get_node();
        if (node == nullptr) {
            continue;
        }
        const erhe::scene::Draw_mode mode = draw_mode->resolved_draw_mode();
        if ((mode == erhe::scene::Draw_mode::default_) || (mode == erhe::scene::Draw_mode::inherited)) {
            continue;
        }
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
        if (!draw_mode->get_extent(min, max)) {
            continue;
        }
        m_lines.clear();
        switch (mode) {
            case erhe::scene::Draw_mode::origin: {
                submit_origin(min, max);
                break;
            }
            case erhe::scene::Draw_mode::bounds: {
                submit_bounds(min, max);
                break;
            }
            // `cards` draws no lines: its proxy is the generated quad
            // geometry the attachment owns (Draw_mode::get_card_proxy),
            // which the ordinary content passes render.
            default: {
                break;
            }
        }
        if (m_lines.empty()) {
            continue;
        }
        const glm::vec3 color = draw_mode->get_value(Draw_mode::draw_mode_color_property);
        line_renderer.add_lines(node->world_from_node(), glm::vec4{color, 1.0f}, m_lines);
    }
    m_lines.clear();
}

} // namespace editor
