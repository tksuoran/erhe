#include "operations/property_edit_operation.hpp"
#include "app_context.hpp"
#include "editor_log.hpp"
#include "operations/item_property_apply.hpp"
#include "operations/property_set_operation.hpp"
#include "rig/bone_connect.hpp"

#include "erhe_item/item.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_write_recording.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>

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

// True for a record of the item's own property that stays writable on a
// sealed item (D24, Property_flags::writable_when_sealed: lock_edit), the
// write that seals or unseals the item.
[[nodiscard]] auto is_seal_record(const Property_edit_operation::Record& record) -> bool
{
    return
        !record.sub_object.has_value() &&
        ((record.property->get_metadata(record.item->get_property_owner_type()).flags & erhe::property::Property_flags::writable_when_sealed) != 0u);
}

// An item with a seal record and other records: where its seal record goes.
class Seal_order
{
public:
    const erhe::Item_base* item{nullptr};
    std::size_t            seal_index{0};
    std::size_t            last_other_index{0};
    bool                   decided{false};
    bool                   applied{false};
};

// Calls apply_record(i) once for every record index, in the seal order of
// doc/editor/operations.md "Property_edit_operation": records keep record
// order, except that per item a seal record is applied before that item's
// other records when the item is sealed as the first of them is reached
// (the seal record's state is then what lifts the seal), and after them
// otherwise (a state that seals takes effect once they are written). The
// sealed state is read live, so an item's seal reflects the records applied
// before it.
template <typename Apply_record>
void apply_in_seal_order(const std::span<const Property_edit_operation::Record> records, Apply_record&& apply_record)
{
    // Undo / redo / rollback, not per frame; an edit seals or unseals
    // rarely, so this is usually empty.
    std::vector<Seal_order> seal_orders;
    for (std::size_t i = 0, end = records.size(); i < end; ++i) {
        if (!is_seal_record(records[i])) {
            continue;
        }
        Seal_order order{.item = records[i].item.get(), .seal_index = i};
        bool has_other = false;
        for (std::size_t j = 0; j < end; ++j) {
            if ((j != i) && (records[j].item.get() == order.item)) {
                order.last_other_index = j;
                has_other = true;
            }
        }
        if (has_other) {
            seal_orders.push_back(order);
        }
    }
    for (std::size_t i = 0, end = records.size(); i < end; ++i) {
        const erhe::Item_base* const item = records[i].item.get();
        const std::vector<Seal_order>::iterator order = std::find_if(
            seal_orders.begin(), seal_orders.end(), [item](const Seal_order& candidate) { return candidate.item == item; }
        );
        if (order == seal_orders.end()) {
            apply_record(i);
            continue;
        }
        if (!order->decided) {
            order->decided = true;
            if (item->is_sealed()) {
                apply_record(order->seal_index);
                order->applied = true;
            }
        }
        if (i == order->seal_index) {
            // Reached after the item's other records: its own position.
            if (!order->applied && (i > order->last_other_index)) {
                apply_record(i);
                order->applied = true;
            }
            continue;
        }
        apply_record(i);
        if ((i == order->last_other_index) && !order->applied && (order->seal_index < i)) {
            apply_record(order->seal_index);
            order->applied = true;
        }
    }
}

} // anonymous namespace

Property_edit_operation::Property_edit_operation(std::string description, Edit_function edit, const Property_edit_follow_ups follow_ups)
    : m_edit          {std::move(edit)}
    , m_follow_up_kind{follow_ups}
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
        // Restore what the edit did write, in seal order (as undo does).
        // The edit never reported these writes, so restoring them reports
        // nothing either.
        // m_records[i] is written[i] resolved (its before state moved there).
        apply_in_seal_order(
            m_records,
            [this, &written](const std::size_t i) {
                if (!written[i].object->apply_local_state(*written[i].property, m_records[i].before)) {
                    log_operations->error(
                        "Op Execute {}: {} could not be restored",
                        describe(), describe_property(Owning_item{.item = m_records[i].item, .sub_object = m_records[i].sub_object}, *written[i].object, *written[i].property)
                    );
                }
            }
        );
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
    if (m_follow_up_kind == Property_edit_follow_ups::bone_connect) {
        // Recorded after every write, so each reads the written state.
        for (const Record& record : m_records) {
            if (!record.sub_object.has_value()) {
                append_bone_connect_follow_ups(*record.item, *record.property, m_follow_ups);
            }
        }
        for (const std::shared_ptr<Operation>& follow_up : m_follow_ups) {
            follow_up->execute(context);
        }
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
    // Seal order on top of it: an item's seal record goes before or after
    // the item's other records, so none of them meets the seal.
    // No change batch around the restores: a batch defers the changed
    // callbacks to its end, after the later records were restored, so a
    // callback of A would overwrite the restored B of a cascade [A, B].
    apply_in_seal_order(
        m_records,
        [this, &context, state](const std::size_t i) {
            const Record& record = m_records[i];
            if (!apply_item_property(context, *record.item, record.sub_object, *record.property, (state == State::before) ? record.before : record.after)) {
                // The item no longer accepts its recorded state (a state
                // changed outside the undo history: a reference the D28 host
                // check now refuses, a sub-object that is gone); the stack
                // and the document disagree from here on.
                log_operations->error(
                    "Op {} {}: property '{}' on {} could not be applied",
                    (state == State::before) ? "Undo" : "Redo", describe(), record.property->get_name(), describe_target(Owning_item{.item = record.item, .sub_object = record.sub_object})
                );
            }
        }
    );
}

void Property_edit_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());
    if (m_edit) {
        record(context);
        return;
    }
    apply(context, State::after);
    for (const std::shared_ptr<Operation>& follow_up : m_follow_ups) {
        follow_up->execute(context);
    }
}

void Property_edit_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());
    for (auto i = m_follow_ups.rbegin(), end = m_follow_ups.rend(); i != end; ++i) {
        (*i)->undo(context);
    }
    apply(context, State::before);
}

void Property_edit_operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    for (const std::shared_ptr<Operation>& follow_up : m_follow_ups) {
        follow_up->collect_item_references(out_items);
    }
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

auto make_property_set_edit_operation(
    App_context&                            context,
    const std::shared_ptr<erhe::Item_base>& item,
    const erhe::property::Property_set&     values
) -> std::shared_ptr<Property_edit_operation>
{
    if (!item || values.empty()) {
        return {};
    }
    std::string description = fmt::format("Set {} properties on {} '{}'", values.size(), item->get_type_name(), item->get_name());
    return std::make_shared<Property_edit_operation>(
        std::move(description),
        [&context, item, values]() {
            const erhe::property::Dependency_object::Change_batch batch{*item};
            for (const erhe::property::Property_set::Entry& entry : values.entries()) {
                // Checked against the live state, after the entries before
                // it were written.
                std::string refusal;
                if (item->is_write_sealed(*entry.property)) {
                    refusal = "the item is sealed";
                } else if (!item->validate_value(*entry.property, entry.value, refusal)) {
                    // validate_value filled in the reason
                } else if (
                    const std::shared_ptr<erhe::Item_base> referenced = get_referenced_item(erhe::property::Local_state{entry.value});
                    referenced && !is_item_reference_allowed(context, *item, *referenced)
                ) {
                    refusal = fmt::format("it cannot reference {} '{}'", referenced->get_type_name(), referenced->get_name());
                } else {
                    item->set_value(*entry.property, entry.value);
                    continue;
                }
                log_operations->warn("'{}' not written on '{}': {}", entry.property->get_name(), item->get_name(), refusal);
            }
        }
    );
}

}
