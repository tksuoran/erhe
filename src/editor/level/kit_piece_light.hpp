#pragma once

#include "level/kit_light.hpp"

#include "erhe_property/dependency_property.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace erhe        { class Hierarchy; }
namespace erhe::scene { class Light; class Xformable; using Node = Xformable; }

namespace editor {

class App_context;
class Kit_parameter_values;
class Operation;
class Scene_root;

// The kit record of a piece's light node (D3.15): the light frame it sits on, as an attached value group of the light
// node, qualified Kit_light.frame; saved.
class Kit_light
{
public:
    Kit_light() = delete;

    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;

    // The key property: the node is a kit light while it names a frame.
    static const erhe::property::Property<std::string> frame_property;

    [[nodiscard]] static auto all_properties() -> const std::vector<const erhe::property::Dependency_property*>&;
};

[[nodiscard]] auto read_kit_light_frame(const erhe::scene::Node& node) -> std::optional<std::string>;

// The piece's children that carry Kit_light, in child order (cleared, then filled).
void collect_piece_lights(const erhe::scene::Node& piece, std::vector<std::shared_ptr<erhe::scene::Light>>& out_lights);

// A new light node of the frame, not in a scene: the frame's name, record and local transform, the starting values.
[[nodiscard]] auto make_piece_light(Scene_root& scene_root, const Kit_light_frame& frame) -> std::shared_ptr<erhe::scene::Light>;

// A new light node per frame of the values under a piece that is not in a scene yet (a placement before its insert).
void add_piece_lights(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& piece, const Kit_parameter_values& values);

// Makes a piece's light nodes those of the values' frames (D3.15): a matched node moves to its frame keeping its light
// properties, a frame without a node gets a new one, a node recording a frame the values do not state is removed and a
// further node recording a taken frame (a user's copy) keeps living as a plain light, its Kit_light record cleared.
// Owned by Level_kits: its scratch keeps capacity, so a layout preview step of lit pieces on their frames allocates
// nothing, and a piece of a shape without light frames and without children costs no more than two checks.
class Kit_piece_light_sync
{
public:
    // Appends the operations, for the undo entry of the regenerating operation.
    void append(
        App_context&                              context,
        Scene_root&                               scene_root,
        const std::shared_ptr<erhe::scene::Node>& piece,
        const Kit_parameter_values&               values,
        std::vector<std::shared_ptr<Operation>>&  out_operations
    );

    // Applies the same at once, without operations (a scene load is not an edit); the caller holds the scene's
    // item_host_mutex. Returns the count of moved, added, removed and released nodes.
    auto apply_now(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& piece, const Kit_parameter_values& values) -> std::size_t;

    // A live parameter drag (D3.6, U43): the first step remembers the piece's kit light nodes' local transforms; each step
    // moves the nodes matched to the values' frames onto them without an operation (nodes of frames that appear or go are
    // made or removed by the release's append()). Restore gives the remembered transforms back (a cancel, and before the
    // release's operation, which then records the move from them). The caller holds the scene's item_host_mutex.
    void preview(const std::shared_ptr<erhe::scene::Node>& piece, const Kit_parameter_values& values);
    void restore_preview();

private:
    // False when there is nothing to match (no frames and no kit light children).
    [[nodiscard]] auto make_plan(const erhe::scene::Node& piece, const Kit_parameter_values& values) -> bool;
    void release();

    std::vector<Kit_light_frame>                     m_frames;
    std::vector<std::shared_ptr<erhe::scene::Light>> m_lights;
    std::vector<std::string>                         m_light_frames;
    Kit_light_match                                  m_match;
    std::vector<std::shared_ptr<erhe::Hierarchy>>    m_subtree;

    class Remembered
    {
    public:
        std::shared_ptr<erhe::scene::Light> light;
        erhe::scene::Trs_transform          parent_from_node;
    };
    std::vector<Remembered> m_preview;
    bool                    m_previewing{false};
};

// Every child of the node is a kit light (a piece of a kit layout may have these children and still match its element).
[[nodiscard]] auto has_only_kit_light_children(const erhe::scene::Node& node) -> bool;
// The same with surface attached pieces allowed too (S5.2: a layout's kept element keeps them); and attached pieces alone.
[[nodiscard]] auto has_only_kit_light_and_attached_children(const erhe::scene::Node& node) -> bool;
[[nodiscard]] auto has_only_attached_children(const erhe::scene::Node& node) -> bool;

} // namespace editor
