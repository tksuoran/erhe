#pragma once

#include "level/kit_layout.hpp"
#include "level/kit_layout_values.hpp"
#include "level/kit_shape.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace erhe::scene {
    class Mesh;
    class Xformable;
    using Node = Xformable;
}

namespace editor {

class App_context;
class Scene_root;

// The shape the Level Kit window places, offered as a new layout item; shape null when none is chosen.
class Kit_catalog_item
{
public:
    const Kit_shape_definition*    shape {nullptr};
    const Kit_parameter_values*    values{nullptr};
    std::span<const std::string>   slot_materials;
};

// One item of the selected layout node as its rows show it.
class Kit_layout_item_rows
{
public:
    Kit_parameter_values     values;
    std::vector<std::string> materials; // by slot, the slot's default filled in
    std::string              problem;   // why the item does not read
    std::string              message;   // validate() of the last edit
};

// The Level Kit window's Layout section (doc/editor/level_tools.md "Level Kit window", D4.9): Make Layout for a
// selected piece, the value rows of a selected kit layout node. Every edit commits one Level_kits call at the edit.
class Kit_layout_editor
{
public:
    explicit Kit_layout_editor(App_context& context);

    // The node's values are read again (queued) at the next imgui(); on selection and Kit_layout value messages.
    void mark_dirty();

    // The Make Layout pattern takes the selected piece's extent again; on mesh geometry messages.
    void mark_geometry_changed();

    void imgui(
        const std::shared_ptr<Scene_root>&        scene_root,
        const std::shared_ptr<erhe::scene::Node>& layout,
        const std::shared_ptr<erhe::scene::Mesh>& piece,
        const Kit_catalog_item&                   catalog_item
    );

private:
    void make_imgui        (const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Mesh>& piece);
    void layout_imgui      (const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Node>& layout, const std::shared_ptr<erhe::scene::Mesh>& piece, const Kit_catalog_item& catalog_item);
    void items_imgui       (const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Node>& layout, const std::shared_ptr<erhe::scene::Mesh>& piece, const Kit_catalog_item& catalog_item);
    void message_imgui     () const;
    // In the queued call: the node's values, its items' rows and the run they make.
    void read_layout       (Scene_root& scene_root, const erhe::scene::Node& layout);
    void queue_read        (const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Node>& layout);
    // D3.9: set_layout() with the values in the queued call; a refusal shows in the section.
    void queue_set         (const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Node>& layout, const Kit_layout_data& data);
    void queue_lock        (const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Node>& layout, Kit_layout_lock lock);
    void queue_refresh     (const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Node>& layout);
    void queue_make        (const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Mesh>& piece);

    App_context& m_context;

    // Make Layout: the pattern for the selected piece, its step and spacings taken from the piece's extent.
    Kit_layout_pattern               m_new_pattern;
    std::weak_ptr<erhe::scene::Mesh> m_new_piece;

    // The selected layout node's values as last read; the rows edit this copy and commit it.
    std::weak_ptr<erhe::scene::Node>  m_layout;
    bool                              m_dirty      {true};
    bool                              m_read_queued{false};
    bool                              m_ready      {false};
    std::string                       m_problem;   // why the values do not read
    Kit_layout_data                   m_data;
    std::vector<Kit_layout_item_rows> m_items;
    Kit_lengths                       m_lengths;
    Kit_layout_run                    m_run;
    std::string                       m_run_problem;
    float                             m_item_extent{0.0f};

    // The last press's or edit's result: a refusal in red, else a confirmation.
    std::string m_message;
    bool        m_failed{false};
};

} // namespace editor
