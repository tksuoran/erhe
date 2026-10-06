#pragma once

#include "assets/asset_reference.hpp"
#include "operations/operation.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace erhe::primitive { class Material; }
namespace erhe::scene     { class Mesh; }

namespace editor {

class App_context;

// Undoable assignment of a material to mesh primitives - the gesture behind
// the material paint tool, the viewport and item-tree drag-drop targets and
// the Properties material picker.
//
// Mesh::set_primitive_material stays the one writer (R4 of the draw list
// material set work): everything downstream - the Scene_host material hooks,
// the material set slot references, the draw list re-register - hangs off
// that call, so execute() and undo() differ only in which material they hand
// it. Nothing else has to be rebuilt here, unlike a material property edit
// (a Property_edit_operation of set_item_properties or the Properties
// window), which edits a material in place and can move a mesh between
// draw lists.
class Mesh_material_assign_operation : public Operation
{
public:
    class Entry
    {
    public:
        std::shared_ptr<erhe::scene::Mesh>         mesh           {};
        std::size_t                                primitive_index{0};
        std::shared_ptr<erhe::primitive::Material> before         {};
        std::shared_ptr<erhe::primitive::Material> after          {};
    };

    explicit Mesh_material_assign_operation(std::vector<Entry>&& entries);
    ~Mesh_material_assign_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    void adopt_userships(App_context& context);

    std::vector<Entry>           m_entries;
    // R5.4 (asset-manager plan resolution 7, mechanism a): while recorded,
    // this operation is a DECLARED user of every material it can assign -
    // the ones it replaces included, since undo puts them back. Adopted at
    // first execute (the constructor has no Asset_manager; m_entries carry
    // the shared_ptrs from construction on).
    std::vector<Asset_reference> m_userships;
    bool                         m_userships_adopted{false};
};

// Builds the operation for assigning `material` to one primitive of `mesh`.
// Returns null when the assignment would change nothing - the primitive
// already has that material - so a re-paint records no undo entry.
[[nodiscard]] auto make_mesh_material_assign_operation(
    const std::shared_ptr<erhe::scene::Mesh>&         mesh,
    std::size_t                                       primitive_index,
    const std::shared_ptr<erhe::primitive::Material>& material
) -> std::shared_ptr<Mesh_material_assign_operation>;

// Same, for every primitive of `mesh` - the item-tree drop gesture. The
// primitives that already have the material are left out of the operation.
[[nodiscard]] auto make_mesh_material_assign_operation_for_all_primitives(
    const std::shared_ptr<erhe::scene::Mesh>&         mesh,
    const std::shared_ptr<erhe::primitive::Material>& material
) -> std::shared_ptr<Mesh_material_assign_operation>;

// Queue the above onto the operation stack; no-ops when nothing changes.
void queue_mesh_material_assign(
    App_context&                                      context,
    const std::shared_ptr<erhe::scene::Mesh>&         mesh,
    std::size_t                                       primitive_index,
    const std::shared_ptr<erhe::primitive::Material>& material
);

void queue_mesh_material_assign_to_all_primitives(
    App_context&                                      context,
    const std::shared_ptr<erhe::scene::Mesh>&         mesh,
    const std::shared_ptr<erhe::primitive::Material>& material
);

}
