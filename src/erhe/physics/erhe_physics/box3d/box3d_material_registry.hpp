#pragma once

#include "erhe_physics/physics_material.hpp"

#include <box3d/box3d.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

namespace erhe::physics {

// Immutable copy of a Physics_material's contact values, matching the Jolt
// backend's per-body snapshot. The defaults are the material defaults, so a
// body without a material reads the same values.
class Physics_material_snapshot
{
public:
    float        static_friction     {c_default_friction};
    float        dynamic_friction    {c_default_friction};
    float        restitution         {c_default_restitution};
    Combine_mode friction_combine    {Combine_mode::e_average};
    Combine_mode restitution_combine {Combine_mode::e_average};
};

// erhe's Combine_mode is a property of the contact PAIR, not of either
// material: minimum and multiply give results no single per-shape scalar can
// encode. So friction and restitution have to be resolved at contact time,
// through Box3D's world-level mixing callbacks.
//
// Those callbacks intentionally take no context pointer (box3d/types.h: "this
// is called from a worker thread"), so the lookup table has to be reachable
// without one -- hence a process-global registry keyed by
// b3SurfaceMaterial::userMaterialId.
//
// Snapshots are immutable once registered and never removed, so a worker thread
// can read one while the main thread registers another. set_physics_material()
// allocates a new id rather than mutating an existing snapshot, which matches
// the Jolt backend and the threading contract in irigid_body.hpp.
class Box3d_material_registry
{
public:
    // userMaterialId 0 means "no erhe material" (a shape erhe did not create):
    // the callbacks then fall back to Box3D's own mixing rules.
    static constexpr uint64_t no_material_id = 0;

    // The snapshot of the material defaults, registered at construction. A
    // body without a material behaves like one with the default material
    // (Physics_material), so it is stamped with this id.
    static constexpr uint64_t default_material_id = 1;

    Box3d_material_registry();

    [[nodiscard]] static auto get() -> Box3d_material_registry&;

    // Returns the userMaterialId to stamp onto a shape's b3SurfaceMaterial;
    // default_material_id for no material.
    [[nodiscard]] auto register_material(const std::shared_ptr<Physics_material>& material) -> uint64_t;

    // Returns nullptr for no_material_id or an id this registry never issued.
    [[nodiscard]] auto find_snapshot(uint64_t material_id) const -> const Physics_material_snapshot*;

    // Installs the mixing callbacks on a world. Safe to call once per world.
    static void install_callbacks(b3WorldId world);

private:
    // A deque keeps element addresses stable as it grows, so a snapshot pointer
    // handed to a worker thread stays valid.
    mutable std::mutex                    m_mutex;
    std::deque<Physics_material_snapshot> m_snapshots;
};

} // namespace erhe::physics
