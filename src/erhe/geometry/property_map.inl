#pragma once

#include "erhe/geometry/log.hpp"
#include "Tracy.hpp"
#include <gsl/assert>
#include <algorithm>
#include <unordered_map>

namespace erhe::geometry
{

template <typename Key_type, typename Value_type>
inline void
Property_map<Key_type, Value_type>::clear()
{
    ZoneScoped;

    m_values.clear();
    m_present.clear();
}

template <typename Key_type, typename Value_type>
inline auto
Property_map<Key_type, Value_type>::empty() const
-> bool
{
    return m_values.empty();
}

template <typename Key_type, typename Value_type>
inline auto
Property_map<Key_type, Value_type>::size() const
-> size_t
{
    return m_values.size();
}

template <typename Key_type, typename Value_type>
inline void
Property_map<Key_type, Value_type>::put(Key_type key, Value_type value)
{
    ZoneScoped;

    size_t i = static_cast<size_t>(key);
    if (m_values.size() <= i)
    {
        m_values.resize(i + s_grow_size);
        m_present.resize(i + s_grow_size);
    }
    m_values[i] = value;
    m_present[i] = true;
}

template <typename Key_type, typename Value_type>
inline auto
Property_map<Key_type, Value_type>::get(Key_type key) const
-> Value_type
{
    ZoneScoped;

    size_t i = static_cast<size_t>(key);
    if ((m_values.size() <= i) || !m_present[i])
    {
        FATAL("Value not found");
    }
    return m_values[i];
}

template <typename Key_type, typename Value_type>
inline void
Property_map<Key_type, Value_type>::erase(Key_type key)
{
    ZoneScoped;

    size_t i = static_cast<size_t>(key);
    if (m_values.size() <= i)
    {
        m_values.resize(i + s_grow_size);
        m_present.resize(i + s_grow_size);
    }
    m_present[i] = false;
}

template <typename Key_type, typename Value_type>
inline auto
Property_map<Key_type, Value_type>::maybe_get(Key_type key, Value_type& out_value) const
-> bool
{
    ZoneScoped;

    size_t i = static_cast<size_t>(key);
    if ((m_values.size() <= i) || !m_present[i])
    {
        return false;
    }
    out_value = m_values[i];
    return true;
}


template <typename Key_type, typename Value_type>
inline auto
Property_map<Key_type, Value_type>::has(Key_type key) const
-> bool
{
    ZoneScoped;

    size_t i = static_cast<size_t>(key);
    if ((m_values.size() <= i) || !m_present[i])
    {
        return false;
    }
    return true;
}

template <typename Key_type, typename Value_type>
inline auto
Property_map<Key_type, Value_type>::constructor(const Property_map_descriptor& descriptor) const
-> Property_map_base<Key_type>*
{
    ZoneScoped;

    Property_map<Key_type, Value_type>* instance = new Property_map<Key_type, Value_type>(descriptor);
    Property_map_base<Key_type>*        base_ptr = dynamic_cast<Property_map_base<Key_type>*>(instance);
    return base_ptr;
}

template <typename Key_type, typename Value_type>
inline void
Property_map<Key_type, Value_type>::interpolate(
    Property_map_base<Key_type>*                         destination_base,
    std::vector<std::vector<std::pair<float, Key_type>>> key_new_to_olds) const
{
    ZoneScoped;

    auto* destination = dynamic_cast<Property_map<Key_type, Value_type>*>(destination_base);
    VERIFY(destination != nullptr);

    if (m_descriptor.interpolation_mode == Interpolation_mode::none)
    {
        log_interpolate.trace("\tinterpolation mode none, skipping this map\n");
        return;
    }

    for (size_t new_key = 0, end = key_new_to_olds.size(); new_key < end; ++new_key)
    {
        const std::vector<std::pair<float, Key_type>>& old_keys = key_new_to_olds[new_key];

        //log_interpolate.trace("\tkey = {} from\n", new_key);
        float sum_weights = 0.0f;
        for (auto j : old_keys)
        {
            Key_type old_key = j.second;
            //log_interpolate.trace("\told key {} weight {}\n", static_cast<unsigned int>(old_key), static_cast<float>(j.first));
            if (has(old_key))
            {
                sum_weights += j.first;
            }
        }

        if (sum_weights == 0.0f)
        {
            //log_interpolate.trace("\tzero sum\n");
            continue;
        }

        Value_type new_value(0);
        for (auto j : old_keys)
        {
            float      weight  = j.first;
            Key_type   old_key = j.second;
            Value_type old_value;

            if (has(old_key))
            {
                old_value  = get(old_key);
                //log_interpolate.trace("\told value {} weight {}\n", old_value, (weight / sum_weights));
                new_value += static_cast<Value_type>((weight / sum_weights) * old_value);
            }
            else
            {
                //log_interpolate.trace("\told value not found\n");
            }
        }

        // Special treatment for normal and other direction vectors
        if constexpr(std::is_same_v<Value_type, glm::vec3>)
        {
            if (m_descriptor.interpolation_mode == Interpolation_mode::normalized)
            {
                new_value = glm::normalize(new_value);
                //log_interpolate.trace("\tnormalized\n", new_value);
            }
        }

        // Special treatment for tangent vectors (and other vec3 direction + float vec4s).
        if constexpr(std::is_same_v<Value_type, glm::vec4>)
        {
            if (m_descriptor.interpolation_mode == Interpolation_mode::normalized_vec3_float)
            {
                new_value = glm::vec4(glm::normalize(glm::vec3(new_value)), new_value.z);
                //log_interpolate.trace("\tnormalized vec3-float\n", new_value);
            }
        }

        //log_interpolate.trace("\tvalue = {}\n", new_value);

        destination->put(static_cast<Key_type>(new_key), new_value);
    }
}

} // namespace erhe::geometry
