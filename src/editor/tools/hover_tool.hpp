#pragma once

#include "tools/tool.hpp"
#include "windows/imgui_window.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>

namespace erhe::scene {
    class Mesh;
}

namespace erhe::primitive {
    class Material;
}

namespace editor
{

class Scene_root;

class Hover_tool
    : public erhe::components::Component
    , public Tool
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name {"Hover_tool"};
    static constexpr std::string_view c_title{"Hover"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Hover_tool ();
    ~Hover_tool() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Tool
    auto update     (Pointer_context& pointer_context) -> bool override;
    void render     (const Render_context& render_context)     override;
    auto state      () const -> State                          override;
    auto description() -> const char*                          override;

    // Implements Imgui_window
    void imgui(Pointer_context& pointer_context) override;

private:
    void deselect();
    void select  (Pointer_context& pointer_context);

    Scene_root*                        m_scene_root{nullptr};

    std::shared_ptr<erhe::scene::Mesh> m_hover_mesh           {nullptr};
    size_t                             m_hover_primitive_index{0};
    std::optional<glm::vec3>           m_hover_position;
    std::optional<glm::vec3>           m_hover_position_world;
    std::optional<glm::vec3>           m_hover_normal;
    bool                               m_hover_content        {false};
    bool                               m_hover_tool           {false};

    glm::vec4                                  m_hover_primitive_emissive{0.00f, 0.00f, 0.00f, 0.0f};
    glm::vec4                                  m_hover_emissive          {0.05f, 0.05f, 0.10f, 0.0f};
    std::shared_ptr<erhe::primitive::Material> m_original_primitive_material;
    std::shared_ptr<erhe::primitive::Material> m_hover_material;
    size_t                                     m_hover_material_index{0};

    bool m_enable_color_highlight{false};
};

} // namespace editor
