#include "erhe_property/dependency_property.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/property_log.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <limits>

namespace erhe::property {

Dependency_property::Dependency_property(const uint16_t index, Registration&& registration)
    : m_index           {index}
    , m_name            {registration.name}
    , m_type            {registration.type}
    , m_owner_type      {registration.owner_type}
    , m_read_only       {registration.read_only}
    , m_attached        {registration.attached}
    , m_holder_type    {registration.holder_type}
    , m_enum_info       {registration.enum_info}
    , m_validate        {std::move(registration.validate)}
    , m_default_metadata{std::move(registration.metadata)}
{
    if (m_type == Property_type::enumeration) {
        ERHE_VERIFY(m_enum_info != nullptr);
        if (!m_default_metadata.default_value.has_value()) {
            const std::span<const Enum_entry> entries = m_enum_info->get_entries();
            m_default_metadata.default_value = Enum_value{entries.empty() ? 0 : entries.front().value};
        }
    } else if (!m_default_metadata.default_value.has_value()) {
        m_default_metadata.default_value = zero_value(m_type);
    }
    ERHE_VERIFY(type_of(m_default_metadata.default_value.value()) == m_type);
    ERHE_VERIFY(validate(m_default_metadata.default_value.value()));
}

auto Dependency_property::get_metadata(const Owner_type object_type) const -> const Property_metadata&
{
    if (m_overrides.empty()) {
        return m_default_metadata;
    }
    for (Owner_type id = object_type; ; id = get_owner_type_parent(id)) {
        for (const Override& entry : m_overrides) {
            if (entry.owner_type == id) {
                return entry.metadata;
            }
        }
        if (id == root_owner_type) {
            return m_default_metadata;
        }
    }
}

auto Dependency_property::get_default_value(const Owner_type object_type) const -> const Property_value&
{
    return get_metadata(object_type).default_value.value();
}

auto Dependency_property::validate(const Property_value& value) const -> bool
{
    if (type_of(value) != m_type) {
        log->error(
            "property '{}': value type {} does not match property type {}",
            m_name, c_str(type_of(value)), c_str(m_type)
        );
        return false;
    }
    if (m_type == Property_type::enumeration) {
        const int32_t raw = std::get<Enum_value>(value).value;
        if (!m_enum_info->contains(raw)) {
            log->error("property '{}': {} is not a {} enumerator", m_name, raw, m_enum_info->get_type_name());
            return false;
        }
    }
    if (m_validate && !m_validate(value)) {
        log->error("property '{}': value rejected by validate callback", m_name);
        return false;
    }
    return true;
}

void Dependency_property::override_metadata(const Owner_type owner_type, Property_metadata metadata)
{
    if (!metadata.default_value.has_value()) {
        metadata.default_value = m_default_metadata.default_value;
    }
    ERHE_VERIFY(type_of(metadata.default_value.value()) == m_type);
    ERHE_VERIFY(validate(metadata.default_value.value()));
    m_overrides.push_back(Override{.owner_type = owner_type, .metadata = std::move(metadata)});
    Property_registry::get().invalidate_bridged_inheriting_properties();
}

auto Dependency_property::inherits_for_any_owner() const -> bool
{
    if (m_default_metadata.inherits) {
        return true;
    }
    for (const Override& entry : m_overrides) {
        if (entry.metadata.inherits) {
            return true;
        }
    }
    return false;
}

void Dependency_property::add_owner(const Owner_type owner_type, Property_metadata metadata)
{
    override_metadata(owner_type, std::move(metadata));
    Property_registry::get().add_owner(*this, owner_type);
}

//

Property_registry::Property_registry()
{
    m_owner_types.push_back(Owner_entry{.parent = root_owner_type, .name = "root"});
}

auto Property_registry::get() -> Property_registry&
{
    static Property_registry s_registry;
    return s_registry;
}

auto Property_registry::allocate_owner_type(const Owner_type parent, const std::string_view name) -> Owner_type
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    ERHE_VERIFY(parent < m_owner_types.size());
    const Owner_type id = static_cast<Owner_type>(m_owner_types.size());
    m_owner_types.push_back(Owner_entry{.parent = parent, .name = std::string{name}});
    return id;
}

auto Property_registry::get_owner_parent(const Owner_type id) const -> Owner_type
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    ERHE_VERIFY(id < m_owner_types.size());
    return m_owner_types[id].parent;
}

auto Property_registry::get_owner_name(const Owner_type id) const -> std::string_view
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    ERHE_VERIFY(id < m_owner_types.size());
    return m_owner_types[id].name;
}

auto allocate_owner_type(const Owner_type parent, const std::string_view name) -> Owner_type
{
    return Property_registry::get().allocate_owner_type(parent, name);
}

auto get_owner_type_parent(const Owner_type id) -> Owner_type
{
    return Property_registry::get().get_owner_parent(id);
}

auto get_owner_type_name(const Owner_type id) -> std::string_view
{
    return Property_registry::get().get_owner_name(id);
}

auto is_owner_type_or_descendant(Owner_type id, const Owner_type ancestor) -> bool
{
    for (;;) {
        if (id == ancestor) {
            return true;
        }
        if (id == root_owner_type) {
            return false;
        }
        id = get_owner_type_parent(id);
    }
}

auto Property_registry::register_property(Dependency_property::Registration&& registration) -> const Dependency_property&
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    ERHE_VERIFY(m_properties.size() < std::numeric_limits<uint16_t>::max());
    ERHE_VERIFY(registration.owner_type < m_owner_types.size());
    const Owner_name_key key{.owner_type = registration.owner_type, .name = std::string{registration.name}};
    if (m_by_owner_and_name.contains(key)) {
        ERHE_FATAL("property '%s' is already registered for owner type %u (%s)", key.name.c_str(), static_cast<unsigned int>(key.owner_type), m_owner_types[key.owner_type].name.c_str());
    }
    const uint16_t index = static_cast<uint16_t>(m_properties.size());
    const Dependency_property* const default_from = registration.metadata.default_from;
    if (default_from != nullptr) {
        // D31: the source is registered already and of the same type.
        ERHE_VERIFY(default_from->get_index() < m_properties.size());
        ERHE_VERIFY(default_from->get_type() == registration.type);
        ERHE_VERIFY(!registration.metadata.compute_default);
    }
    m_properties.push_back(std::unique_ptr<Dependency_property>{new Dependency_property{index, std::move(registration)}});
    if (default_from != nullptr) {
        m_properties[default_from->get_index()]->m_default_followers.push_back(m_properties.back().get());
    }
    m_by_owner_and_name.emplace(key, index);
    if (m_by_owner.size() <= key.owner_type) {
        m_by_owner.resize(static_cast<std::size_t>(key.owner_type) + 1);
    }
    m_by_owner[key.owner_type].push_back(index);
    m_bridged_inheriting.clear();
    return *m_properties.back();
}

void Property_registry::add_owner(const Dependency_property& property, const Owner_type owner_type)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    ERHE_VERIFY(owner_type < m_owner_types.size());
    const Owner_name_key key{.owner_type = owner_type, .name = std::string{property.get_name()}};
    if (m_by_owner_and_name.contains(key)) {
        ERHE_FATAL("property '%s' is already registered for owner type %u (%s)", key.name.c_str(), static_cast<unsigned int>(owner_type), m_owner_types[owner_type].name.c_str());
    }
    m_by_owner_and_name.emplace(key, property.get_index());
    m_bridged_inheriting.clear();
    if (m_by_owner.size() <= owner_type) {
        m_by_owner.resize(static_cast<std::size_t>(owner_type) + 1);
    }
    m_by_owner[owner_type].push_back(property.get_index());
}

auto Property_registry::find(const Owner_type owner_type, const std::string_view name) const -> const Dependency_property*
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    const auto i = m_by_owner_and_name.find(Owner_name_key{.owner_type = owner_type, .name = std::string{name}});
    if (i == m_by_owner_and_name.end()) {
        return nullptr;
    }
    return m_properties[i->second].get();
}

auto Property_registry::owner_chain(const Owner_type object_type) const -> std::vector<Owner_type>
{
    std::vector<Owner_type> chain;
    const std::lock_guard<std::mutex> lock{m_mutex};
    ERHE_VERIFY(object_type < m_owner_types.size());
    for (Owner_type id = object_type; ; id = m_owner_types[id].parent) {
        chain.push_back(id);
        if (id == root_owner_type) {
            return chain;
        }
    }
}

auto Property_registry::find_for_object(const Owner_type object_type, const std::string_view name) const -> const Dependency_property*
{
    const std::size_t dot = name.find('.');
    if (dot != std::string_view::npos) {
        const std::optional<Owner_type> owner = find_owner_type(name.substr(0, dot));
        if (!owner.has_value()) {
            return nullptr;
        }
        const Dependency_property* property = find(owner.value(), name.substr(dot + 1));
        return ((property != nullptr) && property->applies_to(object_type)) ? property : nullptr;
    }
    for (Owner_type id = object_type; ; id = get_owner_type_parent(id)) {
        const Dependency_property* property = find(id, name);
        if ((property != nullptr) && !property->is_attached()) {
            return property;
        }
        if (id == root_owner_type) {
            return nullptr;
        }
    }
}

auto Property_registry::find_for_object(const Dependency_object& object, const std::string_view name) const -> const Dependency_property*
{
    const Dependency_property* property = find_for_object(object.get_property_owner_type(), name);
    if (property != nullptr) {
        return property;
    }
    const std::size_t dot = name.find('.');
    if (dot == std::string_view::npos) {
        return nullptr;
    }
    const std::optional<Owner_type> owner = find_owner_type(name.substr(0, dot));
    if (!owner.has_value()) {
        return nullptr;
    }
    property = find(owner.value(), name.substr(dot + 1));
    return ((property != nullptr) && is_secondary_property(object, *property)) ? property : nullptr;
}

auto Property_registry::is_secondary_property(const Dependency_object& object, const Dependency_property& property) const -> bool
{
    const std::optional<Owner_type> secondary = object.get_secondary_property_owner_type();
    if (!secondary.has_value() || property.is_attached()) {
        return false;
    }
    const Owner_type owner = property.get_owner_type();
    if (
        (!is_owner_type_or_descendant(secondary.value(), owner) && !is_owner_type_or_descendant(owner, secondary.value())) ||
        is_owner_type_or_descendant(object.get_property_owner_type(), owner)
    ) {
        return false;
    }
    // A bridge and a compute callback both read the registering class's
    // state from the object, which the holder is not.
    const Property_metadata& metadata = property.get_metadata(object.get_property_owner_type());
    return !metadata.bridge.is_bound() && !metadata.is_computed();
}

void Property_registry::for_each_secondary_property(const Dependency_object& object, const std::function<void(const Dependency_property&)>& callback) const
{
    const std::optional<Owner_type> secondary = object.get_secondary_property_owner_type();
    if (!secondary.has_value()) {
        return;
    }
    // The secondary type's own chain first (root first, as for an object of
    // that type), then the own registrations of each strict descendant of
    // the secondary type, descendants in allocation order.
    std::vector<const Dependency_property*> visited;
    for_each_property_of_object(
        secondary.value(),
        [this, &object, &callback, &visited](const Dependency_property& property) {
            if (is_secondary_property(object, property)) {
                visited.push_back(&property);
                callback(property);
            }
        }
    );
    std::vector<Owner_type> descendants;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        for (std::size_t id = 0; id < m_owner_types.size(); ++id) {
            if (id == secondary.value()) {
                continue;
            }
            // The parent walk under the lock: is_owner_type_or_descendant
            // would take it again.
            for (Owner_type ancestor = static_cast<Owner_type>(id); ; ancestor = m_owner_types[ancestor].parent) {
                if (ancestor == secondary.value()) {
                    descendants.push_back(static_cast<Owner_type>(id));
                    break;
                }
                if (ancestor == root_owner_type) {
                    break;
                }
            }
        }
    }
    std::vector<const Dependency_property*> own;
    for (const Owner_type descendant : descendants) {
        own.clear();
        {
            const std::lock_guard<std::mutex> lock{m_mutex};
            if (descendant < m_by_owner.size()) {
                for (const uint16_t index : m_by_owner[descendant]) {
                    own.push_back(m_properties[index].get());
                }
            }
        }
        for (const Dependency_property* property : own) {
            if (std::find(visited.begin(), visited.end(), property) != visited.end()) {
                continue;
            }
            if (is_secondary_property(object, *property)) {
                visited.push_back(property);
                callback(*property);
            }
        }
    }
}

auto Property_registry::find_owner_type(const std::string_view name) const -> std::optional<Owner_type>
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    for (std::size_t id = 0; id < m_owner_types.size(); ++id) {
        if (m_owner_types[id].name == name) {
            return static_cast<Owner_type>(id);
        }
    }
    return std::nullopt;
}

auto Property_registry::qualified_name(const Dependency_property& property) const -> std::string
{
    if (!property.is_attached()) {
        return std::string{property.get_name()};
    }
    std::string result{get_owner_name(property.get_owner_type())};
    result += '.';
    result += property.get_name();
    return result;
}

auto Property_registry::qualified_name(const Dependency_object& object, const Dependency_property& property) const -> std::string
{
    if (!property.is_attached() && !is_secondary_property(object, property)) {
        return std::string{property.get_name()};
    }
    std::string result{get_owner_name(property.get_owner_type())};
    result += '.';
    result += property.get_name();
    return result;
}

auto Dependency_property::applies_to(const Owner_type object_type) const -> bool
{
    return m_attached && is_owner_type_or_descendant(object_type, m_holder_type);
}

void Property_registry::for_each_attached_property_of(const Owner_type object_type, const std::function<void(const Dependency_property&)>& callback) const
{
    for_each_attached_property(
        [object_type, &callback](const Dependency_property& property) {
            if (property.applies_to(object_type)) {
                callback(property);
            }
        }
    );
}

void Property_registry::for_each_attached_property(const std::function<void(const Dependency_property&)>& callback) const
{
    std::vector<const Dependency_property*> attached;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        for (const std::unique_ptr<Dependency_property>& property : m_properties) {
            if (property->is_attached()) {
                attached.push_back(property.get());
            }
        }
    }
    for (const Dependency_property* property : attached) {
        callback(*property);
    }
}

auto Property_registry::get(const uint16_t index) const -> const Dependency_property&
{
    ERHE_VERIFY(index < m_properties.size());
    return *m_properties[index];
}

auto Property_registry::get_count() const -> std::size_t
{
    return m_properties.size();
}

void Property_registry::append_bridged_inheriting_properties(const Owner_type object_type, std::vector<const Dependency_property*>& properties) const
{
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if ((object_type < m_bridged_inheriting.size()) && m_bridged_inheriting[object_type].has_value()) {
            const std::vector<const Dependency_property*>& list = m_bridged_inheriting[object_type].value();
            properties.insert(properties.end(), list.begin(), list.end());
            return;
        }
    }
    // for_each_property_of_object takes the mutex itself.
    std::vector<const Dependency_property*> list;
    for_each_property_of_object(
        object_type,
        [&list, object_type](const Dependency_property& property) {
            if (property.get_metadata(object_type).bridge.is_bound() && property.inherits_for_any_owner()) {
                list.push_back(&property);
            }
        }
    );
    properties.insert(properties.end(), list.begin(), list.end());
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (object_type >= m_bridged_inheriting.size()) {
        m_bridged_inheriting.resize(static_cast<std::size_t>(object_type) + 1);
    }
    m_bridged_inheriting[object_type] = std::move(list);
}

void Property_registry::invalidate_bridged_inheriting_properties()
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_bridged_inheriting.clear();
}

void Property_registry::for_each_property_of_object(
    const Owner_type                                           object_type,
    const std::function<void(const Dependency_property&)>&     callback
) const
{
    const std::vector<Owner_type> chain = owner_chain(object_type);
    // Nearest level first so a shadowed name and a multiply-owned property
    // are claimed by the nearest level, then visit root-first.
    std::vector<const Dependency_property*> visited;
    std::vector<std::string_view>           names;
    std::vector<std::size_t>                level_begin;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        for (const Owner_type id : chain) {
            level_begin.push_back(visited.size());
            if (id >= m_by_owner.size()) {
                continue;
            }
            for (const uint16_t index : m_by_owner[id]) {
                const Dependency_property* property = m_properties[index].get();
                if (property->is_attached()) {
                    continue;
                }
                if (std::find(visited.begin(), visited.end(), property) != visited.end()) {
                    continue;
                }
                if (std::find(names.begin(), names.end(), property->get_name()) != names.end()) {
                    continue;
                }
                visited.push_back(property);
                names.push_back(property->get_name());
            }
        }
    }
    level_begin.push_back(visited.size());
    for (std::size_t level = chain.size(); level > 0; --level) {
        for (std::size_t i = level_begin[level - 1], end = level_begin[level]; i < end; ++i) {
            callback(*visited[i]);
        }
    }
}

} // namespace erhe::property
