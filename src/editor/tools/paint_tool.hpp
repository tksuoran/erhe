#pragma once

#include "tools/tool.hpp"
#include "scene/scene_view.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/geometry/types.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>

namespace erhe::scene
{
    class Mesh;
}

namespace editor
{

class Paint_vertex_command
    : public erhe::application::Command
{
public:
    Paint_vertex_command();
    void try_ready() override;
    auto try_call () -> bool override;
};

enum class Paint_mode
{
    Point = 0,
    Corner,
    Polygon
};

static constexpr const char* c_paint_mode_strings[] =
{
    "Point",
    "Corner",
    "Polygon"
};

class Paint_tool
    : public erhe::application::Imgui_window
    , public erhe::components::Component
    , public Tool
{
public:
    static constexpr int              c_priority {4};
    static constexpr std::string_view c_type_name{"Paint_tool"};
    static constexpr std::string_view c_title{"Paint Tool"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Paint_tool ();
    ~Paint_tool() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Implements Imgui_window
    void imgui() override;

    // Implements Tool
    void handle_priority_update(int old_priority, int new_priority) override;

    auto try_ready() -> bool;
    void paint();

private:
    void paint_corner(
        const std::shared_ptr<erhe::scene::Mesh>& mesh,
        const erhe::geometry::Geometry&           geometry,
        erhe::geometry::Corner_id                 corner_id,
        const glm::vec4                           color
    );
    void paint_vertex(
        const std::shared_ptr<erhe::scene::Mesh>& mesh,
        const erhe::geometry::Geometry&           geometry,
        uint32_t                                  vertex_id,
        const glm::vec4                           color
    );

    Paint_vertex_command                   m_paint_vertex_command;
    erhe::application::Redirect_command    m_drag_redirect_update_command;
    erhe::application::Drag_enable_command m_drag_enable_command;

    Paint_mode              m_paint_mode{Paint_mode::Point};
    glm::vec4               m_color     {1.0f, 1.0f, 1.0f, 1.0f};

    std::optional<uint32_t> m_point_id;
    std::optional<uint32_t> m_corner_id;
    std::vector<glm::vec4>  m_ngon_colors;
};

extern Paint_tool* g_paint_tool;

} // namespace editor
