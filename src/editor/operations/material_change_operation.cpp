#include "operations/material_change_operation.hpp"

#include "app_context.hpp"

#include "erhe_scene_renderer/standard_shader_variants.hpp"

#include <fmt/format.h>

namespace editor {

Material_change_operation::Material_change_operation(
    const std::shared_ptr<erhe::primitive::Material>& material,
    const erhe::primitive::Material_data&             before,
    const erhe::primitive::Material_data&             after
)
    : m_material{material}
    , m_before{before}
    , m_after{after}
{
    set_description(fmt::format("Material change {}", m_material->get_name()));
}

Material_change_operation::~Material_change_operation() noexcept = default;

namespace {

// A material edit can flip which texture samplers are bound, which
// changes the per-material bits of Standard_variant_key. Cache entries
// keyed on the old caps are still valid for materials that didn't
// change, but the cheap path is invalidate-all -- the next render
// recompiles the small set of (material caps, format, light counts)
// tuples that the scene actually uses. Per-key dependency tracking is
// out of scope per doc/shader_variants.md.
void invalidate_standard_variants(App_context& context)
{
    if (context.standard_shader_variants != nullptr) {
        context.standard_shader_variants->clear();
    }
}

} // anonymous namespace

void Material_change_operation::execute(App_context& context)
{
    // TODO Lock the item
    m_material->data = m_after;
    invalidate_standard_variants(context);
}

void Material_change_operation::undo(App_context& context)
{
    // TODO Lock the item
    m_material->data = m_before;
    invalidate_standard_variants(context);
}

}
