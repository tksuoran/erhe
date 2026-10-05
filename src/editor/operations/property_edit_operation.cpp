#include "operations/property_edit_operation.hpp"
#include "app_context.hpp"
#include "editor_log.hpp"
#include "operations/item_property_apply.hpp"
#include "operations/property_set_operation.hpp"

#include "erhe_item/item.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_write_recording.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace editor {

namespace {

class Owning_item
{
public:
    std::shared_ptr<erhe::Item_base> item;
    std::optional<std::size_t>       sub_object;
};

// The item whose document state `object` is: the item itself, or for a mesh
// primitive the mesh that holds it with the primitive's index as the D29
// sub-object. Empty when no live, shared-owned item owns the object (a
// Property_style, a primitive value outside a mesh).
auto find_owning_item(erhe::property::Dependency_object& object) -> Owning_item
{
    if (erhe::Item_base* const item = dynamic_cast<erhe::Item_base*>(&object); item != nullptr) {
        return Owning_item{.item = item->weak_from_this().lock(), .sub_object = std::nullopt};
    }
    if (erhe::scene::Mesh_primitive* const primitive = dynamic_cast<erhe::scene::Mesh_primitive*>(&object); primitive != nullptr) {
        erhe::scene::Mesh* const mesh = primitive->get_owner();
        if ((mesh == nullptr) || (mesh->get_property_sub_object(primitive->get_index()) != &object)) {
            return {};
        }
        std::shared_ptr<erhe::Item_base> owner = mesh->weak_from_this().lock();
        if (!owner) {
            return {};
        }
        return Owning_item{.item = std::move(owner), .sub_object = primitive->get_index()};
    }
    return {};
}

auto describe_target(const Owning_item& owner) -> std::string
{
    if (!owner.item) {
        return "<not an item>";
    }
    if (!owner.sub_object.has_value()) {
        return fmt::format("{} '{}'", owner.item->get_type_name(), owner.item->get_name());
    }
    return fmt::format(
        "{} '{}' [{}]",
        owner.item->get_type_name(), owner.item->get_name(), owner.item->get_property_sub_object_label(owner.sub_object.value())
    );
}

auto describe_property(const Owning_item& owner, erhe::property::Dependency_object& object, const erhe::property::Dependency_property& property) -> std::string
{
    // An attached or secondary property by its qualified name (D3, D30).
    const std::string name = erhe::property::Property_registry::get().qualified_name(object, property);
    return fmt::format("'{}' on {}", name, describe_target(owner));
}

} // anonymous namespace

Property_edit_operation::Property_edit_operation(std::string description, Edit_function edit)
    : m_edit{std::move(edit)}
{
    ERHE_VERIFY(m_edit);
    set_description(std::move(description));
}

Property_edit_operation::~Property_edit_operation() noexcept = default;

void Property_edit_operation::record(App_context& context)
{
    std::vector<erhe::property::Property_write_record>  written;
    std::vector<erhe::property::Property_write_refusal> refusals;
    {
        erhe::property::Property_write_recording recording;
        m_edit();
        written = recording.take_records();
        const std::span<const erhe::property::Property_write_refusal> refused = recording.get_refusals();
        refusals.assign(refused.begin(), refused.end());
    }
    m_records.reserve(written.size());
    for (erhe::property::Property_write_record& write : written) {
        Owning_item owner = find_owning_item(*write.object);
        if (!owner.item) {
            // Not document state of a live item: that write belongs in a
            // bespoke operation, and this one could not restore it.
            ERHE_FATAL(
                "Property_edit_operation '%s': property '%s' was written on an object no item owns",
                describe().c_str(),
                std::string{write.property->get_name()}.c_str()
            );
        }
        m_records.push_back(
            Record{
                .item       = std::move(owner.item),
                .sub_object = owner.sub_object,
                .property   = write.property,
                .before     = std::move(write.before),
                .after      = std::move(write.after)
            }
        );
    }

    std::string error;
    if (!refusals.empty()) {
        const erhe::property::Property_write_refusal& refusal = refusals.front();
        error = fmt::format("write of {} refused", describe_property(find_owning_item(*refusal.object), *refusal.object, *refusal.property));
    } else if (m_records.empty()) {
        error = "the edit wrote no property";
    } else {
        // The gates apply_item_property adds to the store's own, applied to
        // every recorded write - through set_value or through any setter -
        // so that undo and redo can apply what the edit wrote.
        for (std::size_t i = 0, end = m_records.size(); i < end; ++i) {
            const Record& record = m_records[i];
            // D24: a mesh primitive carries no seal of its own; its mesh's
            // seal covers it.
            if (is_sealed_sub_object(*record.item, *written[i].object)) {
                error = fmt::format(
                    "write of {} refused: the item is sealed",
                    describe_property(Owning_item{.item = record.item, .sub_object = record.sub_object}, *written[i].object, *record.property)
                );
                break;
            }
            // D28 host check.
            const std::shared_ptr<erhe::Item_base> referenced = get_referenced_item(record.after);
            if (referenced && !is_item_reference_allowed(context, *record.item, *referenced)) {
                error = fmt::format(
                    "write of {} refused: it cannot reference {} '{}'",
                    describe_property(Owning_item{.item = record.item, .sub_object = record.sub_object}, *written[i].object, *record.property),
                    referenced->get_type_name(),
                    referenced->get_name()
                );
                break;
            }
        }
    }

    if (!error.empty()) {
        set_error(error);
        log_operations->warn("Op Execute {} failed: {}", describe(), error);
        // Restore what the edit did write, in record order (as undo does).
        // The edit never reported these writes, so restoring them reports
        // nothing either.
        // m_records[i] is written[i] resolved (its before state moved there).
        for (std::size_t i = 0, end = written.size(); i < end; ++i) {
            if (!written[i].object->apply_local_state(*written[i].property, m_records[i].before)) {
                log_operations->error("Op Execute {}: property '{}' could not be restored", describe(), written[i].property->get_name());
            }
        }
        m_records.clear();
        m_edit = {};
        return;
    }

    for (const Record& record : m_records) {
        adopt_reference_usership(context, m_userships, get_referenced_item(record.before));
        adopt_reference_usership(context, m_userships, get_referenced_item(record.after));
    }
    // The writes happened during the edit; only their editor consequences
    // remain.
    for (const Record& record : m_records) {
        context.on_item_property_changed(*record.item, *record.property);
    }
    // Released last: the function's captures may be the only owners of the
    // items it wrote, and the records above read those items through raw
    // pointers until m_records holds them.
    m_edit = {};
}

void Property_edit_operation::apply(App_context& context, const State state)
{
    // A recording open now would take these restores for edits.
    ERHE_VERIFY(erhe::property::Property_write_recording::get_active() == nullptr);
    // Record order for both directions: an edit that wrote A, whose changed
    // callback wrote B, recorded [A, B]; restoring A first lets the callback
    // write B, and restoring B afterwards puts back B's own recorded state.
    for (const Record& record : m_records) {
        apply_item_property(context, *record.item, record.sub_object, *record.property, (state == State::before) ? record.before : record.after);
    }
}

void Property_edit_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());
    if (m_edit) {
        record(context);
        return;
    }
    apply(context, State::after);
}

void Property_edit_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());
    apply(context, State::before);
}

void Property_edit_operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    for (const Record& record : m_records) {
        out_items.insert(record.item.get());
        for (const std::optional<erhe::property::Local_state>* state : {&record.before, &record.after}) {
            if (const std::shared_ptr<erhe::Item_base> referenced = get_referenced_item(*state); referenced) {
                out_items.insert(referenced.get());
            }
        }
    }
}

auto Property_edit_operation::get_records() const -> std::span<const Record>
{
    return m_records;
}

}
