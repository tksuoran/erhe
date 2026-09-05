#include "erhe_property/dependency_object.hpp"
#include "erhe_property/property_log.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <limits>

namespace erhe::property {

// Observers

class Observer_token::Observer_list
{
public:
    // index == any_property matches every property (D21)
    static constexpr uint16_t any_property = std::numeric_limits<uint16_t>::max();

    struct Entry
    {
        uint16_t          index;
        uint64_t          id;
        Observer_callback callback;
    };
    std::vector<Entry> entries;
    uint64_t           next_id{1};

    void remove(const uint64_t id)
    {
        std::erase_if(entries, [id](const Entry& entry) { return entry.id == id; });
    }
};

Observer_token::Observer_token(std::weak_ptr<Observer_list> list, const uint64_t id)
    : m_list{std::move(list)}
    , m_id  {id}
{
}

Observer_token::Observer_token(Observer_token&& other) noexcept
    : m_list{std::move(other.m_list)}
    , m_id  {other.m_id}
{
    other.m_id = 0;
}

Observer_token& Observer_token::operator=(Observer_token&& other) noexcept
{
    if (this != &other) {
        release();
        m_list     = std::move(other.m_list);
        m_id       = other.m_id;
        other.m_id = 0;
    }
    return *this;
}

Observer_token::~Observer_token() noexcept
{
    release();
}

auto Observer_token::is_active() const -> bool
{
    return (m_id != 0) && !m_list.expired();
}

void Observer_token::release()
{
    if (m_id != 0) {
        if (const std::shared_ptr<Observer_list> list = m_list.lock()) {
            list->remove(m_id);
        }
    }
    m_list.reset();
    m_id = 0;
}

// Effective_value_entry

Dependency_object::Effective_value_entry::Effective_value_entry(const uint16_t index, Property_value local)
    : index{index}
    , local{std::move(local)}
{
}

Dependency_object::Effective_value_entry::Effective_value_entry(const Effective_value_entry& other)
    : index     {other.index}
    , local     {other.local}
    , coerced   {other.coerced}
    , expression{other.expression ? other.expression->clone() : nullptr}
{
}

Dependency_object::Effective_value_entry& Dependency_object::Effective_value_entry::operator=(const Effective_value_entry& other)
{
    if (this != &other) {
        index      = other.index;
        local      = other.local;
        coerced    = other.coerced;
        expression = other.expression ? other.expression->clone() : nullptr;
    }
    return *this;
}

// Dependency_object

Dependency_object::Dependency_object() = default;

Dependency_object::Dependency_object(const Dependency_object& other)
    : m_entries{other.m_entries}
    , m_style  {other.m_style}
{
    if (m_style) {
        m_style->add_style_user(*this);
    }
}

Dependency_object& Dependency_object::operator=(const Dependency_object& other)
{
    if (this != &other) {
        for (Effective_value_entry& entry : m_entries) {
            detach_expression(entry);
        }
        m_entries = other.m_entries;
        if (m_style) {
            m_style->remove_style_user(*this);
        }
        m_style = other.m_style;
        if (m_style) {
            m_style->add_style_user(*this);
        }
    }
    return *this;
}

Dependency_object::~Dependency_object() noexcept
{
    if (m_style) {
        m_style->remove_style_user(*this);
        m_style.reset();
    }
    // Expressions on this object stop reading their sources; expressions
    // elsewhere that read this object lose the reference.
    for (Effective_value_entry& entry : m_entries) {
        detach_expression(entry);
    }
    if (m_dependents) {
        const std::vector<Dependent> dependents = std::move(*m_dependents);
        m_dependents.reset();
        for (const Dependent& dependent : dependents) {
            dependent.target->on_source_destroyed(*this);
        }
    }
}

auto Dependency_object::find_entry(const uint16_t index) const -> const Effective_value_entry*
{
    const auto i = std::lower_bound(
        m_entries.begin(), m_entries.end(), index,
        [](const Effective_value_entry& entry, const uint16_t value) { return entry.index < value; }
    );
    if ((i == m_entries.end()) || (i->index != index)) {
        return nullptr;
    }
    return &*i;
}

auto Dependency_object::find_entry(const uint16_t index) -> Effective_value_entry*
{
    return const_cast<Effective_value_entry*>(static_cast<const Dependency_object*>(this)->find_entry(index));
}

auto Dependency_object::find_or_create_entry(const uint16_t index) -> Effective_value_entry&
{
    const auto i = std::lower_bound(
        m_entries.begin(), m_entries.end(), index,
        [](const Effective_value_entry& entry, const uint16_t value) { return entry.index < value; }
    );
    if ((i != m_entries.end()) && (i->index == index)) {
        return *i;
    }
    return *m_entries.insert(i, Effective_value_entry{index, Property_value{}});
}

void Dependency_object::remove_entry(const uint16_t index)
{
    if (Effective_value_entry* entry = find_entry(index); entry != nullptr) {
        detach_expression(*entry);
    }
    std::erase_if(m_entries, [index](const Effective_value_entry& entry) { return entry.index == index; });
}

auto Dependency_object::get_metadata(const Dependency_property& property) const -> const Property_metadata&
{
    return property.get_metadata(get_property_owner_type());
}

// An entry on a bridged property exists only to carry an expression: the
// value lives behind the bridge.
auto Dependency_object::entry_is_bridged_expression(const Effective_value_entry& entry, const Property_metadata& metadata) const -> bool
{
    return (entry.expression != nullptr) && metadata.bridge.is_bound();
}

auto Dependency_object::get_style_value(const Dependency_property& property) const -> std::optional<Property_value>
{
    if (!m_style) {
        return std::nullopt;
    }
    return m_style->read_local_value(property);
}

auto Dependency_object::has_own_value(const Dependency_property& property) const -> bool
{
    return has_local_value(property) || (m_style && m_style->has_local_value(property));
}

auto Dependency_object::get_effective_value_below_style(const Dependency_property& property, Value_source& out_source) const -> Property_value
{
    const Property_metadata& metadata = get_metadata(property);
    Property_value value{};
    if (metadata.inherits) {
        if (std::optional<Property_value> inherited = get_inherited_value(property); inherited.has_value()) {
            out_source = Value_source::inherited;
            value = std::move(inherited.value());
            return metadata.coerce ? metadata.coerce(*this, value) : value;
        }
    }
    out_source = Value_source::default_value;
    value = metadata.compute_default ? metadata.compute_default(*this) : metadata.default_value.value();
    return metadata.coerce ? metadata.coerce(*this, value) : value;
}

auto Dependency_object::get_style_user_count() const -> std::size_t
{
    return m_style_users ? m_style_users->size() : std::size_t{0};
}

void Dependency_object::add_style_user(Dependency_object& user) const
{
    if (!m_style_users) {
        m_style_users = std::make_unique<std::vector<Dependency_object*>>();
    }
    if (std::find(m_style_users->begin(), m_style_users->end(), &user) == m_style_users->end()) {
        m_style_users->push_back(&user);
    }
}

void Dependency_object::remove_style_user(Dependency_object& user) const
{
    if (m_style_users) {
        std::erase(*m_style_users, &user);
    }
}

// D25 live edit: this object's local layer changed for args.property (the
// notify that got here says so through its sources); every user that
// reads the style for it is notified with its own old and new value.
void Dependency_object::propagate_to_style_users(const Property_changed_args& args)
{
    if (!m_style_users || m_style_users->empty()) {
        return;
    }
    const auto is_local = [](const Value_source source) { return (source == Value_source::local) || (source == Value_source::expression); };
    const bool old_local = is_local(args.old_source);
    const bool new_local = is_local(args.new_source);
    if (!old_local && !new_local) {
        return; // the source's own inherited / default value moved; its local layer did not
    }
    const std::vector<Dependency_object*> users = *m_style_users; // a notified user may change its style
    for (Dependency_object* user : users) {
        if (user->has_local_value(args.property)) {
            continue;
        }
        const Property_metadata& user_metadata = user->get_metadata(args.property);
        Value_source   old_user_source{};
        Property_value old_user_value{};
        if (old_local) {
            old_user_source = Value_source::style;
            old_user_value  = user_metadata.coerce ? user_metadata.coerce(*user, args.old_value) : args.old_value;
        } else {
            old_user_value = user->get_effective_value_below_style(args.property, old_user_source);
        }
        Value_source   new_user_source{};
        Property_value new_user_value = user->get_effective_value(args.property, new_user_source);
        user->notify(args.property, old_user_value, old_user_source, new_user_value, new_user_source);
    }
}

auto Dependency_object::get_inherited_value(const Dependency_property& property) const -> std::optional<Property_value>
{
    for (const Dependency_object* ancestor = get_inheritance_parent(); ancestor != nullptr; ancestor = ancestor->get_inheritance_parent()) {
        if (ancestor->has_own_value(property)) {
            Value_source source{};
            return ancestor->get_effective_value(property, source);
        }
    }
    return std::nullopt;
}

auto Dependency_object::get_base_value(const Dependency_property& property, Value_source& out_source) const -> Property_value
{
    const Property_metadata& metadata = get_metadata(property);
    if (metadata.is_computed()) {
        // D26: the provider's value is the effective value; no layer applies
        // and the object never has an entry (every write is rejected as
        // read-only).
        out_source = Value_source::computed;
        return metadata.compute(*this);
    }
    if (const Effective_value_entry* entry = find_entry(property.get_index()); entry != nullptr) {
        out_source = (entry->expression != nullptr) ? Value_source::expression : Value_source::local;
        if (entry_is_bridged_expression(*entry, metadata)) {
            return metadata.bridge.get(*this);
        }
        return entry->local;
    }
    if (metadata.bridge.is_bound()) {
        out_source = Value_source::local;
        return metadata.bridge.get(*this);
    }
    if (std::optional<Property_value> styled = get_style_value(property); styled.has_value()) {
        out_source = Value_source::style;
        return std::move(styled.value());
    }
    if (metadata.inherits) {
        if (std::optional<Property_value> inherited = get_inherited_value(property); inherited.has_value()) {
            out_source = Value_source::inherited;
            return std::move(inherited.value());
        }
    }
    out_source = Value_source::default_value;
    // D31: a per-object default is the bottom layer, so an inherited, style
    // or local value still overrides it.
    return metadata.compute_default ? metadata.compute_default(*this) : metadata.default_value.value();
}

auto Dependency_object::get_effective_value(const Dependency_property& property, Value_source& out_source) const -> Property_value
{
    const Effective_value_entry* entry = find_entry(property.get_index());
    if ((entry != nullptr) && (entry->expression != nullptr) && entry->expression->has_unresolved_references()) {
        // Pull: a reference that could not be resolved earlier (a source
        // created later, a loaded scene, a clone) is retried on read.
        Dependency_object* self = const_cast<Dependency_object*>(this);
        self->evaluate_expression(*self->find_entry(property.get_index()), property);
        entry = find_entry(property.get_index());
    }
    if (entry != nullptr) {
        const Property_metadata& metadata = get_metadata(property);
        out_source = (entry->expression != nullptr) ? Value_source::expression : Value_source::local;
        if (entry_is_bridged_expression(*entry, metadata)) {
            const Property_value base = metadata.bridge.get(*this);
            return metadata.coerce ? metadata.coerce(*this, base) : base;
        }
        return entry->coerced.has_value() ? entry->coerced.value() : entry->local;
    }
    Property_value base = get_base_value(property, out_source);
    const Property_metadata& metadata = get_metadata(property);
    if (metadata.coerce && (out_source != Value_source::computed)) {
        return metadata.coerce(*this, base);
    }
    return base;
}

auto Dependency_object::get_value(const Dependency_property& property) const -> Property_value
{
    Value_source source{};
    return get_effective_value(property, source);
}

auto Dependency_object::get_value_source(const Dependency_property& property) const -> Value_source
{
    Value_source source{};
    static_cast<void>(get_base_value(property, source));
    return source;
}

auto Dependency_object::is_coerced(const Dependency_property& property) const -> bool
{
    const Effective_value_entry* entry = find_entry(property.get_index());
    return (entry != nullptr) && entry->coerced.has_value();
}

auto Dependency_object::read_local_value(const Dependency_property& property) const -> std::optional<Property_value>
{
    const Property_metadata& metadata = get_metadata(property);
    const Effective_value_entry* entry = find_entry(property.get_index());
    if ((entry != nullptr) && !entry_is_bridged_expression(*entry, metadata)) {
        return entry->local;
    }
    if (metadata.bridge.is_bound()) {
        return metadata.bridge.get(*this);
    }
    return std::nullopt;
}

auto Dependency_object::has_local_value(const Dependency_property& property) const -> bool
{
    return (find_entry(property.get_index()) != nullptr) || get_metadata(property).bridge.is_bound();
}

void Dependency_object::store_coerced(const Dependency_property& property, Effective_value_entry& entry)
{
    const Property_metadata& metadata = get_metadata(property);
    entry.coerced.reset();
    if (metadata.coerce) {
        Property_value coerced = metadata.coerce(*this, entry.local);
        if (!(coerced == entry.local)) {
            ERHE_VERIFY(type_of(coerced) == property.get_type());
            entry.coerced = std::move(coerced);
        }
    }
}

auto Dependency_object::is_write_sealed(const Dependency_property& property) const -> bool
{
    return m_sealed && ((get_metadata(property).flags & Property_flags::writable_when_sealed) == 0u);
}

auto Dependency_object::reject_if_sealed(const Dependency_property& property) const -> bool
{
    if (is_write_sealed(property)) {
        log->error("property '{}': object is sealed", property.get_name());
        return true;
    }
    return false;
}

auto Dependency_object::set_value(const Dependency_property& property, const Property_value& value) -> bool
{
    return set_value_internal(property, value, false, false);
}

auto Dependency_object::set_current_value(const Dependency_property& property, const Property_value& value) -> bool
{
    return set_value_internal(property, value, false, true);
}

auto Dependency_object::set_value_internal(const Dependency_property& property, const Property_value& value, const bool allow_read_only, const bool keep_expression) -> bool
{
    if (property.is_read_only() && !allow_read_only) {
        log->error("property '{}' is read-only", property.get_name());
        return false;
    }
    if (reject_if_sealed(property)) {
        return false;
    }
    if (!property.validate(value)) {
        return false; // Dependency_property::validate logs the reason itself
    }

    const Property_metadata& metadata = get_metadata(property);
    if (metadata.bridge.validate) {
        std::string validation_error;
        if (!metadata.bridge.validate(*this, value, validation_error)) {
            log->warn("property '{}' rejected the value: {}", property.get_name(), validation_error);
            return false;
        }
    }
    if (metadata.is_computed()) {
        // D26: a writable computed property hands the value to its setter,
        // which writes the stored property the value derives from; that
        // write notifies for the stored property, this one has no entry,
        // no previous value and no notification of its own.
        if (!metadata.is_computed_writable()) {
            log->error("property '{}' is computed and has no setter", property.get_name());
            return false;
        }
        metadata.compute_set(*this, value);
        return true;
    }

    Value_source   old_source{};
    Property_value old_value = get_effective_value(property, old_source);

    Effective_value_entry* entry = find_entry(property.get_index());
    if ((entry != nullptr) && (entry->expression != nullptr) && !keep_expression) {
        // A value write replaces the expression (WPF semantics).
        detach_expression(*entry);
        entry->expression.reset();
        if (metadata.bridge.is_bound()) {
            remove_entry(property.get_index());
            entry = nullptr;
        }
    }
    if (metadata.bridge.is_bound()) {
        metadata.bridge.set(*this, value);
    } else {
        Effective_value_entry& stored = (entry != nullptr) ? *entry : find_or_create_entry(property.get_index());
        stored.local = value;
        store_coerced(property, stored);
    }

    Value_source   new_source{};
    Property_value new_value = get_effective_value(property, new_source);
    notify(property, old_value, old_source, new_value, new_source);
    return true;
}

auto Dependency_object::validate_value(
    const Dependency_property& property,
    const Property_value&      value,
    std::string&               out_error
) const -> bool
{
    if (!property.validate(value)) {
        out_error = "value rejected by property validation";
        return false;
    }
    const Property_metadata& metadata = get_metadata(property);
    if (metadata.bridge.validate && !metadata.bridge.validate(*this, value, out_error)) {
        return false;
    }
    return true;
}

auto Dependency_object::clear_value(const Dependency_property& property) -> bool
{
    return clear_value_internal(property, false);
}

auto Dependency_object::clear_value_internal(const Dependency_property& property, const bool allow_read_only) -> bool
{
    if (property.is_read_only() && !allow_read_only) {
        log->error("property '{}' is read-only", property.get_name());
        return false;
    }
    if (reject_if_sealed(property)) {
        return false;
    }
    const Property_metadata& metadata = get_metadata(property);
    if (metadata.is_computed()) {
        log->error("property '{}' is computed: it has no local value to clear", property.get_name());
        return false;
    }
    if (metadata.bridge.is_bound()) {
        // A bridged property has no "unset" state: clearing writes the
        // default (and drops an expression, through set_value_internal).
        return set_value_internal(property, metadata.default_value.value(), allow_read_only, false);
    }
    if (find_entry(property.get_index()) == nullptr) {
        return true;
    }

    Value_source   old_source{};
    Property_value old_value = get_effective_value(property, old_source);

    remove_entry(property.get_index());

    Value_source   new_source{};
    Property_value new_value = get_effective_value(property, new_source);
    notify(property, old_value, old_source, new_value, new_source);
    return true;
}

void Dependency_object::coerce_value(const Dependency_property& property)
{
    Effective_value_entry* entry = find_entry(property.get_index());
    if ((entry == nullptr) || entry_is_bridged_expression(*entry, get_metadata(property))) {
        return;
    }
    Value_source   old_source{};
    Property_value old_value = get_effective_value(property, old_source);
    store_coerced(property, *entry);
    Value_source   new_source{};
    Property_value new_value = get_effective_value(property, new_source);
    notify(property, old_value, old_source, new_value, new_source);
}

auto Dependency_object::get_default_value(const Dependency_property& property) const -> Property_value
{
    const Property_metadata& metadata = get_metadata(property);
    return metadata.compute_default ? metadata.compute_default(*this) : metadata.default_value.value();
}

void Dependency_object::refresh_computed_default(const Dependency_property& property, const Property_value& old_value, const Value_source old_source)
{
    Value_source   new_source{};
    Property_value new_value = get_effective_value(property, new_source);
    notify_self(property, old_value, old_source, new_value, new_source);
}

void Dependency_object::for_each_local_value(const std::function<void(const Dependency_property&, const Property_value&)>& callback) const
{
    const Property_registry& registry = Property_registry::get();
    // Entries and bridged properties, merged in index order (both are
    // sorted). A bridged property with an expression entry is emitted once,
    // with the bridge value.
    std::vector<const Dependency_property*> bridged;
    const Owner_type owner_type = get_property_owner_type();
    registry.for_each_property_of_object(
        owner_type,
        [&bridged, owner_type](const Dependency_property& property) {
            if (property.get_metadata(owner_type).bridge.is_bound()) {
                bridged.push_back(&property);
            }
        }
    );
    // for_each_property_of_object visits the chain root first, and the
    // registration order across translation units is not the chain order:
    // sort by index so the merge below holds.
    std::sort(
        bridged.begin(), bridged.end(),
        [](const Dependency_property* lhs, const Dependency_property* rhs) { return lhs->get_index() < rhs->get_index(); }
    );
    std::size_t b = 0;
    for (const Effective_value_entry& entry : m_entries) {
        while ((b < bridged.size()) && (bridged[b]->get_index() < entry.index)) {
            callback(*bridged[b], bridged[b]->get_metadata(owner_type).bridge.get(*this));
            ++b;
        }
        if ((b < bridged.size()) && (bridged[b]->get_index() == entry.index)) {
            callback(*bridged[b], bridged[b]->get_metadata(owner_type).bridge.get(*this));
            ++b;
            continue;
        }
        callback(registry.get(entry.index), entry.local);
    }
    for (; b < bridged.size(); ++b) {
        callback(*bridged[b], bridged[b]->get_metadata(owner_type).bridge.get(*this));
    }
}

auto Dependency_object::add_observer(const Dependency_property& property, Observer_callback callback) -> Observer_token
{
    return add_observer_entry(property.get_index(), std::move(callback));
}

auto Dependency_object::add_observer(Observer_callback callback) -> Observer_token
{
    return add_observer_entry(Observer_token::Observer_list::any_property, std::move(callback));
}

auto Dependency_object::add_observer_entry(const uint16_t index, Observer_callback callback) -> Observer_token
{
    if (!m_observers) {
        m_observers = std::make_shared<Observer_token::Observer_list>();
    }
    const uint64_t id = m_observers->next_id++;
    m_observers->entries.push_back(
        Observer_token::Observer_list::Entry{
            .index    = index,
            .id       = id,
            .callback = std::move(callback)
        }
    );
    return Observer_token{m_observers, id};
}

// Expressions (D22)

auto Dependency_object::set_expression(const Dependency_property& property, const std::string_view text) -> bool
{
    if (property.is_read_only()) {
        log->error("property '{}' is read-only", property.get_name());
        return false;
    }
    if (reject_if_sealed(property)) {
        return false;
    }
    if (get_metadata(property).is_computed()) {
        log->error("property '{}' is computed: an expression cannot drive it", property.get_name());
        return false;
    }
    std::string error;
    if (!validate_expression_text(property, text, error)) {
        log->error("expression '{}' for property '{}' rejected: {}", text, property.get_name(), error);
        return false;
    }
    std::unique_ptr<Expression> expression = Expression::compile(text, property.get_type(), error);

    Value_source   old_source{};
    Property_value old_value = get_effective_value(property, old_source);

    // Install: the previous local value (if any) becomes the initial cached
    // result until the first evaluation replaces it. The previous
    // expression (if any) is kept aside until the new one is accepted.
    const bool had_entry = (find_entry(property.get_index()) != nullptr);
    Effective_value_entry& entry = find_or_create_entry(property.get_index());
    detach_expression(entry);
    std::unique_ptr<Expression> previous = std::move(entry.expression);
    entry.expression = std::move(expression);
    const Property_metadata& metadata = get_metadata(property);
    if (!metadata.bridge.is_bound() && !had_entry) {
        entry.local = old_value;
        entry.coerced.reset();
    }

    // Resolve what resolves now, and refuse a formula that reaches its own
    // target through the resolved graph.
    resolve_references(entry, property);
    for (const Expression::Reference& reference : entry.expression->get_references()) {
        if (reference.is_resolved() && expression_reaches(*reference.object, reference.property->get_index(), *this, property.get_index(), 0)) {
            log->error("expression '{}' for property '{}' rejected: cycle through {{{}}}", text, property.get_name(), reference.describe());
            detach_expression(entry);
            entry.expression = std::move(previous); // unresolved; resolves again on the next read
            if (!had_entry) {
                remove_entry(property.get_index());
            }
            return false;
        }
    }

    // The first evaluation, notified against the state before the install
    // (a value that stays equal still changes source).
    if (entry.expression->begin_evaluation()) {
        static_cast<void>(evaluate_into(entry, property));
        Value_source   new_source{};
        Property_value new_value = get_effective_value(property, new_source);
        notify(property, old_value, old_source, new_value, new_source);
        entry.expression->end_evaluation();
    }
    return true;
}

auto Dependency_object::get_expression(const Dependency_property& property) const -> std::optional<std::string_view>
{
    const Effective_value_entry* entry = find_entry(property.get_index());
    if ((entry == nullptr) || !entry->expression) {
        return std::nullopt;
    }
    return entry->expression->get_text();
}

auto Dependency_object::get_expression_error(const Dependency_property& property) const -> std::string_view
{
    const Effective_value_entry* entry = find_entry(property.get_index());
    if ((entry == nullptr) || !entry->expression) {
        return {};
    }
    return entry->expression->get_error();
}

auto Dependency_object::read_local_state(const Dependency_property& property) const -> std::optional<Local_state>
{
    if (std::optional<std::string_view> text = get_expression(property); text.has_value()) {
        return Local_state{Expression_text{std::string{text.value()}}};
    }
    if (std::optional<Property_value> value = read_local_value(property); value.has_value()) {
        return Local_state{std::move(value.value())};
    }
    return std::nullopt;
}

auto Dependency_object::apply_local_state(const Dependency_property& property, const std::optional<Local_state>& state) -> bool
{
    if (!state.has_value()) {
        return clear_value(property);
    }
    if (const Expression_text* text = std::get_if<Expression_text>(&state.value()); text != nullptr) {
        return set_expression(property, text->text);
    }
    return set_value(property, std::get<Property_value>(state.value()));
}

// True when `object`'s `index` is driven by an expression that (through
// resolved references, transitively) reads target's target_index.
auto Dependency_object::expression_reaches(const Dependency_object& object, const uint16_t index, const Dependency_object& target, const uint16_t target_index, const int depth) const -> bool
{
    if ((&object == &target) && (index == target_index)) {
        return true;
    }
    if (depth > 64) {
        return true; // deeper than any sane graph: treat as a cycle
    }
    const Effective_value_entry* entry = object.find_entry(index);
    if ((entry == nullptr) || !entry->expression) {
        return false;
    }
    for (const Expression::Reference& reference : entry->expression->get_references()) {
        if (reference.is_resolved() && expression_reaches(*reference.object, reference.property->get_index(), target, target_index, depth + 1)) {
            return true;
        }
    }
    return false;
}

void Dependency_object::resolve_references(Effective_value_entry& entry, const Dependency_property& property)
{
    const Property_registry& registry = Property_registry::get();
    for (Expression::Reference& reference : entry.expression->get_references()) {
        if (reference.is_resolved()) {
            continue;
        }
        Dependency_object* object = resolve_expression_object(reference.object_path);
        if (object == nullptr) {
            continue;
        }
        const Dependency_property* source_property = registry.find_for_object(object->get_property_owner_type(), reference.property_name);
        if (source_property == nullptr) {
            continue;
        }
        reference.object   = object;
        reference.property = source_property;
        object->add_dependent(
            Dependent{
                .target       = this,
                .target_index = property.get_index(),
                .source_index = source_property->get_index()
            }
        );
    }
}

// Resolves, evaluates and stores the result without notifying. False when
// nothing was stored (error recorded on the expression, the previous value
// stays).
auto Dependency_object::evaluate_into(Effective_value_entry& entry, const Dependency_property& property) -> bool
{
    if (entry.expression->has_unresolved_references()) {
        resolve_references(entry, property);
    }
    std::optional<Property_value> result = entry.expression->evaluate(property.get_enum_info());
    if (!result.has_value()) {
        return false;
    }
    if (!property.validate(result.value())) {
        entry.expression->set_error("the result was rejected by validation");
        return false;
    }
    const Property_metadata& metadata = get_metadata(property);
    if (metadata.bridge.is_bound()) {
        metadata.bridge.set(*this, result.value());
    } else {
        entry.local = std::move(result.value());
        store_coerced(property, entry);
    }
    return true;
}

void Dependency_object::evaluate_expression(Effective_value_entry& entry, const Dependency_property& property)
{
    if (!entry.expression->begin_evaluation()) {
        return; // a cycle: the value from the evaluation up the stack stands
    }
    Value_source   old_source{};
    Property_value old_value = get_effective_value(property, old_source);
    if (evaluate_into(entry, property)) {
        Value_source   new_source{};
        Property_value new_value = get_effective_value(property, new_source);
        notify(property, old_value, old_source, new_value, new_source);
    }
    entry.expression->end_evaluation();
}

void Dependency_object::detach_expression(Effective_value_entry& entry)
{
    if (!entry.expression) {
        return;
    }
    for (Expression::Reference& reference : entry.expression->get_references()) {
        if (reference.object != nullptr) {
            // Scoped to this entry's property: the object's other
            // expressions reading the same source keep their push links.
            reference.object->remove_dependents_of(*this, entry.index);
            reference.object   = nullptr;
            reference.property = nullptr;
        }
    }
}

void Dependency_object::add_dependent(const Dependent& dependent)
{
    if (!m_dependents) {
        m_dependents = std::make_unique<std::vector<Dependent>>();
    }
    for (const Dependent& existing : *m_dependents) {
        if ((existing.target == dependent.target) && (existing.target_index == dependent.target_index) && (existing.source_index == dependent.source_index)) {
            return;
        }
    }
    m_dependents->push_back(dependent);
}

void Dependency_object::remove_dependents_of(const Dependency_object& target, const uint16_t target_index)
{
    if (!m_dependents) {
        return;
    }
    std::erase_if(
        *m_dependents,
        [&target, target_index](const Dependent& dependent) {
            return (dependent.target == &target) && (dependent.target_index == target_index);
        }
    );
    if (m_dependents->empty()) {
        m_dependents.reset();
    }
}

void Dependency_object::on_source_destroyed(const Dependency_object& source)
{
    for (Effective_value_entry& entry : m_entries) {
        if (!entry.expression) {
            continue;
        }
        for (Expression::Reference& reference : entry.expression->get_references()) {
            if (reference.object == &source) {
                reference.object   = nullptr;
                reference.property = nullptr;
                entry.expression->set_error("unresolved reference {" + reference.describe() + "}");
            }
        }
    }
}

void Dependency_object::invalidate_dependents(const Dependency_property& property) const
{
    if (!m_dependents) {
        return;
    }
    // Re-evaluation may resolve or drop dependents on this object; iterate
    // over a copy of the matching entries.
    const uint16_t index = property.get_index();
    std::vector<Dependent> matching;
    for (const Dependent& dependent : *m_dependents) {
        if (dependent.source_index == index) {
            matching.push_back(dependent);
        }
    }
    const Property_registry& registry = Property_registry::get();
    for (const Dependent& dependent : matching) {
        Effective_value_entry* entry = dependent.target->find_entry(dependent.target_index);
        if ((entry == nullptr) || !entry->expression) {
            continue;
        }
        dependent.target->evaluate_expression(*entry, registry.get(dependent.target_index));
    }
}

void Dependency_object::invalidate_dependents() const
{
    if (!m_dependents) {
        return;
    }
    // Re-evaluation may resolve or drop dependents on this object; iterate
    // over a copy.
    const std::vector<Dependent> matching = *m_dependents;
    const Property_registry& registry = Property_registry::get();
    for (const Dependent& dependent : matching) {
        Effective_value_entry* entry = dependent.target->find_entry(dependent.target_index);
        if ((entry == nullptr) || !entry->expression) {
            continue;
        }
        dependent.target->evaluate_expression(*entry, registry.get(dependent.target_index));
    }
}

// Notification

void Dependency_object::notify(
    const Dependency_property& property,
    const Property_value&      old_value,
    const Value_source         old_source,
    const Property_value&      new_value,
    const Value_source         new_source
)
{
    const bool inherits = get_metadata(property).inherits;
    notify_self(property, old_value, old_source, new_value, new_source);
    // Descendants are outside this object's batch; they learn about the
    // change immediately so their own observers see it.
    if (inherits && !(old_value == new_value)) {
        propagate_to_descendants(property, old_value, new_value);
    }
}

void Dependency_object::notify_self(
    const Dependency_property& property,
    const Property_value&      old_value,
    const Value_source         old_source,
    const Property_value&      new_value,
    const Value_source         new_source
)
{
    if (m_batch_depth > 0) {
        const uint16_t index = property.get_index();
        const auto i = std::find_if(m_pending.begin(), m_pending.end(), [index](const Pending_change& pending) { return pending.index == index; });
        if (i != m_pending.end()) {
            i->new_value  = new_value;
            i->new_source = new_source;
        } else {
            m_pending.push_back(
                Pending_change{
                    .index      = index,
                    .old_value  = old_value,
                    .old_source = old_source,
                    .new_value  = new_value,
                    .new_source = new_source
                }
            );
        }
        return;
    }

    if ((old_value == new_value) && (old_source == new_source)) {
        return;
    }
    deliver(
        Property_changed_args{
            .property   = property,
            .old_value  = old_value,
            .new_value  = new_value,
            .old_source = old_source,
            .new_source = new_source
        }
    );
}

void Dependency_object::deliver(const Property_changed_args& args)
{
    const Property_metadata& metadata = get_metadata(args.property);
    // The registering class's callback expects an object of that class;
    // a holder of the property through its secondary owner type (D30) is
    // not one, and only gets the virtual hook and the observers.
    if (metadata.property_changed && (args.property.is_attached() || is_owner_type_or_descendant(get_property_owner_type(), args.property.get_owner_type()))) {
        metadata.property_changed(*this, args);
    }
    on_property_changed(args);
    if (m_observers) {
        // Observers may unsubscribe (or subscribe) from inside the callback,
        // so iterate over a copy of the matching callbacks.
        const uint16_t index = args.property.get_index();
        std::vector<Observer_callback> callbacks;
        for (const Observer_token::Observer_list::Entry& entry : m_observers->entries) {
            if ((entry.index == index) || (entry.index == Observer_token::Observer_list::any_property)) {
                callbacks.push_back(entry.callback);
            }
        }
        for (const Observer_callback& callback : callbacks) {
            callback(*this, args);
        }
    }
    invalidate_dependents(args.property);
    propagate_to_style_users(args);
}

void Dependency_object::propagate_to_descendants(
    const Dependency_property& property,
    const Property_value&      old_value,
    const Property_value&      new_value
)
{
    for_each_inheritance_child(
        [&](Dependency_object& child) {
            if (child.has_own_value(property)) {
                return; // a local or style value shadows the subtree
            }
            const Property_metadata& child_metadata = child.get_metadata(property);
            Property_value child_old = old_value;
            Property_value child_new = new_value;
            if (child_metadata.coerce) {
                child_old = child_metadata.coerce(child, child_old);
                child_new = child_metadata.coerce(child, child_new);
            }
            child.notify(property, child_old, Value_source::inherited, child_new, Value_source::inherited);
        }
    );
}

Dependency_object::Change_batch::Change_batch(Dependency_object& object)
    : m_object{object}
{
    ++m_object.m_batch_depth;
}

Dependency_object::Change_batch::~Change_batch() noexcept
{
    --m_object.m_batch_depth;
    if (m_object.m_batch_depth == 0) {
        m_object.flush_batch();
    }
}

void Dependency_object::flush_batch()
{
    const Property_registry& registry = Property_registry::get();
    // A delivered callback may set values, which queue nothing now that the
    // depth is zero but may recurse into flush; take the pending list first.
    while (!m_pending.empty()) {
        std::vector<Pending_change> pending = std::move(m_pending);
        m_pending.clear();
        for (const Pending_change& change : pending) {
            if ((change.old_value == change.new_value) && (change.old_source == change.new_source)) {
                continue;
            }
            deliver(
                Property_changed_args{
                    .property   = registry.get(change.index),
                    .old_value  = change.old_value,
                    .new_value  = change.new_value,
                    .old_source = change.old_source,
                    .new_source = change.new_source
                }
            );
        }
    }
}

// Style (D25)

auto Dependency_object::set_style(std::shared_ptr<const Dependency_object> style) -> bool
{
    if (m_sealed) {
        log->error("set_style: object is sealed");
        return false;
    }
    if (style == m_style) {
        return true;
    }
    // Effective values before the switch for every property either style
    // names; locals shadow both styles and are left alone.
    struct Before
    {
        const Dependency_property* property;
        Property_value             value;
        Value_source               source;
    };
    std::vector<Before> before;
    const auto collect = [&](const std::shared_ptr<const Dependency_object>& from) {
        if (!from) {
            return;
        }
        from->for_each_local_value(
            [&](const Dependency_property& property, const Property_value&) {
                if (has_local_value(property)) {
                    return;
                }
                const bool seen = std::any_of(before.begin(), before.end(), [&property](const Before& b) { return b.property == &property; });
                if (seen) {
                    return;
                }
                Value_source source{};
                Property_value value = get_effective_value(property, source);
                before.push_back(Before{.property = &property, .value = std::move(value), .source = source});
            }
        );
    };
    collect(m_style);
    collect(style);
    if (m_style) {
        m_style->remove_style_user(*this);
    }
    m_style = std::move(style);
    if (m_style) {
        m_style->add_style_user(*this);
    }
    for (const Before& b : before) {
        Value_source   new_source{};
        Property_value new_value = get_effective_value(*b.property, new_source);
        notify(*b.property, b.value, b.source, new_value, new_source);
    }
    return true;
}

// Inheritance snapshots

void Dependency_object::capture_inheritance_snapshot_recursive(Inheritance_snapshot& snapshot)
{
    const Property_registry& registry = Property_registry::get();
    const Owner_type owner_type = get_property_owner_type();
    const std::size_t count = registry.get_count();
    for (uint16_t index = 0; index < count; ++index) {
        const Dependency_property& property = registry.get(index);
        if (!property.get_metadata(owner_type).inherits) {
            continue;
        }
        if (has_own_value(property)) {
            continue; // local or style value: unaffected by the tree
        }
        Value_source source{};
        Property_value value = get_effective_value(property, source);
        snapshot.entries.push_back(
            Inheritance_snapshot::Entry{
                .object   = this,
                .property = &property,
                .value    = std::move(value),
                .source   = source
            }
        );
    }
    for_each_inheritance_child([&snapshot](Dependency_object& child) { child.capture_inheritance_snapshot_recursive(snapshot); });
}

auto Dependency_object::capture_inheritance_snapshot() -> Inheritance_snapshot
{
    Inheritance_snapshot snapshot;
    capture_inheritance_snapshot_recursive(snapshot);
    return snapshot;
}

void Dependency_object::apply_inheritance_snapshot(const Inheritance_snapshot& snapshot)
{
    for (const Inheritance_snapshot::Entry& entry : snapshot.entries) {
        Dependency_object& object = *entry.object;
        Value_source   new_source{};
        Property_value new_value = object.get_effective_value(*entry.property, new_source);
        if ((new_value == entry.value) && (new_source == entry.source)) {
            continue;
        }
        // Not through notify(): descendants have their own snapshot entries.
        object.deliver(
            Property_changed_args{
                .property   = *entry.property,
                .old_value  = entry.value,
                .new_value  = new_value,
                .old_source = entry.source,
                .new_source = new_source
            }
        );
    }
}

void clear_default_valued_local_properties(Dependency_object& object)
{
    const Owner_type owner_type = object.get_property_owner_type();
    // The entries are collected first: clearing mutates the entry store the
    // enumeration walks.
    std::vector<const Dependency_property*> candidates;
    object.for_each_local_value(
        [&candidates, &object, owner_type](const Dependency_property& property, const Property_value& value) {
            if (property.is_read_only() || property.is_attached() || object.is_write_sealed(property)) {
                return;
            }
            const Property_metadata& metadata = property.get_metadata(owner_type);
            if (metadata.bridge.is_bound() || metadata.is_computed()) {
                return;
            }
            if ((metadata.flags & Property_flags::serialize) == 0u) {
                return;
            }
            if (object.get_expression(property).has_value()) {
                return; // a formula is the authored layer (D22)
            }
            if (!(value == object.get_default_value(property))) {
                return;
            }
            candidates.push_back(&property);
        }
    );
    if (candidates.empty()) {
        return;
    }
    const Dependency_object::Change_batch batch{object};
    for (const Dependency_property* property : candidates) {
        const std::optional<Property_value> local = object.read_local_value(*property);
        if (!local.has_value()) {
            continue;
        }
        if (!object.clear_value(*property)) {
            continue;
        }
        if (object.get_value_source(*property) != Value_source::default_value) {
            // An inherited (R8) or style (D25) layer stands below the local
            // one: the value is the object's own opinion after all.
            static_cast<void>(object.set_value(*property, local.value()));
        }
    }
}

} // namespace erhe::property
