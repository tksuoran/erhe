#pragma once

//#include "erhe/toolkit/profile.hpp"

#ifndef ERHE_PROFILE_FUNCTION
#   define ERHE_PROFILE_FUNCTION()
#   define ERHE_PROFILE_FUNCTION_DUMMY
#endif
#ifndef ERHE_VERIFY
#   define ERHE_VERIFY(condition) assert(condition)
#   define ERHE_VERIFY_DUMMY
#endif
#ifndef ERHE_FATAL
#   define ERHE_FATAL(format, ...) assert(false)
#   define ERHE_FATAL_DUMMY
#endif
#ifndef SPDLOG_LOGGER_TRACE
#   define SPDLOG_LOGGER_TRACE(logger, ...) (void)0
#   define SPDLOG_LOGGER_TRACE_DUMMY
#endif

namespace erhe::geometry
{

template <typename Key_type>
inline void
Property_map_collection<Key_type>::clear()
{
    ERHE_PROFILE_FUNCTION();

    m_entries.clear();
}

template <typename Key_type>
inline auto
Property_map_collection<Key_type>::size() const -> size_t
{
    return m_entries.size();
}

template <typename Key_type>
inline void
Property_map_collection<Key_type>::insert(Property_map_base<Key_type>* map)
{
    ERHE_PROFILE_FUNCTION();

    //// for (const auto& entry : m_entries)
    //// {
    ////     ERHE_VERIFY(entry.key != map->descriptor().name);
    //// }
    m_entries.emplace_back(map->descriptor().name, map);
    SPDLOG_LOGGER_TRACE(log_attribute_maps, "Added attribute map {}", map->descriptor().name);
}

template <typename Key_type>
inline void
Property_map_collection<Key_type>::remove(const std::string& name)
{
    ERHE_PROFILE_FUNCTION();

    m_entries.erase(
        std::remove_if(
            m_entries.begin(),
            m_entries.end(),
            [name](const Entry& entry) {
                return entry.key == name;
            }
        ),
        m_entries.end()
    );
}

template <typename Key_type>
template <typename Value_type>
inline auto
Property_map_collection<Key_type>::contains(const std::string& name) const
    -> bool
{
    ERHE_PROFILE_FUNCTION();

    for (const auto& entry : m_entries) {
        const bool match = entry.key == name;
        if (match) {
            return true;
        }
    }

    return false;
}

template <typename Key_type>
inline auto
Property_map_collection<Key_type>::find_base(
    const Property_map_descriptor& descriptor
) const -> Property_map_base<Key_type>*
{
    ERHE_PROFILE_FUNCTION();

    for (const auto& entry : m_entries) {
        if (entry.key == descriptor.name) {
            return entry.value.get();
        }
    }
    return nullptr;
}

template <typename Key_type>
template <typename Value_type>
inline auto
Property_map_collection<Key_type>::create(
    const Property_map_descriptor& descriptor
) -> Property_map<Key_type, Value_type>*
{
    ERHE_PROFILE_FUNCTION();

    //// for (const auto& entry : m_entries)
    //// {
    ////     ERHE_VERIFY(entry.key != descriptor.name);
    //// }

    const auto p = new Property_map<Key_type, Value_type>(descriptor);
    m_entries.emplace_back(descriptor.name, p);
    SPDLOG_LOGGER_TRACE(log_attribute_maps, "Added attribute map {}", descriptor.name);

    return p;
}

template <typename Key_type>
template <typename Value_type>
inline auto
Property_map_collection<Key_type>::find(
    const Property_map_descriptor& descriptor
) const -> Property_map<Key_type, Value_type>*
{
    ERHE_PROFILE_FUNCTION();

    for (const auto& entry : m_entries) {
        if (entry.key == descriptor.name) {
            const auto p       = entry.value.get();
            const auto typed_p = dynamic_cast<Property_map<Key_type, Value_type>*>(p);
            if (typed_p != nullptr) {
                return typed_p;
            }
        }
    }
    return nullptr;
}

template <typename Key_type>
template <typename Value_type>
inline auto
Property_map_collection<Key_type>::find_or_create(
    const Property_map_descriptor& descriptor
) -> Property_map<Key_type, Value_type>*
{
    ERHE_PROFILE_FUNCTION();

    for (const auto& entry : m_entries) {
        if (entry.key == descriptor.name) {
            const auto p       = entry.value.get();
            const auto typed_p = dynamic_cast<Property_map<Key_type, Value_type>*>(p);
            if (typed_p != nullptr) {
                return typed_p;
            }
        }
    }

    // New entry
    const auto p = new Property_map<Key_type, Value_type>(descriptor);
    m_entries.emplace_back(descriptor.name, p);
    SPDLOG_LOGGER_TRACE(log_attribute_maps, "Added attribute map {}", descriptor.name);
    return p;
}

template <typename Key_type>
inline void
Property_map_collection<Key_type>::trim(size_t size)
{
    ERHE_PROFILE_FUNCTION();

    for (auto& entry : m_entries) {
        entry.value->trim(size);
    }
}

template <typename Key_type>
inline void
Property_map_collection<Key_type>::remap_keys(const std::vector<Key_type>& key_new_to_old)
{
    ERHE_PROFILE_FUNCTION();

    for (auto& entry : m_entries){
        entry.value->remap_keys(key_new_to_old);
    }
}

template <typename Key_type>
inline void
Property_map_collection<Key_type>::interpolate(
    Property_map_collection<Key_type>&                          destination,
    const std::vector<std::vector<std::pair<float, Key_type>>>& key_new_to_olds)
{
    ERHE_PROFILE_FUNCTION();

    for (auto& entry : m_entries) {
        Property_map_base<Key_type>* src_map    = entry.value.get();
        const auto&                  descriptor = src_map->descriptor();
        if (descriptor.interpolation_mode == Interpolation_mode::none) {
            continue;
        }
        Property_map_base<Key_type>* destination_map = src_map->constructor(descriptor);

        SPDLOG_LOGGER_TRACE(log_interpolate, "interpolating {}", src_map->descriptor().name);
        src_map->interpolate(destination_map, key_new_to_olds);

        destination.insert(destination_map);
    }
}

template <typename Key_type>
inline void
Property_map_collection<Key_type>::merge_to(
    Property_map_collection<Key_type>& destination,
    const glm::mat4                    transform
)
{
    ERHE_PROFILE_FUNCTION();

    for (auto& entry : m_entries) {
        Property_map_base<Key_type>* this_map   = entry.value.get();
        const auto&                  descriptor = this_map->descriptor();
        Property_map_base<Key_type>* target_map = destination.find_base(descriptor);
        if (target_map == nullptr) {
            target_map = this_map->constructor(descriptor);
            destination.insert(target_map);
        }
        target_map->import_from(this_map, transform);
    }
}

template <typename Key_type>
inline auto
Property_map_collection<Key_type>::clone() -> Property_map_collection<Key_type>
{
    Property_map_collection<Key_type> result;
    for (auto& entry : m_entries) {
        Property_map_base<Key_type>* this_map   = entry.value.get();
        const auto&                  descriptor = this_map->descriptor();
        Property_map_base<Key_type>* target_map = this_map->constructor(descriptor);
        result.insert(target_map);
        target_map->import_from(this_map);
    }
    return result;
}

template <typename Key_type>
inline void
Property_map_collection<Key_type>::transform(
    const glm::mat4 matrix
)
{
    Property_map_collection<Key_type> result;
    for (auto& entry : m_entries) {
        Property_map_base<Key_type>* property_map = entry.value.get();
        property_map->transform(matrix);
    }
}

template <typename Key_type>
inline auto
Property_map_collection<Key_type>::clone_with_transform(
    const glm::mat4 transform
) -> Property_map_collection<Key_type>
{
    Property_map_collection<Key_type> result;
    for (auto& entry : m_entries) {
        Property_map_base<Key_type>* this_map   = entry.value.get();
        const auto&                  descriptor = this_map->descriptor();
        Property_map_base<Key_type>* target_map = this_map->constructor(descriptor);
        result.insert(target_map);
        target_map->import_from(this_map, transform);
    }
    return result;
}

} // namespace erhe::geometry

#ifdef ERHE_PROFILE_FUNCTION_DUMMY
#   undef ERHE_PROFILE_FUNCTION
#   undef ERHE_PROFILE_FUNCTION_DUMMY
#endif
#ifdef ERHE_VERIFY_DUMMY
#   undef ERHE_VERIFY
#   undef ERHE_VERIFY_DUMMY
#endif
#ifdef ERHE_FATAL_DUMMY
#   undef ERHE_FATAL
#   undef ERHE_FATAL_DUMMY
#endif
#ifdef SPDLOG_LOGGER_TRACE_DUMMY
#   undef SPDLOG_LOGGER_TRACE
#   undef SPDLOG_LOGGER_TRACE_DUMMY
#endif
