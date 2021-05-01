#pragma once

#include "erhe/geometry/log.hpp"
#include "Tracy.hpp"

namespace erhe::geometry
{

template <typename Key_type>
inline void
Property_map_collection<Key_type>::clear()
{
    ZoneScoped;

    m_entries.clear();
}

template <typename Key_type>
inline auto
Property_map_collection<Key_type>::size() const
-> size_t
{
    return m_entries.size();
}

template <typename Key_type>
inline void
Property_map_collection<Key_type>::insert(Property_map_base<Key_type>* map)
{
    ZoneScoped;

    for (const auto& entry : m_entries)
    {
        VERIFY(entry.key != map->descriptor().name);
    }
    m_entries.emplace_back(map->descriptor().name, map);
    log_attribute_maps.trace("Added attribute map {}\n", map->descriptor().name);
}

template <typename Key_type>
inline void
Property_map_collection<Key_type>::remove(const std::string& name)
{
    ZoneScoped;

    auto i = std::remove_if(m_entries.begin(),
                            m_entries.end(),
                            [name](const Entry& entry)
                            {
                                return entry.key == name;
                            });
    if (i != m_entries.end())
    {
        m_entries.erase(i, m_entries.end());
    }
}

template <typename Key_type>
template <typename Value_type>
inline auto
Property_map_collection<Key_type>::contains(const std::string& name) const
-> bool
{
    ZoneScoped;

    for (const auto& entry : m_entries)
    {
        bool match = entry.key == name;
        if (match)
        {
            return true;
        }
    }

    return false;
}

template <typename Key_type>
inline auto
Property_map_collection<Key_type>::find_any(const std::string& name)
-> Property_map_base<Key_type>*
{
    ZoneScoped;

    for (const auto& entry : m_entries)
    {
        bool match = entry.key == name;
        if (match)
        {
            auto p = entry.value.get();
            return p;
        }
    }
    return nullptr;
}

template <typename Key_type>
template <typename Value_type>
inline auto
Property_map_collection<Key_type>::create(const Property_map_descriptor& descriptor)
-> Property_map<Key_type, Value_type>*
{
    ZoneScoped;

    // Look for existing chain
    for (const auto& entry : m_entries)
    {
        VERIFY(entry.key != descriptor.name);
    }

    // New entry
    auto p = new Property_map<Key_type, Value_type>(descriptor);
    m_entries.emplace_back(descriptor.name, p);
    log_attribute_maps.trace("Added attribute map {}\n", descriptor.name);

    return p;
}

template <typename Key_type>
template <typename Value_type>
inline auto
Property_map_collection<Key_type>::find(const Property_map_descriptor& descriptor) const
-> Property_map<Key_type, Value_type>*
{
    ZoneScoped;

    for (const auto& entry : m_entries)
    {
        bool match = entry.key == descriptor.name;
        if (match)
        {
            auto p = entry.value.get();
            return dynamic_cast<Property_map<Key_type, Value_type>*>(p);
        }
    }
    return nullptr;
}

template <typename Key_type>
template <typename Value_type>
inline auto
Property_map_collection<Key_type>::find_or_create(const Property_map_descriptor& descriptor)
-> Property_map<Key_type, Value_type>*
{
    ZoneScoped;

    for (const auto& entry : m_entries)
    {
        bool match = entry.key == descriptor.name;
        if (match)
        {
            auto p = entry.value.get();
            auto typed_p = dynamic_cast<Property_map<Key_type, Value_type>*>(p);
            if (typed_p != nullptr)
            {
                return typed_p;
            }
        }
    }

    // New entry
    auto p = new Property_map<Key_type, Value_type>(descriptor);
    m_entries.emplace_back(descriptor.name, p);
    log_attribute_maps.trace("Added attribute map {}\n", descriptor.name);
    return p;
}

template <typename Key_type>
inline void
Property_map_collection<Key_type>::interpolate(
    Property_map_collection<Key_type>&                   destination,
    std::vector<std::vector<std::pair<float, Key_type>>> key_new_to_olds)
{
    ZoneScoped;

    for (auto& entry : m_entries)
    {
        const std::string&           map_name   = entry.key;
        Property_map_base<Key_type>* src_map    = entry.value.get();
        const auto&                  descriptor = src_map->descriptor();
        if (descriptor.interpolation_mode == Interpolation_mode::none)
        {
            continue;
        }
        Property_map_base<Key_type>* destination_map = src_map->constructor(descriptor);

        log_interpolate.trace("\ninterpolating {}\n", src_map->descriptor().name);
        src_map->interpolate(destination_map, key_new_to_olds);

        destination.insert(destination_map);
    }
}

} // namespace erhe::geometry
