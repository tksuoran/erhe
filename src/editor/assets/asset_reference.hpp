#pragma once

#include "assets/asset_key.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace erhe {
    class Item_base;
}

namespace editor {

class Asset_manager;

enum class Asset_resolve_state : int {
    unresolved = 0,
    resolved   = 1,
    failed     = 2
};

[[nodiscard]] auto c_str(Asset_resolve_state state) -> const char*;

// A held reference to an asset (asset-manager plan D2): the asset's key
// plus a pointer to the asset once resolved. Holding a RESOLVED
// Asset_reference is being a registered user of the asset - the special
// members maintain the manager's usership bookkeeping exactly (copy
// re-registers, move re-points the registration, destructor releases).
// Non-template so config codegen, MCP, and UI share one uniform type;
// typed access via get_as<T>().
//
// Resolution is lazy: constructing never loads; resolve() acquires through
// the manager. A file-scope load failure latches `failed` so a broken
// container is not re-hit every frame; scene_local / builtin misses do not
// latch (callers drive retry, e.g. a per-frame slot-resolve cadence).
class Asset_reference
{
public:
    Asset_reference();
    explicit Asset_reference(const Asset_key& key, std::string_view user_label = {});
    Asset_reference(const Asset_reference& other);
    Asset_reference& operator=(const Asset_reference& other);
    Asset_reference(Asset_reference&& other) noexcept;
    Asset_reference& operator=(Asset_reference&& other) noexcept;
    ~Asset_reference() noexcept;

    [[nodiscard]] auto get_key() const -> const Asset_key&;
    void set_key(const Asset_key& key);                        // clears the resolution cache, releases usership

    // Lazy acquire through the manager; registers this reference as a user
    // on success. On success with a name-resolved key, the key self-heals
    // upward: it learns the asset's uid so the reference re-persists with
    // uid addressing (plan D1).
    auto resolve(Asset_manager& manager) -> const std::shared_ptr<erhe::Item_base>&;

    // Adopt an already-loaded asset object (e.g. a drag-and-drop payload):
    // sets the key from the manager's make_key() and registers this
    // reference as a user of exactly that object. Deliberately NOT a
    // re-resolution through the registry - a name lookup could select a
    // different same-named object (e.g. a builtin shadowing a scene copy).
    // A null item clears the reference.
    void adopt(Asset_manager& manager, const std::shared_ptr<erhe::Item_base>& item);

    [[nodiscard]] auto get() const -> const std::shared_ptr<erhe::Item_base>&; // null while unresolved / failed

    template <typename T>
    [[nodiscard]] auto get_as() const -> std::shared_ptr<T>
    {
        return std::dynamic_pointer_cast<T>(m_resolved);
    }

    [[nodiscard]] auto get_state() const -> Asset_resolve_state;
    void reset_resolution();                                   // releases usership, clears the failed latch, allows retry

    // Human-readable holder description used when the manager names an
    // asset's users (unload refusals, MCP queries).
    [[nodiscard]] auto get_user_label() const -> const std::string&;
    void set_user_label(std::string_view user_label);

private:
    friend class Asset_manager;

    void release_usership();

    // Called by ~Asset_manager on every registered reference: teardown order
    // lets scene-owned holders (library entries, graph nodes) outlive the
    // manager, and their destructors must not call back into a destroyed
    // registry. The resolved object stays readable through get().
    void on_manager_teardown();

    Asset_key                        m_key;
    std::shared_ptr<erhe::Item_base> m_resolved;
    Asset_manager*                   m_manager{nullptr};
    Asset_resolve_state              m_state{Asset_resolve_state::unresolved};
    std::string                      m_user_label;
};

}
