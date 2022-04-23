#pragma once

#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/components/components.hpp"

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
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_name {"Layers_window"};
    static constexpr std::string_view c_title{"Layers"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Layers_window ();
    ~Layers_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;

private:
    // Component dependencies
    std::shared_ptr<Scene_root>     m_scene_root;
    std::shared_ptr<Selection_tool> m_selection_tool;
    std::shared_ptr<Icon_set>       m_icon_set;

    std::shared_ptr<erhe::scene::Node> m_node_clicked;
};

} // namespace editor
