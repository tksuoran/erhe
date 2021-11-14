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
    void imgui_tree_node(erhe::scene::Node* node);

    std::shared_ptr<Scene_root>        m_scene_root;
    std::shared_ptr<Selection_tool>    m_selection_tool;
    std::shared_ptr<Icon_set>          m_icon_set;
    std::shared_ptr<erhe::scene::Node> m_node_clicked;
};

} // namespace editor
