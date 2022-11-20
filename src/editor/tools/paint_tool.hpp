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

namespace erhe::primitive
{
    class Material;
}

namespace editor
{

class Editor_scenes;
class Mesh_memory;
class Paint_tool;
class Scene_view;
class Viewport_window;
class Viewport_windows;

class Paint_vertex_command
    : public erhe::application::Command
{
public:
    explicit Paint_vertex_command(Paint_tool& paint_tool)
        : Command     {"Paint_tool.paint_vertex"}
        , m_paint_tool{paint_tool}
    {
    }

    auto try_call (erhe::application::Command_context& context) -> bool override;
    void try_ready(erhe::application::Command_context& context) override;

private:
    Paint_tool& m_paint_tool;
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
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override;
    void tool_hover (Scene_view* scene_view) override;

    // Implements Imgui_window
    void imgui() override;

    auto try_ready(Scene_view* scene_view) -> bool;
    void paint(Scene_view* scene_view);

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

    // Component dependencies
    std::shared_ptr<Viewport_windows> m_viewport_windows;
    std::shared_ptr<Mesh_memory     > m_mesh_memory;

    Paint_vertex_command m_paint_vertex_command;

    std::array<Hover_entry, Hover_entry::slot_count> m_hover_entries;

    Paint_mode m_paint_mode{Paint_mode::Point};
    glm::vec4  m_color     {1.0f, 1.0f, 1.0f, 1.0f};

    std::optional<uint32_t> m_point_id;
    std::optional<uint32_t> m_corner_id;
    std::vector<glm::vec4>  m_ngon_colors;
};

} // namespace editor
