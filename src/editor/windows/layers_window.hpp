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

class Layers_window
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name{"Node_tree"};

    Layers_window ();
    ~Layers_window() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui(Pointer_context& pointer_context) override;

private:
    auto get_icon(const erhe::scene::Light_type type) const -> const ImVec2;

    bool                               m_show_meshes{true};
    bool                               m_show_lights{true};
    std::shared_ptr<Scene_root>        m_scene_root;
    std::shared_ptr<Selection_tool>    m_selection_tool;
    std::shared_ptr<Icon_set>          m_icon_set;
    std::shared_ptr<erhe::scene::Node> m_node_clicked;
};

} // namespace editor
