#pragma once

#include "windows/imgui_window.hpp"

#include <memory>

namespace erhe::scene
{
    class Node;
}

namespace editor
{

class Scene_root;

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

    std::shared_ptr<Scene_root> m_scene_root;
};

} // namespace editor
