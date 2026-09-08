#pragma once

#include "operations/operation.hpp"

#include <memory>
#include <string>

namespace editor {

class App_context;
class Scene_root;

// The selection half of a variant switch (doc/usd-compatibility-plan.md X4):
// the scene's variant table records which variant of the set is selected, and
// Scene_settings::variant_selections records the same so a save carries it.
// The material assignments are Mesh_material_assign_operations built beside
// this one; the compound they share is what one undo reverts.
class Variant_select_operation : public Operation
{
public:
    // Whether the scene carried a variant_selections entry for this set
    // before the switch: undoing back to `absent` removes the entry again,
    // so a scene that never switched saves none.
    enum class Entry_state : unsigned int {
        absent  = 0,
        present = 1
    };

    class Parameters
    {
    public:
        // Weak: an operation recorded for undo must not keep a closed scene
        // alive (AGENTS.md "Scene-hosted references in editor parts"). An
        // undo after the scene closed has nothing to put back.
        std::weak_ptr<Scene_root>   scene_root;
        std::string                 prim_path;
        std::string                 set_name;
        // The table's selection before the switch - what the file authored,
        // or what an earlier switch chose.
        std::string                 before_variant_name;
        std::string                 after_variant_name;
        Entry_state                 before_entry_state{Entry_state::absent};
    };

    explicit Variant_select_operation(Parameters&& parameters);
    ~Variant_select_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    void apply(const std::string& variant_name, Entry_state entry_state);

    Parameters m_parameters;
};

// The undoable compound one variant switch is: the selection entry above, one
// property write per opinion any variant of the set authors, one `active`
// write per prim any variant of the set adds - the chosen variant's prims
// active, every other variant's inactive - and one material assignment per
// binding of the chosen variant, so a single undo puts all of them back. A
// property the chosen variant does not author goes back to the
// set's base value - what the file authored outside the variant blocks - so
// switching never leaves the previous variant's opinion standing. Null when
// the scene has no such set or the set has no such variant. Selecting the
// variant a set is already on records the selection entry and changes
// nothing, which is how a scene pins the selection a file authored.
[[nodiscard]] auto make_select_variant_operation(
    const std::shared_ptr<Scene_root>& scene_root,
    const std::string&                 prim_path,
    const std::string&                 set_name,
    const std::string&                 variant_name
) -> std::shared_ptr<Operation>;

} // namespace editor
