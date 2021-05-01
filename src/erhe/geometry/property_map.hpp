#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <optional>
#include <typeinfo>
#include <vector>

namespace erhe::geometry
{

enum class Transform_mode : unsigned int
{
    none = 0, // texture coordinates, colors, ...
    matrix,
    normalize_inverse_transpose_matrix,
    normalize_inverse_transpose_matrix_vec3_float,
};

enum class Interpolation_mode : unsigned int
{
    none = 0,
    linear,                 // transform with matrix - position
    normalized,             // normalize(tranform with inverse transpose of matrix) = normal, tagent, bitangent
    normalized_vec3_float,  // normalize(tranform with inverse transpose of matrix) = normal, tagent, bitangent
};

struct Property_map_descriptor
{
    const char*        name;
    Transform_mode     transform_mode;
    Interpolation_mode interpolation_mode;
};

template <typename Key_type>
class Property_map_base
{
public:
    virtual ~Property_map_base() = default;

    virtual auto constructor(const Property_map_descriptor& descriptor) const
    -> Property_map_base* = 0;

    virtual auto descriptor() const -> Property_map_descriptor = 0;

    virtual void clear() = 0;

    virtual auto empty() const
    -> bool = 0;

    virtual auto size() const
    -> size_t = 0;

    virtual auto has(Key_type key) const
    -> bool = 0;

    virtual void interpolate(Property_map_base<Key_type>*                         destination,
                             std::vector<std::vector<std::pair<float, Key_type>>> key_new_to_olds) const = 0;

protected:
    Property_map_base() = default;
};

template <typename Key_type, typename Value_type>
class Property_map
    : public Property_map_base<Key_type>
{
public:
    Property_map(const Property_map_descriptor& descriptor)
        : m_descriptor(descriptor)
    {
    };

    auto descriptor() const -> Property_map_descriptor final { return m_descriptor; }

    void put(Key_type key, Value_type value);

    void erase(Key_type key);

    auto get(Key_type key) const
    -> Value_type;

    auto maybe_get(Key_type key, Value_type& out_value) const
    -> bool;

    auto has(Key_type key) const
    -> bool final;

    void clear() final;

    auto empty() const
    -> bool final;

    auto size() const
    -> size_t final;

    void interpolate(Property_map_base<Key_type>*                         destination,
                     std::vector<std::vector<std::pair<float, Key_type>>> key_new_to_olds) const final;

    auto constructor(const Property_map_descriptor& descriptor) const
    -> Property_map_base<Key_type>* final;

private:
    static constexpr const size_t s_grow_size = 4096;

    Property_map_descriptor m_descriptor;
    std::vector<Value_type> m_values;
    std::vector<bool>       m_present; // Yes, I know vector<bool> has limitations
};

} // namespace erhe::geometry

#include "property_map.inl"
