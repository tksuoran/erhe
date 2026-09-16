#include "erhe_physics/box3d/box3d_material_registry.hpp"

#include <cmath>

namespace erhe::physics {

Box3d_material_registry::Box3d_material_registry()
{
    m_snapshots.push_back(Physics_material_snapshot{}); // default_material_id
}

auto Box3d_material_registry::get() -> Box3d_material_registry&
{
    static Box3d_material_registry instance;
    return instance;
}

auto Box3d_material_registry::register_material(const std::shared_ptr<Physics_material>& material) -> uint64_t
{
    if (!material) {
        return default_material_id;
    }
    Physics_material_snapshot snapshot{};
    snapshot.static_friction     = material->get_static_friction();
    snapshot.dynamic_friction    = material->get_dynamic_friction();
    snapshot.restitution         = material->get_restitution();
    snapshot.friction_combine    = material->get_friction_combine();
    snapshot.restitution_combine = material->get_restitution_combine();

    const std::lock_guard<std::mutex> lock{m_mutex};
    m_snapshots.push_back(snapshot);
    return static_cast<uint64_t>(m_snapshots.size()); // ids are 1-based
}

auto Box3d_material_registry::find_snapshot(const uint64_t material_id) const -> const Physics_material_snapshot*
{
    if (material_id == no_material_id) {
        return nullptr;
    }
    const std::lock_guard<std::mutex> lock{m_mutex};
    const std::size_t index = static_cast<std::size_t>(material_id - 1);
    if (index >= m_snapshots.size()) {
        return nullptr;
    }
    return &m_snapshots[index];
}

namespace {

[[nodiscard]] auto mix_friction(
    const float    friction_a,
    const uint64_t material_id_a,
    const float    friction_b,
    const uint64_t material_id_b
) -> float
{
    const Box3d_material_registry&   registry   = Box3d_material_registry::get();
    const Physics_material_snapshot* snapshot_a = registry.find_snapshot(material_id_a);
    const Physics_material_snapshot* snapshot_b = registry.find_snapshot(material_id_b);
    if ((snapshot_a == nullptr) || (snapshot_b == nullptr)) {
        // At least one shape was not created by erhe, so there is no combine mode
        // to honor; use Box3D's own rule (b3DefaultFrictionCallback).
        return std::sqrt(friction_a * friction_b);
    }
    // Box3D carries a single friction per surface, so erhe's dynamic friction
    // is the one that acts. static_friction is kept in the snapshot but unused.
    return combine_values(
        combine(snapshot_a->friction_combine, snapshot_b->friction_combine),
        snapshot_a->dynamic_friction,
        snapshot_b->dynamic_friction
    );
}

[[nodiscard]] auto mix_restitution(
    const float    restitution_a,
    const uint64_t material_id_a,
    const float    restitution_b,
    const uint64_t material_id_b
) -> float
{
    const Box3d_material_registry&   registry   = Box3d_material_registry::get();
    const Physics_material_snapshot* snapshot_a = registry.find_snapshot(material_id_a);
    const Physics_material_snapshot* snapshot_b = registry.find_snapshot(material_id_b);
    if ((snapshot_a == nullptr) || (snapshot_b == nullptr)) {
        return (restitution_a > restitution_b) ? restitution_a : restitution_b;
    }
    return combine_values(
        combine(snapshot_a->restitution_combine, snapshot_b->restitution_combine),
        snapshot_a->restitution,
        snapshot_b->restitution
    );
}

} // anonymous namespace

void Box3d_material_registry::install_callbacks(const b3WorldId world)
{
    b3World_SetFrictionCallback   (world, &mix_friction);
    b3World_SetRestitutionCallback(world, &mix_restitution);
}

} // namespace erhe::physics
