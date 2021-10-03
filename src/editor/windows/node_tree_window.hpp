#pragma once

#include "windows/imgui_window.hpp"

#include <memory>

namespace erhe::graphics
{
    class Texture;
}

namespace erhe::scene
{
    class Node;
}

namespace editor
{

class Icon_set;
class Scene_root;
//class Textures;

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
    void node_imgui(const std::shared_ptr<erhe::scene::Node>& node);
    void icon(ImVec2 uv0);

    std::shared_ptr<Scene_root> m_scene_root;
    std::shared_ptr<Icon_set>   m_icon_set;

    //std::shared_ptr<Textures>   m_textures;
};

} // namespace editor
