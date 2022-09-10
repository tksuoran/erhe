#pragma once

#include "tools/tool.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

namespace erhe::application
{
    class Line_renderer_set;
}

namespace editor
{

class Grid_tool
    : public erhe::application::Imgui_window
    , public erhe::components::Component
    , public Tool
{
public:
    static constexpr std::string_view c_type_name{"Grid_tool"};
    static constexpr std::string_view c_title{"Grid"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Grid_tool ();
    ~Grid_tool() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override;
    void tool_render(const Render_context& context)  override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    void viewport_toolbar();
    [[nodiscard]] auto snap(const glm::vec3 v) const -> glm::vec3;
    void set_major_color(const glm::vec4 color);
    void set_minor_color(const glm::vec4 color);

private:
    // Component dependencies
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;

    bool      m_enable     {false};
    float     m_cell_size  {1.0f};
    int       m_cell_div   {10};
    int       m_cell_count {2};
    float     m_thickness  {-2.0f};
    glm::vec3 m_center     {0.0f};
    glm::vec4 m_major_color{0.065f, 0.065f, 0.065f, 1.0f};
    glm::vec4 m_minor_color{0.235f, 0.235f, 0.235f, 1.0f};
};

} // namespace editor
