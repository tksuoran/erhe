#pragma once

#include "erhe_property/dependency_property.hpp"
#include "erhe_property/expression.hpp"
#include "erhe_property/owner_type.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_property/property_value.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace erhe::property {

class Dependency_object;

// Subscription to one property on one object. Move-only; unsubscribes on
// destruction or release(). Outlives the object safely: the object's
// destruction turns the token into a no-op.
class Observer_token
{
public:
    Observer_token() = default;
    Observer_token(const Observer_token&) = delete;
    Observer_token& operator=(const Observer_token&) = delete;
    Observer_token(Observer_token&& other) noexcept;
    Observer_token& operator=(Observer_token&& other) noexcept;
    ~Observer_token() noexcept;

    [[nodiscard]] auto is_active() const -> bool;
    void release();

private:
    friend class Dependency_object;
    class Observer_list;
    Observer_token(std::weak_ptr<Observer_list> list, uint64_t id);

    std::weak_ptr<Observer_list> m_list{};
    uint64_t                     m_id  {0};
};

using Observer_callback = std::function<void(Dependency_object&, const Property_changed_args&)>;

// Values of inherits-flagged properties over a subtree, captured before a
// tree change so the change notifications after it carry the right old
// values (see capture_inheritance_snapshot).
class Inheritance_snapshot
{
public:
    struct Entry
    {
        Dependency_object*         object;
        const Dependency_property* property;
        Property_value             value;
        Value_source               source;
    };
    std::vector<Entry> entries;
};

// WPF DependencyObject: a sparse store of per-property values with
// precedence coerced > local > style > inherited > default, validate / coerce /
// changed callbacks from the property metadata, a virtual changed hook,
// per-object observers, change batching, and expressions driving
// properties from other properties (D22).
class Dependency_object
{
public:
    Dependency_object();
    // Copies local values (and their coerced values), expression texts
    // (unresolved: the copy resolves them itself) and the style pointer;
    // observers, dependents, the seal and pending batches are not copied.
    Dependency_object(const Dependency_object& other);
    Dependency_object& operator=(const Dependency_object& other);
    virtual ~Dependency_object() noexcept;

    // Per-class owner type id (owner_type.hpp): the registry key for what
    // properties this object has and which metadata applies. Item<> returns
    // the id of the object's class; a runtime-defined kind returns its own
    // id.
    [[nodiscard]] virtual auto get_property_owner_type() const -> Owner_type { return root_owner_type; }

    // A second owner type whose properties this object may hold as local
    // values for its inheritance descendants to read (D30): the editor's
    // content-library folder names its category's item class, so a
    // Materials folder holds Material values that the materials below it
    // inherit. Such a property is addressed on the object by its qualified
    // name and never bridged (Property_registry::is_secondary_property).
    // nullopt: none (the default).
    [[nodiscard]] virtual auto get_secondary_property_owner_type() const -> std::optional<Owner_type> { return std::nullopt; }

    // Tree for inherits-flagged properties (Hierarchy overrides both).
    [[nodiscard]] virtual auto get_inheritance_parent() const -> const Dependency_object* { return nullptr; }
    virtual void for_each_inheritance_child(const std::function<void(Dependency_object&)>& callback) { static_cast<void>(callback); }

    // The object an expression reference path names (D22): "" is this
    // object; Item_base adds ".." (the inheritance parent) and, through its
    // Item_host, an item named by a slash-separated path
    // (Hierarchy::get_path()) or by a bare name. nullptr = unresolved
    // (retried on every read and evaluation).
    [[nodiscard]] virtual auto resolve_expression_object(std::string_view path) const -> Dependency_object*
    {
        return path.empty() ? const_cast<Dependency_object*>(this) : nullptr;
    }

    // Object references (D28). get_reference_path() is the text form of a
    // reference to this object, the inverse of resolve_expression_object
    // on the referencing object (Hierarchy: the item's path; Item_base:
    // the item name; the library default is empty, "no path"). get_shared_reference() is the owning
    // pointer an Object_reference stores (Item_base: shared_from_this;
    // the default is null, "not shareable").
    [[nodiscard]] virtual auto get_reference_path  () const -> std::string                        { return {}; }
    [[nodiscard]] virtual auto get_shared_reference() const -> std::shared_ptr<Dependency_object> { return {}; }

    // Typed access
    template <Property_storable T>
    [[nodiscard]] auto get_value(const Property<T>& property) const -> T
    {
        return get_as<T>(get_value(property.get()));
    }

    template <Property_storable T>
    void set_value(const Property<T>& property, const T& value)
    {
        set_value_internal(property.get(), make_value<T>(value), false, false);
    }

    template <Property_storable T>
    void set_value(const Property_key<T>& key, const T& value)
    {
        set_value_internal(key.get(), make_value<T>(value), true, false);
    }

    template <Property_storable T>
    void clear_value(const Property<T>& property)
    {
        clear_value_internal(property.get(), false);
    }

    template <Property_storable T>
    void clear_value(const Property_key<T>& key)
    {
        clear_value_internal(key.get(), true);
    }

    template <Property_storable T>
    [[nodiscard]] auto read_local_value(const Property<T>& property) const -> std::optional<T>
    {
        const std::optional<Property_value> value = read_local_value(property.get());
        if (!value.has_value()) {
            return std::nullopt;
        }
        return get_as<T>(value.value());
    }

    // Sealing (D24, WPF Freeze): while sealed, every write of the local
    // layer (set_value, set_current_value, clear_value, set_expression,
    // apply_local_state) is rejected the way a read-only write is - one
    // logged error, nothing changes. Reads, inherited values and their
    // notifications, observers and installed expressions keep working. A
    // copy is not sealed.
    void               seal     ()       { m_sealed = true; }
    void               unseal   ()       { m_sealed = false; }
    [[nodiscard]] auto is_sealed() const -> bool { return m_sealed; }
    // True when a write of `property` is refused because the object is
    // sealed: the seal is on and the property is not flagged
    // Property_flags::writable_when_sealed. Read-only rows and disabled
    // editors ask this rather than is_sealed() alone.
    [[nodiscard]] auto is_write_sealed(const Dependency_property& property) const -> bool;

    // Style (D25): one shared style source per object - another
    // Dependency_object whose LOCAL values are the style (a Property_style,
    // or the editor's style item, doc/style-library.md D1) - read between
    // the local and inherited layers; a bridged property ignores it.
    // set_style notifies every property whose effective value or source
    // changes (locals shadow the style and are untouched); nullptr clears.
    // False (logged) on a sealed object. The source keeps a list of its
    // users: a change of its local layer notifies every user without a
    // local value of that property, so an edited style is live.
    auto               set_style(std::shared_ptr<const Dependency_object> style) -> bool;
    [[nodiscard]] auto get_style() const -> const std::shared_ptr<const Dependency_object>& { return m_style; }
    [[nodiscard]] auto get_style_user_count() const -> std::size_t;

    // Untyped access (editor, undo, serialization, MCP). Writes to a
    // read-only property or a sealed object are rejected here (false);
    // the typed key overloads are the only write path for read-only
    // properties. set_value on a property driven by an expression replaces
    // the expression with the value.
    [[nodiscard]] auto get_value       (const Dependency_property& property) const -> Property_value;
    auto               set_value       (const Dependency_property& property, const Property_value& value) -> bool;
    // Dependency_property::validate (the value alone) followed by the
    // property's optional bridge validation (the value on this object, e.g. a
    // name that must be unique among siblings). set_value runs it before it
    // writes anything; a caller that wants the reason ahead of the write - the
    // MCP set_item_property, which queues an undoable operation - runs it
    // itself and reports out_error.
    [[nodiscard]] auto validate_value  (const Dependency_property& property, const Property_value& value, std::string& out_error) const -> bool;
    auto               clear_value     (const Dependency_property& property) -> bool;
    [[nodiscard]] auto read_local_value(const Dependency_property& property) const -> std::optional<Property_value>;
    [[nodiscard]] auto has_local_value (const Dependency_property& property) const -> bool;
    // Local (entry or bridge) or style value: the object is the origin of
    // the value its descendants inherit.
    [[nodiscard]] auto has_own_value   (const Dependency_property& property) const -> bool;
    [[nodiscard]] auto get_value_source(const Dependency_property& property) const -> Value_source;
    [[nodiscard]] auto is_coerced      (const Dependency_property& property) const -> bool;

    // Expressions (D22). set_expression compiles `text` and installs it as
    // the property's local layer; false (and a logged error) on a syntax
    // error, a string target, a read-only property, or a formula that
    // reaches its own target through already-resolved references - nothing
    // changes then. References resolve lazily; get_expression_error is
    // empty after a successful evaluation. set_current_value writes the
    // effective value and keeps the expression (WPF SetCurrentValue).
    // read_local_state / apply_local_state carry the exact local layer
    // (value, expression or nothing) for undo.
    auto               set_expression      (const Dependency_property& property, std::string_view text) -> bool;
    [[nodiscard]] auto get_expression      (const Dependency_property& property) const -> std::optional<std::string_view>;
    [[nodiscard]] auto get_expression_error(const Dependency_property& property) const -> std::string_view;
    auto               set_current_value   (const Dependency_property& property, const Property_value& value) -> bool;
    [[nodiscard]] auto read_local_state    (const Dependency_property& property) const -> std::optional<Local_state>;
    auto               apply_local_state   (const Dependency_property& property, const std::optional<Local_state>& state) -> bool;

    // Re-evaluates every expression that reads `property` of this object.
    // For storage that changed outside set_value (a bridged property
    // written through its own member, e.g. Node::handle_transform_update);
    // set_value paths call it themselves. One null check when nothing
    // depends on the object.
    void invalidate_dependents(const Dependency_property& property) const;

    // Re-evaluates every expression that reads any property of this object,
    // for a storage-change funnel that does not know which properties
    // changed (Graph_editor_node::mark_dirty). Guard with has_dependents().
    void invalidate_dependents() const;
    [[nodiscard]] auto has_dependents() const -> bool { return static_cast<bool>(m_dependents) && !m_dependents->empty(); }

    // Re-runs the coerce callback against the current local value and
    // notifies if the effective value changed (WPF CoerceValue). A property
    // without a local value on this object is coerced on every read and has
    // nothing to re-run.
    void coerce_value(const Dependency_property& property);

    void for_each_local_value(const std::function<void(const Dependency_property&, const Property_value&)>& callback) const;

    // Observers (R16). The overload without a property observes every
    // property of the object (D21).
    [[nodiscard]] auto add_observer(const Dependency_property& property, Observer_callback callback) -> Observer_token;
    [[nodiscard]] auto add_observer(Observer_callback callback) -> Observer_token;

    // Inheritance support for tree changes: capture on the subtree root
    // before the tree changes, apply after. apply notifies every object in
    // the snapshot whose effective value or source changed.
    [[nodiscard]] auto capture_inheritance_snapshot() -> Inheritance_snapshot;
    void               apply_inheritance_snapshot(const Inheritance_snapshot& snapshot);

    // While one is alive, changed notifications on the object are queued and
    // delivered once when the outermost batch ends, one per property with
    // the value before the batch and the value after.
    class Change_batch
    {
    public:
        explicit Change_batch(Dependency_object& object);
        Change_batch(const Change_batch&) = delete;
        Change_batch& operator=(const Change_batch&) = delete;
        ~Change_batch() noexcept;
    private:
        Dependency_object& m_object;
    };

protected:
    virtual void on_property_changed(const Property_changed_args& args) { static_cast<void>(args); }

private:
    struct Effective_value_entry
    {
        Effective_value_entry() = default;
        Effective_value_entry(uint16_t index, Property_value local);
        Effective_value_entry(const Effective_value_entry& other);
        Effective_value_entry& operator=(const Effective_value_entry& other);
        Effective_value_entry(Effective_value_entry&& other) noexcept = default;
        Effective_value_entry& operator=(Effective_value_entry&& other) noexcept = default;
        ~Effective_value_entry() noexcept = default;

        uint16_t                      index{0};
        Property_value                local;      // the stored value, or the last evaluated result of `expression`
        std::optional<Property_value> coerced;
        std::unique_ptr<Expression>   expression; // D22; on a bridged property the entry carries only this
    };

    // (target object, target property) reading `source_index` of this
    // object through an expression (WPF DependentList).
    struct Dependent
    {
        Dependency_object* target;
        uint16_t           target_index;
        uint16_t           source_index;
    };

    struct Pending_change
    {
        uint16_t       index;
        Property_value old_value;
        Value_source   old_source;
        Property_value new_value;
        Value_source   new_source;
    };

    [[nodiscard]] auto find_entry          (uint16_t index) const -> const Effective_value_entry*;
    [[nodiscard]] auto find_entry          (uint16_t index) -> Effective_value_entry*;
    [[nodiscard]] auto find_or_create_entry(uint16_t index) -> Effective_value_entry&;
    void               remove_entry        (uint16_t index);

    [[nodiscard]] auto get_metadata       (const Dependency_property& property) const -> const Property_metadata&;
    [[nodiscard]] auto get_base_value     (const Dependency_property& property, Value_source& out_source) const -> Property_value;
    [[nodiscard]] auto get_effective_value(const Dependency_property& property, Value_source& out_source) const -> Property_value;
    [[nodiscard]] auto get_inherited_value(const Dependency_property& property) const -> std::optional<Property_value>;
    [[nodiscard]] auto get_style_value    (const Dependency_property& property) const -> std::optional<Property_value>;

    [[nodiscard]] auto reject_if_sealed   (const Dependency_property& property) const -> bool;

    // The effective value the object would have without its style layer
    // (inherited or default, coerced): a style user's value before the
    // source gained the property, or after it lost it.
    [[nodiscard]] auto get_effective_value_below_style(const Dependency_property& property, Value_source& out_source) const -> Property_value;
    // Style users (D25 live edit): this object as a style source.
    void add_style_user   (Dependency_object& user) const;
    void remove_style_user(Dependency_object& user) const;
    void propagate_to_style_users(const Property_changed_args& args);
    auto               set_value_internal  (const Dependency_property& property, const Property_value& value, bool allow_read_only, bool keep_expression) -> bool;
    auto               clear_value_internal(const Dependency_property& property, bool allow_read_only) -> bool;
    void store_coerced       (const Dependency_property& property, Effective_value_entry& entry);

    // Expressions
    [[nodiscard]] auto entry_is_bridged_expression(const Effective_value_entry& entry, const Property_metadata& metadata) const -> bool;
    void               resolve_references          (Effective_value_entry& entry, const Dependency_property& property);
    [[nodiscard]] auto evaluate_into               (Effective_value_entry& entry, const Dependency_property& property) -> bool;
    void               evaluate_expression         (Effective_value_entry& entry, const Dependency_property& property);
    void               detach_expression           (Effective_value_entry& entry);
    [[nodiscard]] auto expression_reaches          (const Dependency_object& object, uint16_t index, const Dependency_object& target, uint16_t target_index, int depth) const -> bool;
    void               add_dependent               (const Dependent& dependent);
    void               remove_dependents_of        (const Dependency_object& target, uint16_t target_index);
    void               on_source_destroyed         (const Dependency_object& source);

    void notify(
        const Dependency_property& property,
        const Property_value&      old_value,
        Value_source               old_source,
        const Property_value&      new_value,
        Value_source               new_source
    );
    void deliver(const Property_changed_args& args);
    [[nodiscard]] auto add_observer_entry(uint16_t index, Observer_callback callback) -> Observer_token;
    void flush_batch();
    void propagate_to_descendants(
        const Dependency_property& property,
        const Property_value&      old_value,
        const Property_value&      new_value
    );
    void capture_inheritance_snapshot_recursive(Inheritance_snapshot& snapshot);

    std::vector<Effective_value_entry>              m_entries;     // sorted by index
    std::shared_ptr<Observer_token::Observer_list>  m_observers;   // allocated on first add_observer
    std::unique_ptr<std::vector<Dependent>>         m_dependents;  // allocated when the first expression resolves to this object
    std::vector<Pending_change>                     m_pending;
    std::shared_ptr<const Dependency_object>        m_style;
    mutable std::unique_ptr<std::vector<Dependency_object*>> m_style_users; // allocated when this object first becomes a style source
    int                                             m_batch_depth{0};
    bool                                            m_sealed{false};
};

} // namespace erhe::property
