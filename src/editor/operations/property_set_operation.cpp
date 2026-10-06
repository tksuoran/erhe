#include "operations/property_set_operation.hpp"
#include "app_context.hpp"
#include "assets/asset_key.hpp"
#include "assets/asset_manager.hpp"
#include "editor_log.hpp"
#include "rig/bone_connect.hpp"
#include "scene/item_lookup.hpp"
#include "scene/scene_root.hpp"

#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_string.hpp"

#include <fmt/format.h>

namespace editor {

// D28 host check: same scene, or a manager-owned asset (material, brush,
// animation) the asset manager accepts across scenes; a scene-hosted item
// (a texture, a graph texture, a node) never crosses scenes. An item whose
// scene cannot be determined (previews, unhosted test items) passes.
auto is_item_reference_allowed(App_context& context, const erhe::Item_base& target, const erhe::Item_base& referenced) -> bool
{
    Scene_root* const target_scene     = find_scene_root_for_item(context, target);
    Scene_root* const referenced_scene = find_scene_root_for_item(context, referenced);
    log_operations->trace(
        "reference check: '{}' scene '{}', '{}' scene '{}'",
        target.get_name(), (target_scene != nullptr) ? target_scene->get_name() : "<none>",
        referenced.get_name(), (referenced_scene != nullptr) ? referenced_scene->get_name() : "<none>"
    );
    if ((target_scene == nullptr) || (referenced_scene == nullptr) || (target_scene == referenced_scene)) {
        return true;
    }
    if (
        (context.asset_manager != nullptr) &&
        is_manager_owned_asset_type(asset_type_from_item(referenced)) &&
        context.asset_manager->is_cross_scene_referenceable(referenced)
    ) {
        return true;
    }
    log_operations->warn(
        "property write refused: '{}' ({}) of scene '{}' cannot reference '{}' ({}) of scene '{}' (not a manager-owned asset that is cross-scene referenceable)",
        target.get_name(), target.get_type_name(), target_scene->get_name(),
        referenced.get_name(), referenced.get_type_name(), referenced_scene->get_name()
    );
    return false;
}

void adopt_reference_usership(App_context& context, std::vector<Asset_reference>& userships, const std::shared_ptr<erhe::Item_base>& item)
{
    if (!item || (context.asset_manager == nullptr) || (asset_type_from_item(*item) == Asset_type::none)) {
        return;
    }
    for (const Asset_reference& usership : userships) {
        if (usership.get().get() == item.get()) {
            return;
        }
    }
    Asset_reference& usership = userships.emplace_back();
    usership.set_user_label("undo stack: property set");
    usership.adopt(*context.asset_manager, item);
}

auto to_local_state(const std::optional<erhe::property::Property_value>& value) -> std::optional<erhe::property::Local_state>
{
    if (!value.has_value()) {
        return std::nullopt;
    }
    return erhe::property::Local_state{value.value()};
}

auto describe_local_state(const erhe::property::Dependency_property& property, const std::optional<erhe::property::Local_state>& state) -> std::string
{
    if (!state.has_value()) {
        return "<default>";
    }
    if (const erhe::property::Expression_text* text = std::get_if<erhe::property::Expression_text>(&state.value()); text != nullptr) {
        return "expression '" + text->text + "'";
    }
    return erhe::property::to_string(property, std::get<erhe::property::Property_value>(state.value()));
}

Property_set_operation::Property_set_operation(
    const std::shared_ptr<erhe::Item_base>&      item,
    const erhe::property::Dependency_property&   property,
    std::optional<erhe::property::Local_state>   before,
    std::optional<erhe::property::Local_state>   after
)
    : Property_set_operation{item, std::nullopt, property, std::move(before), std::move(after)}
{
}

Property_set_operation::Property_set_operation(
    const std::shared_ptr<erhe::Item_base>&      item,
    std::optional<std::size_t>                   sub_object,
    const erhe::property::Dependency_property&   property,
    std::optional<erhe::property::Local_state>   before,
    std::optional<erhe::property::Local_state>   after
)
    : m_item      {item}
    , m_sub_object{sub_object}
    , m_property  {property}
    , m_before    {std::move(before)}
    , m_after     {std::move(after)}
{
    set_description(
        fmt::format(
            "Set {} '{}'{} {} = {}",
            item->get_type_name(),
            item->get_name(),
            sub_object.has_value() ? fmt::format(" [{}]", item->get_property_sub_object_label(sub_object.value())) : std::string{},
            erhe::property::Property_registry::get().qualified_name(*item, property), // an attached or secondary property by its qualified name (D3, D30)
            describe_local_state(property, m_after)
        )
    );
}

Property_set_operation::Property_set_operation(
    const std::shared_ptr<erhe::Item_base>&       item,
    const erhe::property::Dependency_property&    property,
    std::optional<erhe::property::Property_value> before,
    std::optional<erhe::property::Property_value> after
)
    : Property_set_operation{item, property, to_local_state(before), to_local_state(after)}
{
}

Property_set_operation::~Property_set_operation() noexcept = default;

void Property_set_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());
    adopt_userships(context);
    const bool applied = apply(context, m_after);
    if (!m_follow_ups_recorded) {
        if (!applied) {
            return;
        }
        m_follow_ups_recorded = true;
        if (m_item && !m_sub_object.has_value()) {
            append_bone_connect_follow_ups(*m_item, m_property, m_follow_ups);
        }
    }
    for (const std::shared_ptr<Operation>& follow_up : m_follow_ups) {
        follow_up->execute(context);
    }
}

void Property_set_operation::adopt_userships(App_context& context)
{
    if (m_userships_adopted || (context.asset_manager == nullptr)) {
        return;
    }
    m_userships_adopted = true;
    adopt_reference_usership(context, m_userships, get_referenced_item(m_before));
    adopt_reference_usership(context, m_userships, get_referenced_item(m_after));
}

void Property_set_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());
    for (auto i = m_follow_ups.rbegin(), end = m_follow_ups.rend(); i != end; ++i) {
        (*i)->undo(context);
    }
    apply(context, m_before);
}

auto Property_set_operation::apply(App_context& context, const std::optional<erhe::property::Local_state>& state) -> bool
{
    if (!m_item) {
        return false;
    }
    return apply_item_property(context, *m_item, m_sub_object, m_property, state);
}

void Property_set_operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    if (m_item) {
        out_items.insert(m_item.get());
    }
    for (const std::shared_ptr<Operation>& follow_up : m_follow_ups) {
        follow_up->collect_item_references(out_items);
    }
    for (const std::optional<erhe::property::Local_state>* state : {&m_before, &m_after}) {
        if (const std::shared_ptr<erhe::Item_base> referenced = get_referenced_item(*state); referenced) {
            out_items.insert(referenced.get());
        }
    }
}

}
