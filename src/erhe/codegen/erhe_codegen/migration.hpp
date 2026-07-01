#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace erhe::codegen {

// Set by run_migrations() whenever a deserialized struct's stored _version is older than
// the code's current_version. Because every generated deserialize() calls run_migrations()
// for its own struct, this becomes true if any struct anywhere in the tree was written by
// an older schema version. load_config() clears it before parsing and reads it afterwards
// to decide whether to rewrite (upgrade) the file on disk in the current format.
//
// thread_local so concurrent loads on different threads (parallel part init) stay
// independent; a single load is otherwise sequential on its own thread.
inline thread_local bool t_deserialized_old_version = false;

inline void clear_deserialized_old_version_flag()
{
    t_deserialized_old_version = false;
}

[[nodiscard]] inline auto deserialized_old_version() -> bool
{
    return t_deserialized_old_version;
}

template <typename T>
using Migration_callback = std::function<auto(T& value, uint32_t old_version, uint32_t new_version) -> bool>;

struct Migration_registry
{
    std::mutex                                                 mutex;
    std::unordered_map<std::type_index, std::vector<std::any>> callbacks;

    static auto instance() -> Migration_registry&
    {
        static Migration_registry reg;
        return reg;
    }
};

template <typename T>
void register_migration(Migration_callback<T> callback)
{
    auto& reg = Migration_registry::instance();
    std::lock_guard lock{reg.mutex};
    reg.callbacks[std::type_index(typeid(T))].push_back(std::move(callback));
}

template <typename T>
auto run_migrations(T& value, uint32_t old_version, uint32_t new_version) -> bool
{
    if (old_version >= new_version) {
        return true;
    }

    // An older document version was seen. Record it regardless of whether any migration
    // callback is registered -- the format change alone warrants rewriting the file so it
    // is upgraded to the current format on load.
    t_deserialized_old_version = true;

    auto& reg = Migration_registry::instance();

    // Copy callbacks under lock, execute outside lock to avoid deadlock
    std::vector<std::any> cbs;
    {
        std::lock_guard lock{reg.mutex};
        auto it = reg.callbacks.find(std::type_index(typeid(T)));
        if (it == reg.callbacks.end()) {
            return true;
        }
        cbs = it->second;
    }

    for (const auto& cb_any : cbs) {
        const auto& cb = std::any_cast<const Migration_callback<T>&>(cb_any);
        if (!cb(value, old_version, new_version)) {
            return false;
        }
    }
    return true;
}

} // namespace erhe::codegen
