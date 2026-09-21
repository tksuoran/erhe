#pragma once

#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace erhe::property::test {

// Owner type ids for test registrations. Functions (not variables) so a
// registration in another translation unit's static initialization finds
// the id allocated. type_a_child is a child of type_a(), type_b_child of type_b(); the rest are roots.
inline auto type_a() -> Owner_type { static const Owner_type id = allocate_owner_type(root_owner_type, "type_a"); return id; }
inline auto type_b() -> Owner_type { static const Owner_type id = allocate_owner_type(root_owner_type, "type_b"); return id; }
inline auto type_c() -> Owner_type { static const Owner_type id = allocate_owner_type(root_owner_type, "type_c"); return id; }
inline auto type_d() -> Owner_type { static const Owner_type id = allocate_owner_type(root_owner_type, "type_d"); return id; } // bridged-property tests only
inline auto type_e() -> Owner_type { static const Owner_type id = allocate_owner_type(root_owner_type, "type_e"); return id; } // animated bridged-property tests only
inline auto type_a_child() -> Owner_type { static const Owner_type id = allocate_owner_type(type_a(), "type_a_child"); return id; }
inline auto type_b_child() -> Owner_type { static const Owner_type id = allocate_owner_type(type_b(), "type_b_child"); return id; } // secondary-owner tests only

struct Recorded_change
{
    std::string    property_name;
    Property_value old_value;
    Property_value new_value;
    Value_source   old_source;
    Value_source   new_source;
};

// Minimal tree-capable Dependency_object: the inheritance virtuals plus a
// record of every on_property_changed() call.
class Test_object : public Dependency_object
{
public:
    explicit Test_object(const Owner_type type = type_a()) : m_type{type} {}
    Test_object(const Test_object&) = default;
    Test_object& operator=(const Test_object&) = default;
    ~Test_object() noexcept override
    {
        for (Test_object* child : m_children) {
            child->m_parent = nullptr;
        }
        if (m_parent != nullptr) {
            std::erase(m_parent->m_children, this);
        }
    }

    auto get_property_owner_type() const -> Owner_type override                { return m_type; }
    auto get_inheritance_parent () const -> const Dependency_object* override  { return m_parent; }
    void for_each_inheritance_child(const std::function<void(Dependency_object&)>& callback) override
    {
        for (Test_object* child : m_children) {
            callback(*child);
        }
    }

    // Reparent with the snapshot protocol Hierarchy::set_parent uses.
    void set_parent(Test_object* parent)
    {
        const Inheritance_snapshot snapshot = capture_inheritance_snapshot(parent);
        if (m_parent != nullptr) {
            std::erase(m_parent->m_children, this);
        }
        m_parent = parent;
        if (m_parent != nullptr) {
            m_parent->m_children.push_back(this);
        }
        apply_inheritance_snapshot(snapshot);
    }

    std::vector<Recorded_change> changes;

    [[nodiscard]] auto change_count(const std::string_view name) const -> std::size_t
    {
        return static_cast<std::size_t>(std::count_if(changes.begin(), changes.end(), [name](const Recorded_change& c) { return c.property_name == name; }));
    }

protected:
    void on_property_changed(const Property_changed_args& args) override
    {
        changes.push_back(
            Recorded_change{
                .property_name = std::string{args.property.get_name()},
                .old_value     = args.old_value,
                .new_value     = args.new_value,
                .old_source    = args.old_source,
                .new_source    = args.new_source
            }
        );
    }

private:
    Owner_type                m_type;
    Test_object*              m_parent{nullptr};
    std::vector<Test_object*> m_children;
};

} // namespace erhe::property::test
