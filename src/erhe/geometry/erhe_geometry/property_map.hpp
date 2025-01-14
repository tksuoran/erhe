#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <typeinfo>
#include <vector>

namespace erhe::geometry {

// TODO Use cofactor matrix for bivectors?
enum class Transform_mode : unsigned int {
    none = 0,                              // texture coordinates, colors, ...
    mat_mul_vec3_one,                      // position vectors
    mat_mul_vec3_zero,                     // transform using vec4(v, 0.0)
    normalize_mat_mul_vec3_zero,           // transform using vec4(v, 0.0), normalize
    normalize_mal_mul_vec3_zero_and_float, // tangent and bitangent with extra float that is not to be transformed
    cofactor_mat_mul_vec3_zero,            // normal - transform using cofactor
    normalize_cofactor_mat_mul_vec3_zero   // normal - transform using cofactor, normalize
};

enum class Interpolation_mode : unsigned int {
    none = 0,
    linear,                // transform with matrix - position
    normalized,            // normalize after transform (normal, tagent, bitangent)
    normalized_vec3_float, // normalize after tarnsform (tangent, bitangent), extra float not normalized
};

class Property_map_descriptor
{
public:
    const char*        name;
    Transform_mode     transform_mode;
    Interpolation_mode interpolation_mode;
};

template <typename Key_type>
class Property_map_base
{
public:
    virtual ~Property_map_base() noexcept = default;

    virtual auto constructor(const Property_map_descriptor& descriptor) const -> Property_map_base* = 0;

    virtual auto descriptor() const -> Property_map_descriptor = 0;
    virtual void clear     () = 0;
    virtual auto empty     () const -> bool = 0;
    virtual auto size      () const -> std::size_t = 0;
    virtual auto has       (Key_type key) const -> bool = 0;
    virtual void trim      (std::size_t size) = 0;
    virtual void remap_keys(const std::vector<Key_type>& key_old_to_new) = 0;

    virtual void interpolate(
        Property_map_base<Key_type>*                                destination,
        const std::vector<std::vector<std::pair<float, Key_type>>>& key_new_to_olds
    ) const = 0;

    virtual void transform  (const glm::mat4 matrix) = 0;
    virtual void import_from(Property_map_base<Key_type>* source) = 0;
    virtual void import_from(Property_map_base<Key_type>* source, const glm::mat4 transform) = 0;

protected:
    Property_map_base() = default;
};

template <typename Key_type, typename Value_type>
class Property_map : public Property_map_base<Key_type>
{
public:
    explicit Property_map(const Property_map_descriptor& descriptor)
        : m_descriptor{descriptor}
    {
    };

    auto descriptor() const -> Property_map_descriptor override
    {
        return m_descriptor;
    }

    void put       (Key_type key, Value_type value);
    void erase     (Key_type key);
    auto get       (Key_type key) const -> Value_type;
    auto maybe_get (Key_type key, Value_type& out_value) const -> bool;
    auto has       (Key_type key) const -> bool final;
    void clear     () final;
    auto empty     () const -> bool final;
    auto size      () const -> std::size_t final;
    void trim      (std::size_t size) final;
    void remap_keys(const std::vector<Key_type>& key_new_to_old) final;

    void interpolate(
        Property_map_base<Key_type>*                                destination,
        const std::vector<std::vector<std::pair<float, Key_type>>>& key_new_to_olds
    ) const final;

    void transform  (const glm::mat4 matrix) final;
    void import_from(Property_map_base<Key_type>* source) final;
    void import_from(Property_map_base<Key_type>* source, const glm::mat4 transform) final;
    auto constructor(const Property_map_descriptor& descriptor) const -> Property_map_base<Key_type>* final;

    static constexpr std::size_t s_grow_size = 4096;

    std::vector<Value_type> values;
    std::vector<bool>       present; // Yes, I know vector<bool> has limitations

private:
    Property_map_descriptor m_descriptor;
};

} // namespace erhe::geometry

#include "property_map.inl"
