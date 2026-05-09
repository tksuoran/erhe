#include "erhe_scene_renderer/shader_variant_cache.hpp"

#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_profile/profile.hpp"

#include <algorithm>

namespace erhe::scene_renderer {

namespace {

[[nodiscard]] auto canonicalize_defines(
    std::vector<std::pair<std::string, std::string>> defines
) -> std::vector<std::pair<std::string, std::string>>
{
    // Sort by name first so two keys built with the same logical defines
    // in different orders compare equal.
    std::sort(
        defines.begin(),
        defines.end(),
        [](const std::pair<std::string, std::string>& a, const std::pair<std::string, std::string>& b) {
            return a.first < b.first;
        }
    );
    // Dedup adjacent duplicates by name. Two entries with the same name
    // and different values is a caller bug -- log it and keep the first.
    auto out = defines.begin();
    auto in  = defines.begin();
    while (in != defines.end()) {
        if (out != in) {
            *out = std::move(*in);
        }
        auto next = in;
        ++next;
        while ((next != defines.end()) && (next->first == out->first)) {
            if (next->second != out->second) {
                log_program_interface->warn(
                    "Shader_variant_key: duplicate define '{}' with conflicting values '{}' vs '{}'; keeping the first",
                    out->first,
                    out->second,
                    next->second
                );
            }
            ++next;
        }
        in = next;
        ++out;
    }
    defines.erase(out, defines.end());
    return defines;
}

[[nodiscard]] auto fnv1a64(const void* data, std::size_t size, std::uint64_t seed) noexcept -> std::uint64_t
{
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t           hash  = seed;
    const auto*             bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= prime;
    }
    return hash;
}

[[nodiscard]] auto hash_string(std::uint64_t seed, const std::string& s) noexcept -> std::uint64_t
{
    return fnv1a64(s.data(), s.size(), seed);
}

} // anonymous namespace

Shader_variant_key::Shader_variant_key(
    std::string                                      in_name,
    std::vector<std::pair<std::string, std::string>> in_defines,
    uint32_t                                         in_multiview_view_count,
    bool                                             in_no_vertex_input,
    bool                                             in_dump_interface
)
    : name                {std::move(in_name)}
    , defines             {canonicalize_defines(std::move(in_defines))}
    , multiview_view_count{in_multiview_view_count}
    , no_vertex_input     {in_no_vertex_input}
    , dump_interface      {in_dump_interface}
{
}

auto Shader_variant_key_hash::operator()(const Shader_variant_key& key) const noexcept -> std::size_t
{
    constexpr std::uint64_t fnv_offset_basis = 14695981039346656037ull;

    std::uint64_t hash = hash_string(fnv_offset_basis, key.name);
    for (const std::pair<std::string, std::string>& define : key.defines) {
        hash = hash_string(hash, define.first);
        hash = hash_string(hash, define.second);
    }
    hash = fnv1a64(&key.multiview_view_count, sizeof(key.multiview_view_count), hash);
    const unsigned char flags =
        (key.no_vertex_input ? 0x1u : 0u) |
        (key.dump_interface  ? 0x2u : 0u);
    hash = fnv1a64(&flags, sizeof(flags), hash);
    return static_cast<std::size_t>(hash);
}

Shader_variant_cache::Shader_variant_cache(
    erhe::graphics::Device& graphics_device,
    Program_interface&      program_interface
)
    : m_graphics_device  {graphics_device}
    , m_program_interface{program_interface}
{
}

Shader_variant_cache::~Shader_variant_cache() noexcept
{
    erhe::graphics::Shader_monitor& monitor = m_graphics_device.get_shader_monitor();
    for (auto& [key, entry] : m_entries) {
        monitor.remove(*entry);
    }
}

auto Shader_variant_cache::compile_locked(const Shader_variant_key& key) -> erhe::graphics::Reloadable_shader_stages*
{
    const auto it = m_entries.find(key);
    if (it != m_entries.end()) {
        return it->second.get();
    }

    erhe::graphics::Shader_stages_create_info create_info{
        .name            = key.name,
        .defines         = key.defines,
        .no_vertex_input = key.no_vertex_input,
        .dump_interface  = key.dump_interface
    };
    if (key.multiview_view_count >= 2) {
        create_info.enable_multiview(key.multiview_view_count);
    }

    erhe::graphics::Shader_stages_prototype prototype = m_program_interface.make_prototype(std::move(create_info));
    prototype.compile_shaders();
    const bool linked = prototype.link_program();

    auto entry = std::make_unique<erhe::graphics::Reloadable_shader_stages>(m_graphics_device, std::move(prototype));
    erhe::graphics::Reloadable_shader_stages* entry_ptr = entry.get();
    const bool initially_valid = linked && entry_ptr->shader_stages.is_valid();

    if (!initially_valid) {
        log_program_interface->error("Shader variant compile/link failed: name='{}', defines={}", key.name, key.defines.size());
    }

    // Register for hot reload unconditionally so a future edit to the
    // .vert / .frag source can recover a variant whose initial compile
    // failed. Stable pointer guaranteed by the unique_ptr; clear()
    // detaches before freeing.
    m_graphics_device.get_shader_monitor().add(*entry_ptr);

    m_entries.emplace(key, std::move(entry));
    return entry_ptr;
}

auto Shader_variant_cache::get_or_compile(const Shader_variant_key& key) -> const erhe::graphics::Shader_stages*
{
    ERHE_PROFILE_FUNCTION();

    std::lock_guard<std::mutex> lock{m_mutex};
    erhe::graphics::Reloadable_shader_stages* entry = compile_locked(key);
    if ((entry != nullptr) && entry->shader_stages.is_valid()) {
        return &entry->shader_stages;
    }
    return nullptr;
}

auto Shader_variant_cache::get_or_compile(
    const Shader_variant_key& key,
    const Shader_variant_key& fallback_key
) -> const erhe::graphics::Shader_stages*
{
    ERHE_PROFILE_FUNCTION();

    std::lock_guard<std::mutex> lock{m_mutex};
    erhe::graphics::Reloadable_shader_stages* entry = compile_locked(key);
    if ((entry != nullptr) && entry->shader_stages.is_valid()) {
        return &entry->shader_stages;
    }

    // Requested variant compile failed; fall back to the supplied
    // fallback key, lazy-compiling it if needed. If the fallback also
    // fails, return nullptr.
    if (fallback_key == key) {
        return nullptr;
    }
    erhe::graphics::Reloadable_shader_stages* fallback_entry = compile_locked(fallback_key);
    if ((fallback_entry != nullptr) && fallback_entry->shader_stages.is_valid()) {
        return &fallback_entry->shader_stages;
    }
    return nullptr;
}

void Shader_variant_cache::clear()
{
    std::lock_guard<std::mutex> lock{m_mutex};
    erhe::graphics::Shader_monitor& monitor = m_graphics_device.get_shader_monitor();
    for (auto& [key, entry] : m_entries) {
        monitor.remove(*entry);
    }
    m_entries.clear();
}

auto Shader_variant_cache::size() const -> std::size_t
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_entries.size();
}

} // namespace erhe::scene_renderer
