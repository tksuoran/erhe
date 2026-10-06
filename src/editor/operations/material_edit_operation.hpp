#pragma once

#include <memory>

namespace erhe::primitive {
    class Material;
    class Material_data;
    class Material_values;
}

namespace editor {

class Property_edit_operation;

// The undoable form of one material edit (MCP edit_material;
// doc/editor/operations.md "Property_edit_operation"): a
// Property_edit_operation whose edit function writes, on `material`, the
// Material_values fields that differ between `before_values` and
// `after_values` as local values, and the texture slot fields that differ
// between `before_data` and `after_data` - each field through its own slot
// property as a local value (a changed field is an explicit request, even
// when it equals the default); only a texture reference set to null clears
// the slot's local value. Fields that do not differ are not written, so an
// expression or an inherited value on them stays, and undo restores the
// exact prior local layer of every written property. Null when nothing
// differs.
[[nodiscard]] auto make_material_edit_operation(
    const std::shared_ptr<erhe::primitive::Material>& material,
    const erhe::primitive::Material_values&           before_values,
    const erhe::primitive::Material_values&           after_values,
    const erhe::primitive::Material_data&             before_data,
    const erhe::primitive::Material_data&             after_data
) -> std::shared_ptr<Property_edit_operation>;

}
