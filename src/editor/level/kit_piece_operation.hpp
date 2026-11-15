#pragma once

#include "operations/operation.hpp"

#include "erhe_scene/mesh.hpp"

#include <memory>
#include <string>
#include <vector>

namespace erhe::physics { class ICollision_shape; }

namespace editor {

class Brush;

// What a parameter change (D3.3) or a replace (D3.10) of a piece swaps.
class Kit_piece_mesh_state
{
public:
    std::string                                      shape;      // Kit_piece.shape
    std::vector<erhe::scene::Mesh_primitive>         primitives;
    std::shared_ptr<erhe::physics::ICollision_shape> collision_shape;
    std::shared_ptr<Brush>                           brush;      // Brush_placement.brush
    std::string                                      parameters; // Kit_piece.parameters
};

// Replaces a piece's shape id, mesh primitives, collision shape, brush and parameter text; undo restores them.
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

} // namespace editor
