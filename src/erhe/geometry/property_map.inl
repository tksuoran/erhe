#pragma once

#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/geometry/log.hpp"
#include "erhe/log/log_glm.hpp"

#include <gsl/assert>

#include <algorithm>
#include <unordered_map>

namespace erhe::geometry
{

template <typename Key_type, typename Value_type>
inline void
Property_map<Key_type, Value_type>::clear()
{
    ERHE_PROFILE_FUNCTION

    values.clear();
    present.clear();
}

template <typename Key_type, typename Value_type>
inline auto
Property_map<Key_type, Value_type>::empty() const -> bool
{
    return values.empty();
}

template <typename Key_type, typename Value_type>
inline auto
Property_map<Key_type, Value_type>::size() const -> size_t
{
    return values.size();
}

template <typename Key_type, typename Value_type>
inline void
Property_map<Key_type, Value_type>::trim(size_t size)
{
    values.resize(size);
    present.resize(size);
}

template <typename Key_type, typename Value_type>
inline void
Property_map<Key_type, Value_type>::remap_keys(const std::vector<Key_type>& key_new_to_old)
{
    const auto old_values  = values;
    const auto old_present = present;
    for (Key_type new_key = 0, end = static_cast<Key_type>(key_new_to_old.size()); new_key < end; ++new_key)
    {
        Key_type old_key = key_new_to_old[new_key];
        values [new_key] = old_values[old_key];
        present[new_key] = old_present[old_key];
    }
}

template <typename Key_type, typename Value_type>
inline void
Property_map<Key_type, Value_type>::put(Key_type key, Value_type value)
{
    ERHE_PROFILE_FUNCTION

    const size_t i = static_cast<size_t>(key);
    if (values.size() <= i)
    {
        values.resize(i + s_grow_size);
        present.resize(i + s_grow_size);
    }
    values[i] = value;
    present[i] = true;
}

template <typename Key_type, typename Value_type>
inline auto
Property_map<Key_type, Value_type>::get(Key_type key) const -> Value_type
{
    ERHE_PROFILE_FUNCTION

    const size_t i = static_cast<size_t>(key);
    if ((values.size() <= i) || !present[i])
    {
        ERHE_FATAL("Value not found\n");
    }
    return values[i];
}

template <typename Key_type, typename Value_type>
inline void
Property_map<Key_type, Value_type>::erase(Key_type key)
{
    ERHE_PROFILE_FUNCTION

    const size_t i = static_cast<size_t>(key);
    if (values.size() <= i)
    {
        values.resize(i + s_grow_size);
        present.resize(i + s_grow_size);
    }
    present[i] = false;
}

template <typename Key_type, typename Value_type>
inline auto
Property_map<Key_type, Value_type>::maybe_get(Key_type key, Value_type& out_value) const -> bool
{
    ERHE_PROFILE_FUNCTION

    const size_t i = static_cast<size_t>(key);
    if ((values.size() <= i) || !present[i])
    {
        return false;
    }
    out_value = values[i];
    return true;
}


template <typename Key_type, typename Value_type>
inline auto
Property_map<Key_type, Value_type>::has(Key_type key) const -> bool
{
    ERHE_PROFILE_FUNCTION

    const size_t i = static_cast<size_t>(key);
    if ((values.size() <= i) || !present[i])
    {
        return false;
    }
    return true;
}

template <typename Key_type, typename Value_type>
inline auto
Property_map<Key_type, Value_type>::constructor(
    const Property_map_descriptor& descriptor
) const -> Property_map_base<Key_type>*
{
    ERHE_PROFILE_FUNCTION

    Property_map<Key_type, Value_type>* instance = new Property_map<Key_type, Value_type>(descriptor);
    Property_map_base<Key_type>*        base_ptr = dynamic_cast<Property_map_base<Key_type>*>(instance);
    return base_ptr;
}

template <typename Key_type, typename Value_type>
inline void
Property_map<Key_type, Value_type>::interpolate(
    Property_map_base<Key_type>*                                destination_base,
    const std::vector<std::vector<std::pair<float, Key_type>>>& key_new_to_olds
) const
{
    ERHE_PROFILE_FUNCTION

    auto* destination = dynamic_cast<Property_map<Key_type, Value_type>*>(destination_base);
    ERHE_VERIFY(destination != nullptr);

    if (m_descriptor.interpolation_mode == Interpolation_mode::none)
    {
        log_interpolate.trace("\tinterpolation mode none, skipping this map\n");
        return;
    }

    for (size_t new_key = 0, end = key_new_to_olds.size(); new_key < end; ++new_key)
    {
        const std::vector<std::pair<float, Key_type>>& old_keys = key_new_to_olds[new_key];

        log_interpolate.trace("\tkey = {} from\n", new_key);
        float sum_weights{0.0f};
        for (auto j : old_keys)
        {
            const Key_type old_key = j.second;
            log_interpolate.trace("\told key {} weight {}\n", static_cast<unsigned int>(old_key), static_cast<float>(j.first));
            if (has(old_key))
            {
                sum_weights += j.first;
            }
        }

        if (sum_weights == 0.0f)
        {
            log_interpolate.trace("\tzero sum\n");
            continue;
        }

        Value_type new_value(0);
        for (auto j : old_keys)
        {
            const float    weight  = j.first;
            const Key_type old_key = j.second;

            if (has(old_key))
            {
                const Value_type old_value = get(old_key);
                log_interpolate.trace("\told value {} weight {}\n", old_value, (weight / sum_weights));
                new_value += static_cast<Value_type>((weight / sum_weights) * old_value);
            }
            else
            {
                log_interpolate.trace("\told value not found\n");
            }
        }

        // Special treatment for normal and other direction vectors
        if constexpr (std::is_same_v<Value_type, glm::vec3>)
        {
            if (m_descriptor.interpolation_mode == Interpolation_mode::normalized)
            {
                new_value = glm::normalize(new_value);
                log_interpolate.trace("\tnormalized\n", new_value);
            }
        }

        // Special treatment for tangent vectors (and other vec3 direction + float vec4s).
        if constexpr (std::is_same_v<Value_type, glm::vec4>)
        {
            if (m_descriptor.interpolation_mode == Interpolation_mode::normalized_vec3_float)
            {
                new_value = glm::vec4{
                    glm::normalize(
                        glm::vec3{new_value}
                    ),
                    new_value.z
                };
                log_interpolate.trace("\tnormalized vec3-float\n", new_value);
            }
        }

        log_interpolate.trace("\tvalue = {}\n", new_value);

        destination->put(static_cast<Key_type>(new_key), new_value);
    }
}

static inline float apply_transform(const float value, const glm::mat4 transform, const float w)
{
    return (transform * glm::vec4{value, 0.0f, 0.0f, w})[0];
}

static inline auto apply_transform(const glm::vec2 value, const glm::mat4 transform, const float w) -> glm::vec2
{
    return glm::vec2{transform * glm::vec4{value, 0.0f, w}};
}

static inline auto apply_transform(const glm::vec3 value, const glm::mat4 transform, const float w) -> glm::vec3
{
    return glm::vec3{transform * glm::vec4{value, w}};
}

static inline auto apply_transform(const glm::vec4 value, const glm::mat4 transform, const float w) -> glm::vec4
{
    static_cast<void>(w);
    return transform * glm::vec4{value};
}


template <typename T> struct transform_properties            { static const bool is_transformable = false; };
template <>           struct transform_properties<float>     { static const bool is_transformable = true;  };
template <>           struct transform_properties<glm::vec2> { static const bool is_transformable = true;  };
template <>           struct transform_properties<glm::vec3> { static const bool is_transformable = true;  };
template <>           struct transform_properties<glm::vec4> { static const bool is_transformable = true;  };

template <typename Key_type, typename Value_type>
inline void
Property_map<Key_type, Value_type>::import_from(
    Property_map_base<Key_type>* source_base
)
{
    ERHE_PROFILE_FUNCTION

    const auto* const source = dynamic_cast<Property_map<Key_type, Value_type>*>(source_base);
    ERHE_VERIFY(source != nullptr);

    ERHE_VERIFY(values.size() == present.size());
    ERHE_VERIFY(source->values.size() == source->present.size());

    values .reserve(values.size()  + source->values.size());
    present.reserve(present.size() + source->present.size());
    present.insert(present.end(), source->present.begin(), source->present.end());
    values.insert(values.end(), source->values.begin(), source->values.end());
}

template <typename Key_type, typename Value_type>
inline void
Property_map<Key_type, Value_type>::import_from(
    Property_map_base<Key_type>* source_base,
    const glm::mat4              transform
)
{
    ERHE_PROFILE_FUNCTION

    auto* source = dynamic_cast<Property_map<Key_type, Value_type>*>(source_base);
    ERHE_VERIFY(source != nullptr);

    ERHE_VERIFY(values.size() == present.size());
    ERHE_VERIFY(source->values.size() == source->present.size());

    const auto combined_values_size = values.size()  + source->values.size();
    const auto combined_present_size = present.size() + source->present.size();
    values .reserve(combined_values_size);
    present.reserve(combined_present_size);
    present.insert(present.end(), source->present.begin(), source->present.end());
    if constexpr(!transform_properties<Value_type>::is_transformable)
    {
        values.insert(values.end(), source->values.begin(), source->values.end());
    }
    else
    {
        switch (m_descriptor.transform_mode)
        {
            using enum Transform_mode;
            default:
            case none:
            {
                values.insert(values.end(), source->values.begin(), source->values.end());
                break;
            }

            case matrix:
            {
                for (size_t i = 0, end = source->values.size(); i < end; ++i)
                {
                    const Value_type source_value = source->values[i];
                    const Value_type result       = apply_transform(source_value, transform, 1.0f);
                    values.push_back(result);
                }
                break;
            }

            // TODO Use cofactor matrix for bivectors?
            case normalize_inverse_transpose_matrix:           
            {
                const glm::mat4 inverse_transpose_transform = glm::inverse(glm::transpose(transform));
                for (size_t i = 0, end = source->values.size(); i < end; ++i)
                {
                    const Value_type source_value = source->values[i];
                    const Value_type result       = glm::normalize(apply_transform(source_value, inverse_transpose_transform, 0.0f));
                    values.push_back(result);
                }
                break;
            }

            // TODO Use cofactor matrix for bivectors?
            case normalize_inverse_transpose_matrix_vec3_float:
            {
                if constexpr (std::is_same_v<Value_type, glm::vec4>)
                {
                    const glm::mat4 inverse_transpose_transform = glm::inverse(glm::transpose(transform));
                    for (size_t i = 0, end = source->values.size(); i < end; ++i)
                    {
                        const Value_type source_value     = source->values[i];
                        const glm::vec3  transformed_vec3 = glm::normalize(apply_transform(glm::vec3{source_value}, inverse_transpose_transform, 0.0f));
                        const Value_type result           = glm::vec4{transformed_vec3, source_value.w};
                        values.push_back(result);
                    }
                }
                break;
            }
        }
    }
}

} // namespace erhe::geometry
