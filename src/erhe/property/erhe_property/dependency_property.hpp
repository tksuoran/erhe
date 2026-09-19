#pragma once

#include "erhe_property/enum_info.hpp"
#include "erhe_property/owner_type.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_property/property_value.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace erhe::property {

class Dependency_object;
class Property_registry;

// One registered property: immutable after registration except for
// metadata overrides added by other owner types (override_metadata /
// add_owner), which also happen during static initialization.
class Dependency_property
{
public:
    class Registration
    {
    public:
        std::string_view  name;
        Property_type     type;
        Owner_type        owner_type;        // id of the registering class (owner_type.hpp)
        Property_metadata metadata;
        Validate_callback validate  {};
        const Enum_info*  enum_info {nullptr}; // required when type == enumeration
        bool              read_only {false};
        bool              attached  {false};
        // Attached only: the class of the objects that may hold the value
        // (WPF attached properties are typed by their holder too - a Grid.Row
        // sits on a UIElement). Listing and qualified lookup offer the
        // property to that class and its descendants only.
        Owner_type        holder_type{};
    };

    [[nodiscard]] auto get_index      () const -> uint16_t         { return m_index; }
    [[nodiscard]] auto get_name       () const -> std::string_view { return m_name; }
    [[nodiscard]] auto get_type       () const -> Property_type    { return m_type; }
    [[nodiscard]] auto get_owner_type () const -> Owner_type       { return m_owner_type; }
    [[nodiscard]] auto is_read_only   () const -> bool             { return m_read_only; }
    [[nodiscard]] auto is_attached    () const -> bool             { return m_attached; }
    [[nodiscard]] auto get_holder_type() const -> Owner_type       { return m_holder_type; } // attached only
    [[nodiscard]] auto get_enum_info  () const -> const Enum_info* { return m_enum_info; }

    // Attached only: whether an object of object_type (the holder type or
    // a descendant of it) may hold the value. False for a non-attached
    // property: its own chain lists it.
    [[nodiscard]] auto applies_to(Owner_type object_type) const -> bool;

    // Metadata resolution for an object whose get_property_owner_type() is
    // object_type: the override registered for the nearest owner type on
    // the object's ancestor chain (object_type itself first), else the
    // default metadata. Override lists are short (usually empty), so each
    // level is a linear scan.
    [[nodiscard]] auto get_metadata        (Owner_type object_type) const -> const Property_metadata&;
    [[nodiscard]] auto get_default_metadata() const -> const Property_metadata& { return m_default_metadata; }
    [[nodiscard]] auto get_default_value   (Owner_type object_type) const -> const Property_value&;

    // Type check plus the validate callback. False means the write is dropped.
    [[nodiscard]] auto validate(const Property_value& value) const -> bool;

    // Registers metadata for another owner type (WPF OverrideMetadata). A
    // missing default_value keeps the default metadata's default.
    void override_metadata(Owner_type owner_type, Property_metadata metadata);

    // override_metadata plus lookup of this property under (owner_type, name)
    // (WPF AddOwner).
    void add_owner(Owner_type owner_type, Property_metadata metadata);

private:
    friend class Property_registry;
    Dependency_property(uint16_t index, Registration&& registration);

    class Override
    {
    public:
        Owner_type        owner_type;
        Property_metadata metadata;
    };

    uint16_t              m_index;
    std::string           m_name;
    Property_type         m_type;
    Owner_type            m_owner_type;
    bool                  m_read_only;
    bool                  m_attached;
    Owner_type            m_holder_type;
    const Enum_info*      m_enum_info;
    Validate_callback     m_validate;
    Property_metadata     m_default_metadata;
    std::vector<Override> m_overrides;
};

class Property_registry
{
public:
    [[nodiscard]] static auto get() -> Property_registry&;

    // Registration happens from static initializers of the owning classes
    // and from single-threaded early startup (descriptor-driven
    // registrations whose tables are function-local statics, e.g. the
    // texture graph's register_texture_graph_properties), before any
    // thread other than main exists. (owner_type, name) must be unique.
    auto register_property(Dependency_property::Registration&& registration) -> const Dependency_property&;
    void add_owner        (const Dependency_property& property, Owner_type owner_type);

    // Exact (owner type, name) lookup: the registration made for that id.
    [[nodiscard]] auto find(Owner_type owner_type, std::string_view name) const -> const Dependency_property*;

    // What an object whose get_property_owner_type() is object_type means
    // by `name`: find() on object_type, then on each ancestor up to the
    // root; the nearest registration wins (WPF derived-DType shadowing).
    // A qualified name `<owner>.<name>` (WPF `Grid.Row`) resolves to the
    // attached property `name` registered by the owner type called
    // `<owner>`, for any object; the chain walk never returns an attached
    // property and a qualified name never resolves a non-attached one.
    [[nodiscard]] auto find_for_object(Owner_type object_type, std::string_view name) const -> const Dependency_property*;
    // The object form: the owner-type walk above, then a qualified name
    // that names a secondary property of the object (D30).
    [[nodiscard]] auto find_for_object(const Dependency_object& object, std::string_view name) const -> const Dependency_property*;
    // True when the object may hold `property` through its secondary owner
    // type (Dependency_object::get_secondary_property_owner_type, D30): the
    // property is not attached, is registered on the secondary type, one
    // of its ancestors or one of its descendants, is not on the object's
    // own owner chain, and is neither bridged nor computed for the object.
    [[nodiscard]] auto is_secondary_property(const Dependency_object& object, const Dependency_property& property) const -> bool;
    // Every secondary property of the object: the secondary type's own
    // chain in the order for_each_property_of_object lists it, then the
    // own registrations of each descendant type, in allocation order.
    void for_each_secondary_property(const Dependency_object& object, const std::function<void(const Dependency_property&)>& callback) const;
    // The owner type called `name`, if one has been allocated.
    [[nodiscard]] auto find_owner_type(std::string_view name) const -> std::optional<Owner_type>;
    // `<owner>.<name>` for an attached property, the plain name otherwise:
    // the name serialization, MCP and row labels use.
    [[nodiscard]] auto qualified_name (const Dependency_property& property) const -> std::string;
    // The name `object` addresses `property` by: `<owner>.<name>` for an
    // attached property and for a secondary one (D30), the plain name
    // otherwise.
    [[nodiscard]] auto qualified_name (const Dependency_object& object, const Dependency_property& property) const -> std::string;
    // Every attached registration, in registration order (R7 listing).
    void for_each_attached_property(const std::function<void(const Dependency_property&)>& callback) const;
    // The attached properties an object of the given type may hold
    // (Dependency_property::applies_to), in registration order.
    void for_each_attached_property_of(Owner_type object_type, const std::function<void(const Dependency_property&)>& callback) const;
    [[nodiscard]] auto get      (uint16_t index) const -> const Dependency_property&;
    [[nodiscard]] auto get_count() const -> std::size_t;

    // Non-attached properties an object whose get_property_owner_type() is
    // object_type has: the root's registrations first, then each level down
    // to object_type's own, each level in registration order. A name
    // registered at several levels is visited once, at the nearest level;
    // a property owned by several levels (add_owner) likewise.
    void for_each_property_of_object(Owner_type object_type, const std::function<void(const Dependency_property&)>& callback) const;

    // Owner type id table (owner_type.hpp): entry 0 is the root.
    auto               allocate_owner_type(Owner_type parent, std::string_view name) -> Owner_type;
    [[nodiscard]] auto get_owner_parent   (Owner_type id) const -> Owner_type;
    [[nodiscard]] auto get_owner_name     (Owner_type id) const -> std::string_view;

private:
    Property_registry();

    // A deque keeps the names at stable addresses, so a string_view handed
    // out by get_owner_name stays valid across later allocations.
    class Owner_entry
    {
    public:
        Owner_type  parent;
        std::string name;
    };

    class Owner_name_key
    {
    public:
        Owner_type  owner_type;
        std::string name;
        [[nodiscard]] auto operator==(const Owner_name_key&) const -> bool = default;
    };
    class Owner_name_hash
    {
    public:
        [[nodiscard]] auto operator()(const Owner_name_key& key) const -> std::size_t
        {
            return std::hash<std::string>{}(key.name) ^ (std::hash<Owner_type>{}(key.owner_type) * 0x9e3779b97f4a7c15ull);
        }
    };

    // Ancestor chain of object_type, nearest first, ending with the root.
    [[nodiscard]] auto owner_chain(Owner_type object_type) const -> std::vector<Owner_type>;

    mutable std::mutex                                                 m_mutex;
    std::deque<Owner_entry>                                            m_owner_types;
    std::vector<std::unique_ptr<Dependency_property>>                  m_properties;
    std::unordered_map<Owner_name_key, uint16_t, Owner_name_hash>      m_by_owner_and_name;
    std::vector<std::vector<uint16_t>>                                 m_by_owner; // indexed by owner type id, registration order
};

// Conversion between an object's member and the stored Property_value, for
// member-backed registrations (Property<T>::register_member, D28). The
// identity serves every Property_storable member type (enumerations
// through make_value / get_as); a std::shared_ptr<U> member is stored as an
// Object_reference, cast to and from U (U may be a Dependency_object class
// or an interface the pointee also implements), and its validate rejects a
// non-null pointee that is not a U. Instantiate where U is complete.
template <typename Member>
struct Member_value_traits;

template <Property_storable M>
struct Member_value_traits<M>
{
    using stored_type = M;
    [[nodiscard]] static auto to_value  (const M& member) -> Property_value  { return make_value<M>(member); }
    [[nodiscard]] static auto from_value(const Property_value& value) -> M   { return get_as<M>(value); }
    [[nodiscard]] static auto validate  (const Property_value&) -> bool      { return true; }
};

template <typename U>
struct Member_value_traits<std::shared_ptr<U>>
{
    using stored_type = Object_reference;
    [[nodiscard]] static auto to_value(const std::shared_ptr<U>& member) -> Property_value
    {
        return Object_reference{std::dynamic_pointer_cast<Dependency_object>(member)};
    }
    [[nodiscard]] static auto from_value(const Property_value& value) -> std::shared_ptr<U>
    {
        return std::dynamic_pointer_cast<U>(std::get<Object_reference>(value).object);
    }
    [[nodiscard]] static auto validate(const Property_value& value) -> bool
    {
        const Object_reference& reference = std::get<Object_reference>(value);
        return (!reference.object) || (dynamic_cast<const U*>(reference.object.get()) != nullptr);
    }
};

// The weak counterpart: a std::weak_ptr<U> member is stored as a
// Weak_object_reference, read as the locked U (an expired target reads as
// null), and its validate rejects a live pointee that is not a U.
template <typename U>
struct Member_value_traits<std::weak_ptr<U>>
{
    using stored_type = Weak_object_reference;
    [[nodiscard]] static auto to_value(const std::weak_ptr<U>& member) -> Property_value
    {
        return Weak_object_reference{std::dynamic_pointer_cast<Dependency_object>(member.lock())};
    }
    [[nodiscard]] static auto from_value(const Property_value& value) -> std::weak_ptr<U>
    {
        return std::dynamic_pointer_cast<U>(std::get<Weak_object_reference>(value).object.lock());
    }
    [[nodiscard]] static auto validate(const Property_value& value) -> bool
    {
        const std::shared_ptr<Dependency_object> object = std::get<Weak_object_reference>(value).object.lock();
        return (!object) || (dynamic_cast<const U*>(object.get()) != nullptr);
    }
};

// Typed handle to a registered property. Copyable, trivially cheap.
template <Property_storable T>
class Property
{
public:
    Property() = default;
    explicit Property(const Dependency_property* property) : m_property{property} {}

    [[nodiscard]] auto is_valid() const -> bool                       { return m_property != nullptr; }
    [[nodiscard]] auto get     () const -> const Dependency_property& { return *m_property; }
    [[nodiscard]] auto get_ptr () const -> const Dependency_property* { return m_property; }
    [[nodiscard]] operator const Dependency_property&() const         { return *m_property; }

    // Registration entry points. The enum_info overload is the only one
    // valid for enumeration types; the other only for non-enumeration types.
    template <Property_storable U = T>
        requires (!Property_enum_type<U>)
    static auto register_property(
        std::string_view  name,
        Owner_type        owner_type,
        Property_metadata metadata = {},
        Validate_callback validate = {}
    ) -> Property<T>
    {
        return Property<T>{
            &Property_registry::get().register_property(
                Dependency_property::Registration{
                    .name       = name,
                    .type       = property_type_of<T>(),
                    .owner_type = owner_type,
                    .metadata   = std::move(metadata),
                    .validate   = std::move(validate),
                }
            )
        };
    }

    template <Property_storable U = T>
        requires Property_enum_type<U>
    static auto register_property(
        std::string_view  name,
        Owner_type        owner_type,
        const Enum_info&  enum_info,
        Property_metadata metadata = {},
        Validate_callback validate = {}
    ) -> Property<T>
    {
        return Property<T>{
            &Property_registry::get().register_property(
                Dependency_property::Registration{
                    .name       = name,
                    .type       = Property_type::enumeration,
                    .owner_type = owner_type,
                    .metadata   = std::move(metadata),
                    .validate   = std::move(validate),
                    .enum_info  = &enum_info,
                }
            )
        };
    }

    // An attached property (R7): registered by owner_type, held by objects
    // of holder_type and its descendants (Layout registers the per-child
    // hints a Node holds).
    template <Property_storable U = T>
        requires (!Property_enum_type<U>)
    static auto register_attached(
        std::string_view  name,
        Owner_type        owner_type,
        Owner_type        holder_type,
        Property_metadata metadata = {},
        Validate_callback validate = {}
    ) -> Property<T>
    {
        return Property<T>{
            &Property_registry::get().register_property(
                Dependency_property::Registration{
                    .name        = name,
                    .type        = property_type_of<T>(),
                    .owner_type  = owner_type,
                    .metadata    = std::move(metadata),
                    .validate    = std::move(validate),
                    .attached    = true,
                    .holder_type = holder_type,
                }
            )
        };
    }

    template <Property_storable U = T>
        requires Property_enum_type<U>
    static auto register_attached(
        std::string_view  name,
        Owner_type        owner_type,
        Owner_type        holder_type,
        const Enum_info&  enum_info,
        Property_metadata metadata = {},
        Validate_callback validate = {}
    ) -> Property<T>
    {
        return Property<T>{
            &Property_registry::get().register_property(
                Dependency_property::Registration{
                    .name        = name,
                    .type        = Property_type::enumeration,
                    .owner_type  = owner_type,
                    .metadata    = std::move(metadata),
                    .validate    = std::move(validate),
                    .enum_info   = &enum_info,
                    .attached    = true,
                    .holder_type = holder_type,
                }
            )
        };
    }

    // A member-backed property (D28, D18 semantics): the owner's member is
    // the storage, reached through `access` (a callable returning a
    // reference to the member from an Owner&, const or not - a generic
    // lambda `[](auto& o) -> auto& { return o.a.b.c; }` or the
    // pointer-to-member overloads below). `set` writes the member through
    // Member_value_traits and then runs `after_set(owner)`, the consequence
    // a hand-written bridge ran inline; a write of the value the member
    // already holds does neither (R4: consequences follow effective changes
    // only). The traits' validate is combined with `validate`. An
    // enumeration member takes the Enum_info the way register_property does.
    template <typename Owner, typename Member, typename Accessor>
        requires (!Property_enum_type<T>) && std::is_same_v<typename Member_value_traits<Member>::stored_type, T>
    static auto register_member(
        std::string_view            name,
        Owner_type                  owner_type,
        Accessor                    access,
        Property_metadata           metadata  = {},
        std::type_identity_t<std::function<void(Owner&)>> after_set = {},
        Validate_callback           validate  = {}
    ) -> Property<T>
    {
        return register_member_impl<Owner, Member>(name, owner_type, nullptr, std::move(access), std::move(metadata), std::move(after_set), std::move(validate));
    }

    template <typename Owner, typename Member, typename Accessor>
        requires Property_enum_type<T> && std::is_same_v<typename Member_value_traits<Member>::stored_type, T>
    static auto register_member(
        std::string_view            name,
        Owner_type                  owner_type,
        const Enum_info&            enum_info,
        Accessor                    access,
        Property_metadata           metadata  = {},
        std::type_identity_t<std::function<void(Owner&)>> after_set = {},
        Validate_callback           validate  = {}
    ) -> Property<T>
    {
        return register_member_impl<Owner, Member>(name, owner_type, &enum_info, std::move(access), std::move(metadata), std::move(after_set), std::move(validate));
    }

    template <typename Owner, typename Member>
        requires (!Property_enum_type<T>) && std::is_same_v<typename Member_value_traits<Member>::stored_type, T>
    static auto register_member(
        std::string_view            name,
        Owner_type                  owner_type,
        Member Owner::*             member,
        Property_metadata           metadata  = {},
        std::type_identity_t<std::function<void(Owner&)>> after_set = {},
        Validate_callback           validate  = {}
    ) -> Property<T>
    {
        return register_member_impl<Owner, Member>(
            name, owner_type, nullptr,
            [member](auto& owner) -> auto& { return owner.*member; },
            std::move(metadata), std::move(after_set), std::move(validate)
        );
    }

    template <typename Owner, typename Member>
        requires Property_enum_type<T> && std::is_same_v<typename Member_value_traits<Member>::stored_type, T>
    static auto register_member(
        std::string_view            name,
        Owner_type                  owner_type,
        const Enum_info&            enum_info,
        Member Owner::*             member,
        Property_metadata           metadata  = {},
        std::type_identity_t<std::function<void(Owner&)>> after_set = {},
        Validate_callback           validate  = {}
    ) -> Property<T>
    {
        return register_member_impl<Owner, Member>(
            name, owner_type, &enum_info,
            [member](auto& owner) -> auto& { return owner.*member; },
            std::move(metadata), std::move(after_set), std::move(validate)
        );
    }

    // A computed property (D26): read-only, its effective value is
    // `compute(object)` on every read, no local / style / inherited layer,
    // not listed among local values. The owner calls
    // Dependency_object::invalidate_dependents where the inputs change.
    template <Property_storable U = T>
        requires (!Property_enum_type<U>)
    static auto register_computed(
        std::string_view  name,
        Owner_type        owner_type,
        Compute_callback  compute,
        Property_metadata metadata = {}
    ) -> Property<T>
    {
        metadata.compute = std::move(compute);
        return Property<T>{
            &Property_registry::get().register_property(
                Dependency_property::Registration{
                    .name       = name,
                    .type       = property_type_of<T>(),
                    .owner_type = owner_type,
                    .metadata   = std::move(metadata),
                    .read_only  = true,
                }
            )
        };
    }

    // A writable computed property (D26): `set` writes `writes`, the
    // stored property the provider derives the value from, and the
    // property is not read-only. Clearing it and driving it by an
    // expression stay rejected; an undoable edit records `writes`.
    template <Property_storable U = T>
        requires (!Property_enum_type<U>)
    static auto register_computed(
        std::string_view            name,
        Owner_type                  owner_type,
        Compute_callback            compute,
        Compute_set_callback        set,
        const Dependency_property&  writes,
        Property_metadata           metadata = {}
    ) -> Property<T>
    {
        metadata.compute        = std::move(compute);
        metadata.compute_set    = std::move(set);
        metadata.compute_writes = &writes;
        return Property<T>{
            &Property_registry::get().register_property(
                Dependency_property::Registration{
                    .name       = name,
                    .type       = property_type_of<T>(),
                    .owner_type = owner_type,
                    .metadata   = std::move(metadata),
                    .read_only  = false,
                }
            )
        };
    }

    template <Property_storable U = T>
        requires Property_enum_type<U>
    static auto register_computed(
        std::string_view  name,
        Owner_type        owner_type,
        const Enum_info&  enum_info,
        Compute_callback  compute,
        Property_metadata metadata = {}
    ) -> Property<T>
    {
        metadata.compute = std::move(compute);
        return Property<T>{
            &Property_registry::get().register_property(
                Dependency_property::Registration{
                    .name       = name,
                    .type       = Property_type::enumeration,
                    .owner_type = owner_type,
                    .metadata   = std::move(metadata),
                    .enum_info  = &enum_info,
                    .read_only  = true,
                }
            )
        };
    }

private:
    template <typename Owner, typename Member, typename Accessor>
    static auto register_member_impl(
        std::string_view                  name,
        Owner_type                        owner_type,
        const Enum_info*                  enum_info,
        Accessor                          access,
        Property_metadata                 metadata,
        std::function<void(Owner&)>       after_set,
        Validate_callback                 validate
    ) -> Property<T>
    {
        using Traits = Member_value_traits<Member>;
        metadata.bridge = Property_bridge{
            .get = [access](const Dependency_object& object) -> Property_value {
                const Owner& owner = static_cast<const Owner&>(object);
                return Traits::to_value(access(owner));
            },
            .set = [access, after_set](Dependency_object& object, const Property_value& value) {
                Owner& owner = static_cast<Owner&>(object);
                if (Traits::to_value(access(owner)) == value) {
                    return;
                }
                access(owner) = Traits::from_value(value);
                if (after_set) {
                    after_set(owner);
                }
            }
        };
        Validate_callback combined = [validate = std::move(validate)](const Property_value& value) -> bool {
            return Traits::validate(value) && ((!validate) || validate(value));
        };
        return Property<T>{
            &Property_registry::get().register_property(
                Dependency_property::Registration{
                    .name       = name,
                    .type       = property_type_of<T>(),
                    .owner_type = owner_type,
                    .metadata   = std::move(metadata),
                    .validate   = std::move(combined),
                    .enum_info  = enum_info,
                }
            )
        };
    }

    const Dependency_property* m_property{nullptr};
};

// Write permission for a read-only property (WPF DependencyPropertyKey). The
// owner keeps the key private and hands out get_property() for reading.
template <Property_storable T>
class Property_key
{
public:
    Property_key() = default;

    template <Property_storable U = T>
        requires (!Property_enum_type<U>)
    static auto register_read_only(
        std::string_view  name,
        Owner_type        owner_type,
        Property_metadata metadata = {},
        Validate_callback validate = {}
    ) -> Property_key<T>
    {
        return Property_key<T>{
            Property<T>{
                &Property_registry::get().register_property(
                    Dependency_property::Registration{
                        .name       = name,
                        .type       = property_type_of<T>(),
                        .owner_type = owner_type,
                        .metadata   = std::move(metadata),
                        .validate   = std::move(validate),
                        .read_only  = true,
                    }
                )
            }
        };
    }

    template <Property_storable U = T>
        requires Property_enum_type<U>
    static auto register_read_only(
        std::string_view  name,
        Owner_type        owner_type,
        const Enum_info&  enum_info,
        Property_metadata metadata = {},
        Validate_callback validate = {}
    ) -> Property_key<T>
    {
        return Property_key<T>{
            Property<T>{
                &Property_registry::get().register_property(
                    Dependency_property::Registration{
                        .name       = name,
                        .type       = Property_type::enumeration,
                        .owner_type = owner_type,
                        .metadata   = std::move(metadata),
                        .validate   = std::move(validate),
                        .enum_info  = &enum_info,
                        .read_only  = true,
                    }
                )
            }
        };
    }

    [[nodiscard]] auto get_property() const -> Property<T>               { return m_property; }
    [[nodiscard]] auto get         () const -> const Dependency_property& { return m_property.get(); }

private:
    explicit Property_key(Property<T> property) : m_property{property} {}
    Property<T> m_property;
};

} // namespace erhe::property
