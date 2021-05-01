#pragma once

#include "erhe/geometry/property_map.hpp"
#include <map>
#include <unordered_map>
#include <memory>

namespace erhe::geometry
{

template <typename Key_type>
class Property_map_collection
{
private:
    struct Entry
    {
        Entry() = default;
        ~Entry() = default;

        Entry(const std::string&           key,
              Property_map_base<Key_type>* p)
            : key  {key}
            , value{p}
        {
        }

        Entry(const Entry&) = delete;

        auto operator=(const Entry&)
        -> Entry& = delete;

        Entry(Entry&& other) noexcept
            : key  {other.key}
            , value{std::move(other.value)}
        {
        }

        auto operator=(Entry&& other)
        -> Entry&
        {
            key = other.key;
            value = std::move(other.value);
        }

        std::string key;
        std::unique_ptr<Property_map_base<Key_type>> value;
    };

    using Collection_type = std::vector<Entry>;

public:
    void clear();

    auto size() const
    -> size_t;

    template <typename Value_type>
    auto create(const Property_map_descriptor& descriptor)
    -> Property_map<Key_type, Value_type>*;

    void insert(Property_map_base<Key_type>* map);

    void remove(const std::string& name);

    template <typename Value_type>
    auto contains(const std::string& name) const
    -> bool;

    auto find_any(const std::string& name)
    -> Property_map_base<Key_type>*;

    template <typename Value_type>
    auto find(const Property_map_descriptor& descriptor) const
    -> Property_map<Key_type, Value_type>*;

    template <typename Value_type>
    auto find_or_create(const Property_map_descriptor& name)
    -> Property_map<Key_type, Value_type>*;

    void interpolate(Property_map_collection<Key_type>&                   destination,
                     std::vector<std::vector<std::pair<float, Key_type>>> key_new_to_olds);

private:
    Collection_type m_entries;
};

} // namespace erhe::geometry

#include "property_map_collection.inl"
