#pragma once

#include "operations/operation.hpp"

#include <memory>

namespace erhe::physics {
    class ICollision_shape;
}
namespace erhe::primitive {
    class Material;
}

namespace editor {

class Brush;
class Scene_builder;

// Tracks the side-resources eagerly created by Scene_builder::add_room
// (floor brush, collision shape, and floor material) so the undo /
// redo pair tears them down and re-installs them atomically with the
// Compound_operation that performs the scene-graph insert.
//
// Construction does NOT mutate the builder; the resources are
// registered in execute() (initial run + redo) and removed in undo().
// register_floor_resources is idempotent so a redo after a missed
// undo (or simply running the operation twice on the same resources)
// will not duplicate entries.
class Scene_builder_floor_resources_operation : public Operation
{
public:
    class Parameters
    {
    public:
        Scene_builder*                                          builder;
        std::shared_ptr<Brush>                                  brush;
        std::shared_ptr<erhe::physics::ICollision_shape>        collision_shape;
        std::shared_ptr<erhe::primitive::Material>              material;
    };

    explicit Scene_builder_floor_resources_operation(Parameters parameters);
    ~Scene_builder_floor_resources_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    Parameters m_parameters;
};

} // namespace editor
