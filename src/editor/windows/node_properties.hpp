#pragma once

#include "windows/imgui_window.hpp"

#include "erhe/components/components.hpp"
#include "erhe/scene/transform.hpp"

#include <vector>
#include <memory>

namespace erhe::scene
{
    class Camera;
    class ICamera;
    class Light;
    class Mesh;
    class Node;
}

namespace editor
{

class Operation_stack;
class Scene_root;
class Selection_tool;

class Node_properties
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name {"Node_properties"};
    static constexpr std::string_view c_title{"Node Properties"};
    static constexpr uint32_t         hash   {compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {})};

    Node_properties ();
    ~Node_properties() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui   () override;
    void on_begin() override;
    void on_end  () override;

private:
    void icamera_properties  (erhe::scene::ICamera& camera) const;
    void light_properties    (erhe::scene::Light& light) const;
    void mesh_properties     (erhe::scene::Mesh& mesh) const;
    void transform_properties(erhe::scene::Node& node);

    class Value_edit_state
    {
    public:
        void combine(Value_edit_state other)
        {
            value_changed = value_changed || other.value_changed;
            edit_ended    = edit_ended || other.edit_ended;
        }

        bool value_changed{false};
        bool edit_ended   {false};
    };

    auto make_scalar_button(
        float* const      value,
        const uint32_t    text_color,
        const uint32_t    background_color,
        const char* const label,
        const char* const imgui_label
    ) const -> Value_edit_state;

    auto make_angle_button(
        float&            radians_value,
        const uint32_t    text_color,
        const uint32_t    background_color,
        const char* const label,
        const char* const imgui_label
    ) const -> Value_edit_state;

    struct Node_state
    {
        explicit Node_state(erhe::scene::Node& node);

        erhe::scene::Node*     node   {nullptr};
        bool                   touched{false};
        erhe::scene::Transform initial_parent_from_node_transform;
        erhe::scene::Transform initial_world_from_node_transform;
    };

    [[nodiscard]] auto get_node_state(erhe::scene::Node& node) -> Node_state&;
    auto drop_node_state(erhe::scene::Node& node);

    // Component dependencies
    std::shared_ptr<Operation_stack> m_operation_stack;
    std::shared_ptr<Scene_root>      m_scene_root;
    std::shared_ptr<Selection_tool>  m_selection_tool;

    std::vector<Node_state> m_node_states;
};

} // namespace editor
