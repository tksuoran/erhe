#pragma once

#include "tools/tool.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/scene/transform.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::application
{
    class Line_renderer_set;
}

namespace editor
{

class Brush;
class Brushes;
class Brush_create_context;
class Editor_scenes;
class Materials_window;
class Mesh_memory;
class Operation_stack;
class Selection_tool;

class Create_uv_sphere
{
public:
    void render_preview(
        const Render_context&                 context,
        erhe::application::Line_renderer_set& line_renderer_set,
        const erhe::scene::Transform&         transform
    );

    [[nodiscard]] auto imgui(const Brush_create_context& context) -> std::shared_ptr<Brush>;

private:
    bool  m_preview    {false};
    int   m_slice_count{8};
    int   m_stack_count{8};
    float m_radius     {1.0f};
};

class Create
    : public erhe::components::Component
    , public Tool
    , public erhe::application::Imgui_window
{
public:
    static constexpr int              c_priority {4};
    static constexpr std::string_view c_type_name{"Create"};
    static constexpr std::string_view c_title    {"Create"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Create ();
    ~Create() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto tool_priority() const -> int   override { return c_priority; }
    [[nodiscard]] auto description  () -> const char* override { return c_title.data(); }
    void tool_render            (const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

private:
    // Component dependencies
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<Brushes         > m_brushes;
    std::shared_ptr<Editor_scenes   > m_editor_scenes;
    std::shared_ptr<Materials_window> m_materials_window;
    std::shared_ptr<Mesh_memory     > m_mesh_memory;
    std::shared_ptr<Operation_stack > m_operation_stack;
    std::shared_ptr<Selection_tool  > m_selection_tool;

    Create_uv_sphere m_create_uv_sphere;
};

} // namespace editor
