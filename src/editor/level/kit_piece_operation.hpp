#pragma once

#include "level/kit_connector.hpp"
#include "level/kit_piece.hpp"
#include "operations/operation.hpp"

#include "erhe_physics/irigid_body.hpp"
#include "erhe_scene/mesh.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace erhe::physics { class ICollision_shape; }

namespace editor {

class Brush;
class Scene_root;

// Whether a piece is ground: a piece of a shape without a body is not (Editor_item_flags::pass_through).
enum class Kit_piece_ground : unsigned int {
    ground = 0,
    pass_through
};

// What a parameter change (D3.3) or a replace (D3.10) of a piece swaps.
class Kit_piece_mesh_state
{
public:
    std::string                                      shape;      // Kit_piece.shape
    std::vector<erhe::scene::Mesh_primitive>         primitives;
    std::shared_ptr<erhe::physics::ICollision_shape> collision_shape;
    std::shared_ptr<Brush>                           brush;      // Brush_placement.brush
    std::string                                      parameters; // Kit_piece.parameters
    erhe::physics::Motion_mode                       motion_mode{erhe::physics::Motion_mode::e_none}; // e_none: no body
    Kit_piece_ground                                 ground     {Kit_piece_ground::ground};
};

// The motion mode and ground flag of a piece's mesh as they are, for a Kit_piece_mesh_state.
[[nodiscard]] auto get_piece_motion_mode(const erhe::scene::Mesh& mesh) -> erhe::physics::Motion_mode;
[[nodiscard]] auto get_piece_ground (const erhe::scene::Mesh& mesh) -> Kit_piece_ground;

// Replaces a piece's shape id, mesh primitives, collision shape, body, pass_through flag, brush and parameter text;
// undo restores them.
class Kit_piece_parameters_operation : public Operation
{
public:
    Kit_piece_parameters_operation(
        const std::shared_ptr<erhe::scene::Mesh>& mesh,
        Kit_piece_mesh_state&&                    before,
        Kit_piece_mesh_state&&                    after
    );
    ~Kit_piece_parameters_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const override;

private:
    void apply(App_context& context, const Kit_piece_mesh_state& state);

    std::shared_ptr<erhe::scene::Mesh> m_mesh;
    Kit_piece_mesh_state               m_before;
    Kit_piece_mesh_state               m_after;
};

// C6.2: one call's overrides of the flow mask and Auto-connect settings.
class Kit_flow_overrides
{
public:
    std::optional<Kit_connector_types> flow_mask;
    std::optional<bool>                auto_connect;
};

// A connection an explicit join records, reported by its change's flow (C3.13).
class Kit_new_connection
{
public:
    std::shared_ptr<erhe::scene::Node> node;
    Kit_attachment                     attachment;
    std::shared_ptr<erhe::scene::Node> target;
};

// S5.2: moves an attached piece under another parent keeping its local transform, its place in the element's node space;
// the parent it leaves is read at the first execute.
class Kit_surface_rehome_operation : public Operation
{
public:
    Kit_surface_rehome_operation(const std::shared_ptr<erhe::scene::Node>& node, const std::shared_ptr<erhe::Hierarchy>& parent);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const override;

private:
    std::shared_ptr<erhe::scene::Node> m_node;
    std::shared_ptr<erhe::Hierarchy>   m_parent_after;
    std::shared_ptr<erhe::Hierarchy>   m_parent_before;
    std::size_t                        m_index_before{0};
    bool                               m_executed{false};
};

// A move of a node that ended surface snapped with these options (S2.1, S6.1): the change's follow operation attaches the
// node to the support it rests on, or detaches it where it rests on none.
class Kit_surface_snap
{
public:
    std::shared_ptr<erhe::scene::Node> node;
    Surface_options                    options;
};

// Step 41: appended last to a change's entry; its first execute asks Level_kits::build_attachment_follow_ups() for the
// followers' moves and the detaches and runs them; redo runs them again, undo undoes them in reverse.
class Kit_attachment_follow_operation : public Operation
{
public:
    Kit_attachment_follow_operation(
        std::vector<std::shared_ptr<erhe::scene::Node>>&& moved_nodes,
        std::vector<std::shared_ptr<erhe::scene::Node>>&& changed_pieces,
        std::optional<Kit_snap_pair>&&                    snap,
        std::vector<std::shared_ptr<erhe::scene::Node>>&& requesting,
        std::vector<Kit_new_connection>&&                 new_connections,
        const std::optional<Kit_flow_overrides>&          flow_overrides,
        const std::vector<std::shared_ptr<erhe::scene::Node>>& auto_connect_excluded,
        std::vector<Kit_surface_snap>&&                   surface_snaps
    );
    ~Kit_attachment_follow_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const override;

private:
    // The first execute's fits, flow, following and auto-connect, applied; returns a refusal, with what ran undone.
    [[nodiscard]] auto record(App_context& context, Scene_root& scene_root) -> std::string;

    std::vector<std::shared_ptr<erhe::scene::Node>> m_moved_nodes;
    std::vector<std::shared_ptr<erhe::scene::Node>> m_changed_pieces;
    std::optional<Kit_snap_pair>                    m_snap;
    std::vector<std::shared_ptr<erhe::scene::Node>> m_requesting; // the pieces of new connections, which take values (C3.4)
    std::vector<Kit_new_connection>                 m_new_connections; // the explicit joins' records, for the report
    std::optional<Kit_flow_overrides>               m_flow_overrides;  // C6.2: the overrides of the call that made the entry
    std::vector<std::shared_ptr<erhe::scene::Node>> m_auto_connect_excluded; // the attach-none join's piece, which takes no record
    std::vector<Kit_surface_snap>                   m_surface_snaps;         // the nodes the change snapped to the surface
    bool                                            m_recorded{false};
    std::vector<std::shared_ptr<Operation>>         m_operations;
};

} // namespace editor
