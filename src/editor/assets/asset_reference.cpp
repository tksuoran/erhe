#include "assets/asset_reference.hpp"

#include "assets/asset_manager.hpp"
#include "editor_log.hpp"

#include "erhe_item/item.hpp"

namespace editor {

namespace {

const std::shared_ptr<erhe::Item_base> s_empty_item{};

}

auto c_str(const Asset_resolve_state state) -> const char*
{
    switch (state) {
        case Asset_resolve_state::resolved: return "resolved";
        case Asset_resolve_state::failed:   return "failed";
        case Asset_resolve_state::unresolved:
        default:                            return "unresolved";
    }
}

Asset_reference::Asset_reference() = default;

Asset_reference::Asset_reference(const Asset_key& key, const std::string_view user_label)
    : m_key       {key}
    , m_user_label{user_label}
{
}

Asset_reference::Asset_reference(const Asset_reference& other)
    : m_key       {other.m_key}
    , m_resolved  {other.m_resolved}
    , m_manager   {other.m_manager}
    , m_state     {other.m_state}
    , m_user_label{other.m_user_label}
{
    if ((m_state == Asset_resolve_state::resolved) && (m_manager != nullptr)) {
        m_manager->register_user(this);
    }
}

Asset_reference& Asset_reference::operator=(const Asset_reference& other)
{
    if (this == &other) {
        return *this;
    }
    release_usership();
    m_key        = other.m_key;
    m_resolved   = other.m_resolved;
    m_manager    = other.m_manager;
    m_state      = other.m_state;
    m_user_label = other.m_user_label;
    if ((m_state == Asset_resolve_state::resolved) && (m_manager != nullptr)) {
        m_manager->register_user(this);
    }
    return *this;
}

Asset_reference::Asset_reference(Asset_reference&& other) noexcept
    : m_key       {std::move(other.m_key)}
    , m_resolved  {std::move(other.m_resolved)}
    , m_manager   {other.m_manager}
    , m_state     {other.m_state}
    , m_user_label{std::move(other.m_user_label)}
{
    // The manager tracks users by Asset_reference pointer: re-point the
    // registration from the moved-from object to this one.
    if ((m_state == Asset_resolve_state::resolved) && (m_manager != nullptr)) {
        m_manager->unregister_user(&other);
        m_manager->register_user(this);
    }
    other.m_manager = nullptr;
    other.m_state   = Asset_resolve_state::unresolved;
}

Asset_reference& Asset_reference::operator=(Asset_reference&& other) noexcept
{
    if (this == &other) {
        return *this;
    }
    release_usership();
    m_key        = std::move(other.m_key);
    m_resolved   = std::move(other.m_resolved);
    m_manager    = other.m_manager;
    m_state      = other.m_state;
    m_user_label = std::move(other.m_user_label);
    if ((m_state == Asset_resolve_state::resolved) && (m_manager != nullptr)) {
        m_manager->unregister_user(&other);
        m_manager->register_user(this);
    }
    other.m_manager = nullptr;
    other.m_state   = Asset_resolve_state::unresolved;
    return *this;
}

Asset_reference::~Asset_reference() noexcept
{
    release_usership();
}

void Asset_reference::release_usership()
{
    if ((m_state == Asset_resolve_state::resolved) && (m_manager != nullptr)) {
        m_manager->unregister_user(this);
    }
    m_resolved.reset();
    m_manager = nullptr;
    m_state   = Asset_resolve_state::unresolved;
}

void Asset_reference::on_manager_teardown()
{
    m_manager = nullptr;
}

auto Asset_reference::get_key() const -> const Asset_key&
{
    return m_key;
}

void Asset_reference::set_key(const Asset_key& key)
{
    release_usership();
    m_key = key;
}

auto Asset_reference::resolve(Asset_manager& manager) -> const std::shared_ptr<erhe::Item_base>&
{
    if (m_state == Asset_resolve_state::resolved) {
        return m_resolved;
    }
    if (m_state == Asset_resolve_state::failed) {
        return s_empty_item; // latched; reset_resolution() allows retry
    }
    std::string error;
    std::shared_ptr<erhe::Item_base> item = manager.acquire(m_key, error);
    if (!item) {
        if (m_key.scope == Asset_scope::file) {
            m_state = Asset_resolve_state::failed;
            log_asset->warn("Asset_reference '{}': resolve failed for {}: {}", m_user_label, m_key.describe(), error);
        }
        return s_empty_item;
    }
    m_resolved = item;
    m_manager  = &manager;
    m_state    = Asset_resolve_state::resolved;
    manager.register_user(this);
    // Self-heal upward: a key resolved via name learns the asset's uid, so
    // content migrates to uid addressing organically on each save.
    if (m_key.uid.empty() && !item->get_gltf_uid().empty()) {
        m_key.uid = item->get_gltf_uid();
    }
    return m_resolved;
}

void Asset_reference::adopt(Asset_manager& manager, const std::shared_ptr<erhe::Item_base>& item)
{
    release_usership();
    if (!item) {
        m_key = Asset_key{};
        return;
    }
    m_key      = manager.make_key(*item);
    m_resolved = item;
    m_manager  = &manager;
    m_state    = Asset_resolve_state::resolved;
    manager.register_user(this);
}

auto Asset_reference::get() const -> const std::shared_ptr<erhe::Item_base>&
{
    return m_resolved;
}

auto Asset_reference::get_state() const -> Asset_resolve_state
{
    return m_state;
}

void Asset_reference::reset_resolution()
{
    release_usership();
}

auto Asset_reference::get_user_label() const -> const std::string&
{
    return m_user_label;
}

void Asset_reference::set_user_label(const std::string_view user_label)
{
    m_user_label = user_label;
}

}
