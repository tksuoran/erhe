#pragma once

#include "windows/imgui_window.hpp"

#include "erhe/components/component.hpp"

#include <memory>

namespace erhe::scene
{
    class Node;
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
    static constexpr std::string_view c_name {"Node_tree"};
    static constexpr std::string_view c_title{"Scene"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Node_tree_window ();
    ~Node_tree_window() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui   () override;
    void on_begin() override;
    void on_end  () override;

private:
    void imgui_tree_node(erhe::scene::Node* node);

    Scene_root*     m_scene_root    {nullptr};
    Selection_tool* m_selection_tool{nullptr};
    Icon_set*       m_icon_set      {nullptr};

    std::shared_ptr<erhe::scene::Node> m_node_clicked;
};

} // namespace editor
