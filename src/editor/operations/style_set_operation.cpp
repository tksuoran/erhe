#include "operations/style_set_operation.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "content_library/style.hpp"
#include "editor_log.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "scene/item_lookup.hpp"
#include "scene/scene_root.hpp"

#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_set.hpp"

#include <fmt/format.h>

namespace editor {

Style_set_operation::Style_set_operation(
    const std::shared_ptr<erhe::Item_base>&               item,
    std::shared_ptr<const erhe::property::Dependency_object> before,
    std::shared_ptr<const erhe::property::Dependency_object> after
)
    : m_item  {item}
    , m_before{std::move(before)}
    , m_after {std::move(after)}
{
    set_description(
        fmt::format(
            "Set {} '{}' style = {}",
            item->get_type_name(),
            item->get_name(),
            m_after ? fmt::format("'{}'", m_after->get_reference_path()) : "none"
        )
    );
}

Style_set_operation::~Style_set_operation() noexcept = default;

void Style_set_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());
    apply(context, m_after);
}

void Style_set_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());
    apply(context, m_before);
}

void Style_set_operation::apply(App_context& context, const std::shared_ptr<const erhe::property::Dependency_object>& style)
{
    if (!m_item) {
        return;
    }
    // Consequences (D11) for every property either style names; the store
    // itself notified only the ones whose effective value changed.
    const std::shared_ptr<const erhe::property::Dependency_object> old_style = m_item->get_style();
    if (!m_item->set_style(style)) {
        log_operations->warn("style on '{}' not applied", m_item->get_name());
        return;
    }
    for (const std::shared_ptr<const erhe::property::Dependency_object>& touched : {old_style, style}) {
        if (!touched) {
            continue;
        }
        touched->for_each_local_value(
            [&context, this](const erhe::property::Dependency_property& property, const erhe::property::Property_value&) {
                context.on_item_property_changed(*m_item, property);
            }
        );
    }
}

auto make_style_from_values(
    App_context&                                         context,
    const std::vector<std::shared_ptr<erhe::Item_base>>& items,
    const erhe::property::Property_set&                  values,
    const std::string_view                               name
) -> std::shared_ptr<Compound_operation>
{
    if (items.empty() || !items.front()) {
        return {};
    }
    const std::shared_ptr<erhe::Item_base>& first = items.front();
    Scene_root* const scene_root = find_scene_root_for_item(context, *first);
    const std::shared_ptr<Content_library> library = (scene_root != nullptr) ? scene_root->get_content_library() : std::shared_ptr<Content_library>{};
    if (!library) {
        log_operations->warn("style from '{}': item '{}' belongs to no scene library", name, first->get_name());
        return {};
    }
    std::shared_ptr<Style> style = std::make_shared<Style>(make_unique_style_name(*library, name));
    // Only the values a style can hold (by identity), as paste.
    const erhe::property::Property_registry& registry = erhe::property::Property_registry::get();
    std::size_t value_count = 0;
    for (const erhe::property::Property_set::Entry& entry : values.entries()) {
        if (entry.property->is_read_only() || !registry.is_secondary_property(*style, *entry.property)) {
            continue;
        }
        if (style->set_value(*entry.property, entry.value)) {
            ++value_count;
        }
    }
    if (value_count == 0) {
        return {};
    }
    Compound_operation::Parameters parameters;
    parameters.operations.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = context,
                .item    = style,
                .parent  = library->get_scope(erhe::Item_type::style),
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        if (!item || item->is_sealed()) {
            continue;
        }
        parameters.operations.push_back(std::make_shared<Style_set_operation>(item, item->get_style(), style));
    }
    if (parameters.operations.size() < 2) {
        return {};
    }
    std::shared_ptr<Compound_operation> compound = std::make_shared<Compound_operation>(std::move(parameters));
    compound->set_description(fmt::format("[{}] Style '{}' from values", compound->get_serial(), style->get_name()));
    return compound;
}

void Style_set_operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    if (m_item) {
        out_items.insert(m_item.get());
    }
}

} // namespace editor
