#pragma once

#include "app_message.hpp"
#include "level/kit_layout.hpp"
#include "level/kit_layout_outline.hpp"
#include "tools/point_set_tool.hpp"

#include "erhe_message_bus/message_bus.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace erhe::commands { class Commands; }
namespace erhe::scene    { class Xformable; using Node = Xformable; }

namespace editor {

class App_message_bus;
class Scene_root;
class Tools;

// Viewport handles on a selected scatter kit layout node's region outline (doc/editor/level_tools.md "Scatter region
// handles"): a point set tool whose target is the kit layout node while it is the only selected item. The points are
// the outline's Kit_region_handle list; one handle is selected at a time and the gizmo drag resizes the region.
class Kit_layout_region_tool : public Point_set_tool
{
public:
    Kit_layout_region_tool(
        erhe::commands::Commands& commands,
        App_context&              context,
        App_message_bus&          app_message_bus,
        Tools&                    tools
    );

    [[nodiscard]] auto get_target_layout() const -> std::shared_ptr<erhe::scene::Node>;
    [[nodiscard]] auto get_selected_handle() const -> std::optional<std::size_t>;
    // The target's handles in its own frame (empty when it draws no scatter region); clears and fills.
    void get_handles(std::vector<Kit_region_handle>& out_handles) const;
    // Makes layout the only selected item and the tool's target, then selects the handle (nullopt clears).
    auto select_layout_handle(const std::shared_ptr<erhe::scene::Node>& layout, std::optional<std::size_t> handle) -> bool;

    // Implements IPoint_set
    auto update_point_set() -> bool override;
    [[nodiscard]] auto has_point_set_target    () const -> bool override;
    [[nodiscard]] auto get_point_set_scene_root() const -> std::shared_ptr<Scene_root> override;
    void get_point_world_positions(std::vector<std::optional<glm::vec3>>& out_positions) const override;
    [[nodiscard]] auto is_point_selected       (std::size_t index) const -> bool override;
    [[nodiscard]] auto has_point_selection     () const -> bool override;
    void select_point                          (std::size_t index, Point_select select) override;
    auto clear_point_selection                 () -> bool override;
    [[nodiscard]] auto get_point_frame_rotation() const -> glm::quat override;
    [[nodiscard]] auto get_point_drawing       () const -> Point_drawing override;
    auto begin_point_edit () -> bool override;
    void apply_point_edit (const std::optional<glm::mat4>& world_delta) override;
    void commit_point_edit() override;
    void cancel_point_edit() override;

protected:
    [[nodiscard]] auto try_ready_miss(Viewport_scene_view& viewport_scene_view, const glm::vec2& pointer) -> bool override;
    auto on_select_miss() -> bool override;

private:
    void set_target(const std::shared_ptr<erhe::scene::Node>& layout);
    // The target's cached outline, null when it is not drawn (values refused, scale not 1).
    [[nodiscard]] auto get_target_outline() const -> const Kit_layout_outline*;
    void on_layout_changed(uint64_t node_id);
    // Gives the node its values from before the gesture back: the preview's restore or the pending outline's end.
    void restore_start();
    // The node left the selection or its scene during the gesture.
    [[nodiscard]] auto is_edit_lost() const -> bool;

    erhe::message_bus::Subscription<Selection_message>          m_selection_subscription;
    erhe::message_bus::Subscription<Kit_layout_changed_message> m_kit_layout_changed_subscription;
    erhe::message_bus::Subscription<Close_scene_message>        m_close_scene_subscription;
    erhe::message_bus::Subscription<Items_removed_message>      m_items_removed_subscription;

    std::weak_ptr<erhe::scene::Node>     m_target;
    bool                                 m_target_stale{true};
    std::optional<std::size_t>           m_selected;  // one handle at a time
    std::optional<Repeat_scatter_region> m_region;    // the target's while it is a scatter: what the indices name
    bool                                 m_miss_clear{false};

    class Edit
    {
    public:
        std::shared_ptr<erhe::scene::Node> node;
        std::weak_ptr<Scene_root>          scene_root;
        Kit_region_handle                  handle;
        Kit_layout_data                    before;
        Kit_layout_data                    tried;          // the values of the last step
        Kit_layout_pattern                 shown_pattern;  // the last pattern the preview or the pending outline took
        glm::mat4                          world_from_node{1.0f};
        glm::mat4                          node_from_world{1.0f};
        bool                               active {false};
        bool                               changed{false}; // a step showed values other than before
        bool                               lost   {false};
    };
    Edit m_edit;
};

}
