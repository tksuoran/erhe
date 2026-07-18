#include "operations/material_change_operation.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "assets/asset_manager.hpp"
#include "scene/scene_root.hpp"

#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"

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
    m_usership.set_user_label("undo stack: material change");
}

Material_change_operation::~Material_change_operation() noexcept = default;

void Material_change_operation::execute(App_context& context)
{
    if ((m_usership.get_state() == Asset_resolve_state::unresolved) && (context.asset_manager != nullptr)) {
        m_usership.adopt(*context.asset_manager, m_material);
    }
    // TODO Lock the item
    m_material->data = m_after;
    // R5.8: the edit dirties the material's defining container (undo is an
    // edit too - the file no longer matches the live state either way).
    if (context.asset_manager != nullptr) {
        context.asset_manager->mark_item_dirty(*m_material);
    }
}

void Material_change_operation::undo(App_context& context)
{
    // TODO Lock the item
    m_material->data = m_before;
    if (context.asset_manager != nullptr) {
        context.asset_manager->mark_item_dirty(*m_material);
    }
}

}
