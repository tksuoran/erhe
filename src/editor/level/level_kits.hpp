#pragma once

#include "level/kit_piece_operation.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_library.hpp"
#include "level/level_queries.hpp"

#include "app_message.hpp"

#include "erhe_message_bus/message_bus.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe           { class Hierarchy; class Item_host; }
namespace erhe::primitive { class Material; }
namespace erhe::scene    { class Mesh; class Xformable; using Node = Xformable; }

namespace editor {

class App_context;
class App_message_bus;
class Brush;
class Scene_root;

enum class Kit_snap : unsigned int {
    none = 0,
    module // positions round to multiples of the module (R5.2)
};

// One piece to place (D3.2); positions and rotations are in the scene root's space.
class Kit_placement
{
public:
    const Kit_shape_definition*                shape{nullptr};
    std::optional<Kit_parameter_values>        values;       // none: the defaults at the scene's lengths
    std::shared_ptr<erhe::Hierarchy>           parent;       // none: the scene root
    std::optional<std::size_t>                 parent_index; // an earlier placement of the same call
    glm::vec3                                  position{0.0f};
    glm::quat                                  rotation{1.0f, 0.0f, 0.0f, 0.0f};
    std::string                                name;         // empty: the shape's display name
    std::shared_ptr<erhe::primitive::Material> material;     // none: the palette material of the shape's slot
    Surface_options                            surface;      // applied after the module snap (D7.3)
};

// Whether the Brush tool's kit values take the next seed after each placed piece (D4.5).
enum class Kit_seed_per_piece : unsigned int {
    same = 0,
    next
};

// The placement settings of the Level Kit window and the Brush tool's kit mode (D4.4).
class Kit_placement_settings
{
public:
    Kit_snap           snap{Kit_snap::none};
    Surface_options    surface;
    float              rotation_step_degrees{90.0f};
    Kit_seed_per_piece seed_per_piece{Kit_seed_per_piece::same};
};

// Where a piece placed at a point rests: the transform the kit mode preview shows and a click places.
class Kit_placement_pose
{
public:
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool      surface_found{false};
};

class Kit_piece_data;

// Whether a piece's mesh and collision shape are its kit brush's (D3.5, R8.3, R8.4).
class Kit_piece_check
{
public:
    bool        follows_kit{false};
    std::string problem;    // why it does not follow; empty when it does
};

class Kit_refresh_report
{
public:
    std::size_t piece_count  {0};
    std::size_t follow_count {0};
    std::size_t problem_count{0};
};

enum class Kit_snap_apply : unsigned int {
    move = 0,
    query_only
};

class Kit_snap_result
{
public:
    std::shared_ptr<erhe::scene::Mesh> mesh;
    Surface_placement                  placement;
};

class Kit_placed_piece
{
public:
    std::shared_ptr<erhe::scene::Node>         node;
    std::shared_ptr<Brush>                     brush;
    std::shared_ptr<erhe::primitive::Material> material;
    Kit_parameter_values                       values;
    glm::vec3                                  position{0.0f};
    glm::quat                                  rotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool                                       surface_found{false}; // false also when no surface option was set
};

class Kit_place_result
{
public:
    std::vector<Kit_placed_piece> pieces;
    std::string                   error;       // empty on success; with an error nothing is placed
    std::size_t                   error_index{0};
};

class Kit_set_parameters_result
{
public:
    std::shared_ptr<Brush> brush; // the brush of the new values
    std::string            error; // empty on success; with an error nothing changed
};

// The level kit editor part (D2.2): the shape library, the kit brush cache and piece placement.
class Level_kits
{
public:
    Level_kits(App_context& context, App_message_bus& app_message_bus);
    ~Level_kits() noexcept;
    Level_kits(const Level_kits&) = delete;
    auto operator=(const Level_kits&) -> Level_kits& = delete;

    [[nodiscard]] auto get_library        () const -> const Kit_shape_library&;
    [[nodiscard]] auto get_lengths        (const Scene_root& scene_root) const -> Kit_lengths;
    [[nodiscard]] auto make_default_values(const Scene_root& scene_root, const Kit_shape_definition& shape) const -> Kit_parameter_values;

    // Kit_placement_settings of the editor settings level_kit and surface_snap.
    [[nodiscard]] auto get_placement_settings() const -> Kit_placement_settings;

    // The shared surface pair (Surface_snap_config, D4.6): every UI site reads and writes it here.
    [[nodiscard]] auto get_surface_settings() const -> Surface_options;
    void set_surface_snap  (Surface_snap snap);
    void set_surface_orient(Surface_orient orient);

    // D4.4: the origin at the point (module-rounded), upright, turned by yaw_steps rotation steps
    // about the up axis, then the surface options; no allocation, raytrace scene committed here.
    [[nodiscard]] auto make_placement_pose(
        Scene_root&                   scene_root,
        const erhe::math::Aabb&       local_bounds,
        const glm::vec3&              point,
        int                           yaw_steps,
        const Kit_placement_settings& settings
    ) const -> Kit_placement_pose;

    // A material of the palette color of the shape's slot, outside every content library.
    [[nodiscard]] auto get_preview_material(const Kit_shape_definition& shape) -> std::shared_ptr<erhe::primitive::Material>;

    // The cached brush of the values, made on first use; null when its geometry fails.
    [[nodiscard]] auto get_brush(const Kit_parameter_values& values) -> std::shared_ptr<Brush>;

    // The cached brushes still alive: held by a piece or by an operation of the undo history.
    [[nodiscard]] auto get_cached_brush_count() const -> std::size_t;

    // Validates every placement, then places all of them as one Compound_operation (one undo entry).
    [[nodiscard]] auto place(Scene_root& scene_root, std::span<const Kit_placement> placements, Kit_snap snap) -> Kit_place_result;

    // The brush of the values when the cache holds it alive; never makes one.
    [[nodiscard]] auto find_cached_brush(const Kit_parameter_values& values) const -> std::shared_ptr<Brush>;

    // Reads a piece record into values of its shape; returns the problem (unknown shape, unreadable or rejected values), empty when none.
    [[nodiscard]] auto read_piece_values(const Kit_piece_data& record, Kit_parameter_values& out_values) const -> std::string;

    [[nodiscard]] auto check_piece(const erhe::scene::Node& node) const -> Kit_piece_check;

    // D3.5: after a scene has loaded, gives each piece the mesh and collision shape of its kit brush.
    auto refresh_scene(Scene_root& scene_root) -> Kit_refresh_report;

    // D7.3 on placed meshes (excluded from each other's hits); with move, the meshes that found a
    // surface move as one undo entry. Returns the moved count.
    auto snap_to_surface(
        Scene_root&                                         scene_root,
        std::span<const std::shared_ptr<erhe::scene::Mesh>> meshes,
        const Surface_options&                              options,
        Kit_snap_apply                                      apply,
        std::vector<Kit_snap_result>&                       out_results
    ) -> std::size_t;

    // D3.6: shows the values' mesh on the piece without an operation; the first call of a drag
    // remembers the piece's state. Returns the refusal, empty when shown.
    [[nodiscard]] auto preview_parameters(const std::shared_ptr<erhe::scene::Node>& node, const Kit_parameter_values& values) -> std::string;

    // Gives the previewed piece its state from before the drag back; nothing when no preview is live.
    void cancel_parameter_preview();

    [[nodiscard]] auto is_previewing(const erhe::scene::Node& node) const -> bool;

    // D3.3: gives the piece the mesh and collision shape of the values as one undo entry; with a live
    // preview of the piece, from the state before the drag (no entry when the values end where they began).
    [[nodiscard]] auto set_parameters(const std::shared_ptr<erhe::scene::Node>& node, const Kit_parameter_values& values) -> Kit_set_parameters_result;

    [[nodiscard]] static auto get_mesh_key   (const Kit_parameter_values& values) -> std::string;
    [[nodiscard]] static auto snap_to_module (const glm::vec3& position, float module) -> glm::vec3;

private:
    [[nodiscard]] auto make_brush(const Kit_parameter_values& values) -> std::shared_ptr<Brush>;
    void on_selection    ();
    void on_items_removed(const Removed_items& removed);
    void on_close_scene  (const erhe::Item_host* closing_host);

    App_context&                                m_context;
    Kit_shape_library                           m_library;
    // Weak: pieces (Brush_placement.brush) and undo entries hold the brushes (doc/editor/level_tools.md).
    std::map<std::string, std::weak_ptr<Brush>> m_brushes;
    // By palette material name.
    std::map<std::string, std::shared_ptr<erhe::primitive::Material>> m_preview_materials;

    // The live parameter drag (D3.6): the piece, its state from before the drag and the brush shown.
    class Parameter_preview
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> mesh;
        Kit_piece_mesh_state               before;
        std::shared_ptr<Brush>             brush;
    };
    std::optional<Parameter_preview> m_parameter_preview;

    erhe::message_bus::Subscription<Selection_message>     m_selection_subscription;
    erhe::message_bus::Subscription<Items_removed_message> m_items_removed_subscription;
    erhe::message_bus::Subscription<Close_scene_message>   m_close_scene_subscription;
};

} // namespace editor
