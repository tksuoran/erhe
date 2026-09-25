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

namespace {

// The item an object value names, or null (not an object value, null
// reference, an expression, no local state).
auto referenced_item(const std::optional<erhe::property::Local_state>& state) -> std::shared_ptr<erhe::Item_base>
{
    if (!state.has_value()) {
        return {};
    }
    const erhe::property::Property_value* value = std::get_if<erhe::property::Property_value>(&state.value());
    if (value == nullptr) {
        return {};
    }
    return std::dynamic_pointer_cast<erhe::Item_base>(erhe::property::get_referenced_object(*value));
}

auto referenced_item(const std::optional<erhe::property::Property_value>& value) -> std::shared_ptr<erhe::Item_base>
{
    return referenced_item(to_local_state(value));
}

// D28 host check: same scene, or a manager-owned asset (material, brush,
// animation) the asset manager accepts across scenes; a scene-hosted item
// (a texture, a graph texture, a node) never crosses scenes. An item whose
// scene cannot be determined (previews, unhosted test items) passes.
auto is_reference_allowed(App_context& context, const erhe::Item_base& target, const erhe::Item_base& referenced) -> bool
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

// Asset-manager plan R5.4: an operation holding a managed asset declares
// the usership. One entry per distinct asset.
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

} // anonymous namespace

auto apply_item_property(
    App_context&                                       context,
    erhe::Item_base&                                   item,
    const erhe::property::Dependency_property&         property,
    const std::optional<erhe::property::Local_state>&  state
) -> bool
{
    return apply_item_property(context, item, item, property, state);
}

auto apply_item_property(
    App_context&                                       context,
    erhe::Item_base&                                   item,
    erhe::property::Dependency_object&                 target,
    const erhe::property::Dependency_property&         property,
    const std::optional<erhe::property::Local_state>&  state
) -> bool
{
    if ((&target != &item) && item.is_sealed()) {
        // D24: a sub-object of a sealed item is as sealed as the item.
        log_operations->warn("property '{}' on a sub-object of sealed '{}' not applied", property.get_name(), item.get_name());
        return false;
    }
    if (const std::shared_ptr<erhe::Item_base> referenced = referenced_item(state); referenced && !is_reference_allowed(context, item, *referenced)) {
        return false;
    }
    if (!target.apply_local_state(property, state)) {
        // Sealed item (D24) or a rejected value: the store logged why.
        log_operations->warn("property '{}' on '{}' not applied", property.get_name(), item.get_name());
        return false;
    }
    context.on_item_property_changed(item, property);
    return true;
}

auto make_computed_write_operation(
    const std::shared_ptr<erhe::Item_base>&      item,
    const std::optional<std::size_t>             sub_object,
    erhe::property::Dependency_object&           target,
    const erhe::property::Dependency_property&   property,
    const erhe::property::Property_value&        value
) -> std::shared_ptr<Property_set_operation>
{
    const erhe::property::Property_metadata& metadata = property.get_metadata(target.get_property_owner_type());
    if (!metadata.is_computed_writable()) {
        return {};
    }
    const erhe::property::Dependency_property&       writes = *metadata.compute_writes;
    const std::optional<erhe::property::Local_state> before = target.read_local_state(writes);
    if (!target.set_value(property, value)) {
        return {};
    }
    return std::make_shared<Property_set_operation>(item, sub_object, writes, before, target.read_local_state(writes));
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
    adopt_reference_usership(context, m_userships, referenced_item(m_before));
    adopt_reference_usership(context, m_userships, referenced_item(m_after));
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
    if (!m_sub_object.has_value()) {
        return apply_item_property(context, *m_item, m_property, state);
    }
    erhe::property::Dependency_object* target = m_item->get_property_sub_object(m_sub_object.value());
    if (target == nullptr) {
        log_operations->warn("property '{}' on '{}': sub-object {} no longer exists, not applied", m_property.get_name(), m_item->get_name(), m_sub_object.value());
        return false;
    }
    return apply_item_property(context, *m_item, *target, m_property, state);
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
        if (const std::shared_ptr<erhe::Item_base> referenced = referenced_item(*state); referenced) {
            out_items.insert(referenced.get());
        }
    }
}

//

Property_set_apply_operation::Property_set_apply_operation(
    const std::vector<std::shared_ptr<erhe::Item_base>>& items,
    erhe::property::Property_set                         values
)
    : m_values{std::move(values)}
{
    m_targets.reserve(items.size());
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        if (!item) {
            continue;
        }
        Target target{.item = item, .before = {}};
        target.before.reserve(m_values.size());
        for (const erhe::property::Property_set::Entry& entry : m_values.entries()) {
            target.before.push_back(item->read_local_value(*entry.property));
        }
        m_targets.push_back(std::move(target));
    }
    set_description(fmt::format("Set {} properties on {} items", m_values.size(), m_targets.size()));
}

Property_set_apply_operation::~Property_set_apply_operation() noexcept = default;

void Property_set_apply_operation::adopt_userships(App_context& context)
{
    if (m_userships_adopted || (context.asset_manager == nullptr)) {
        return;
    }
    m_userships_adopted = true;
    for (const erhe::property::Property_set::Entry& entry : m_values.entries()) {
        adopt_reference_usership(context, m_userships, referenced_item(std::optional<erhe::property::Property_value>{entry.value}));
    }
    for (const Target& target : m_targets) {
        for (const std::optional<erhe::property::Property_value>& before : target.before) {
            adopt_reference_usership(context, m_userships, referenced_item(before));
        }
    }
}

void Property_set_apply_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());
    adopt_userships(context);
    for (const Target& target : m_targets) {
        const erhe::property::Dependency_object::Change_batch batch{*target.item};
        for (const erhe::property::Property_set::Entry& entry : m_values.entries()) {
            apply_item_property(context, *target.item, *entry.property, erhe::property::Local_state{entry.value});
        }
    }
}

void Property_set_apply_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());
    for (const Target& target : m_targets) {
        const erhe::property::Dependency_object::Change_batch batch{*target.item};
        const std::vector<erhe::property::Property_set::Entry>& entries = m_values.entries();
        for (std::size_t i = 0, end = entries.size(); i < end; ++i) {
            apply_item_property(context, *target.item, *entries[i].property, to_local_state(target.before[i]));
        }
    }
}

void Property_set_apply_operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    for (const Target& target : m_targets) {
        out_items.insert(target.item.get());
        for (const std::optional<erhe::property::Property_value>& before : target.before) {
            if (const std::shared_ptr<erhe::Item_base> referenced = referenced_item(before); referenced) {
                out_items.insert(referenced.get());
            }
        }
    }
    for (const erhe::property::Property_set::Entry& entry : m_values.entries()) {
        if (const std::shared_ptr<erhe::Item_base> referenced = referenced_item(std::optional<erhe::property::Property_value>{entry.value}); referenced) {
            out_items.insert(referenced.get());
        }
    }
}

}
