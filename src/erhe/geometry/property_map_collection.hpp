#pragma once

#include "erhe/geometry/property_map.hpp"

#include <memory>
#include <string>

namespace erhe::geometry
{

template <typename Key_type>
class Property_map_collection
{
private:
    class Entry final
    {
    public:
        Entry () = default;
        ~Entry() noexcept = default;

        Entry(
            const std::string&           key,
            Property_map_base<Key_type>* p
        )
            : key  {key}
            , value{p}
        {
        }

        Entry         (const Entry&) = delete;
        void operator=(const Entry&) = delete;

        Entry(Entry&& other) noexcept
            : key  {other.key}
            , value{std::move(other.value)}
        {
        }

        auto operator=(Entry&& other) -> Entry&
        {
            key = other.key;
            value = std::move(other.value);
            return *this;
        }

        std::string key;
        std::unique_ptr<Property_map_base<Key_type>> value;
    };

    using Collection_type = std::vector<Entry>;

public:
    void clear();

    auto size() const -> size_t;

    template <typename Value_type>
    auto create(
        const Property_map_descriptor& descriptor
    ) -> Property_map<Key_type, Value_type>*;

    void insert(Property_map_base<Key_type>* map);

    void remove(const std::string& name);

    template <typename Value_type>
    auto contains(const std::string& name) const -> bool;

    auto find_base(
        const Property_map_descriptor& descriptor
    ) const -> Property_map_base<Key_type>*;

    template <typename Value_type>
    auto find(
        const Property_map_descriptor& descriptor
    ) const -> Property_map<Key_type, Value_type>*;

    template <typename Value_type>
    auto find_or_create(
        const Property_map_descriptor& name
    ) -> Property_map<Key_type, Value_type>*;

    void trim      (size_t size);
    void remap_keys(const std::vector<Key_type>& key_new_to_old);
    void interpolate(
        Property_map_collection<Key_type>&                          destination,
        const std::vector<std::vector<std::pair<float, Key_type>>>& key_new_to_olds
    );

    void merge_to            (Property_map_collection<Key_type>& source, const glm::mat4 transform);
    auto clone               () -> Property_map_collection<Key_type>;
    void transform           (const glm::mat4 matrix);
    auto clone_with_transform(const glm::mat4 matrix) -> Property_map_collection<Key_type>;

private:
    Collection_type m_entries;
};

} // namespace erhe::geometry

#include "property_map_collection.inl"
