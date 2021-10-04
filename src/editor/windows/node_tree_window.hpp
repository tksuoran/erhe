#pragma once

#include "windows/imgui_window.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::graphics
{
    class Texture;
}

namespace erhe::scene
{
    class Node;
    enum class Light_type : unsigned int;
}

namespace editor
{

class Icon_set;
class Scene_root;
class Selection_tool;

class Node_tree_window
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name{"Node_tree"};

    Node_tree_window ();
    ~Node_tree_window() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui(Pointer_context& pointer_context) override;

private:
    auto get_icon  (const erhe::scene::Light_type type) const -> const ImVec2;
    void node_imgui(const std::shared_ptr<erhe::scene::Node>& node);
    void icon      (ImVec2 uv0, glm::vec4 tint_color = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f});

    std::shared_ptr<Scene_root>     m_scene_root;
    std::shared_ptr<Selection_tool> m_selection_tool;
    std::shared_ptr<Icon_set>       m_icon_set;
};

} // namespace editor
