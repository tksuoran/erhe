#include "erhe_scene_renderer/shader_variant_cache.hpp"

#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_scene_renderer/variant_handle.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::scene_renderer {

namespace {

[[nodiscard]] auto canonicalize_defines(
    std::vector<std::pair<std::string, std::string>> defines
) -> std::vector<std::pair<std::string, std::string>>
{
    // Sort by the full pair so two keys built with the same logical
    // defines in different orders compare equal. Sorting by name alone
    // would coalesce empty-name "comment" entries (used by
    // make_standard_variant_defines to render `// ERHE_FOO disabled`
    // markers in the shader source) and trigger spurious "conflicting
    // values" warnings when several axes are disabled.
    std::sort(defines.begin(), defines.end());
    // Dedup adjacent entries:
    //   - Identical (name, value): drop the duplicate silently.
    //   - Same non-empty name, different value: caller bug -- warn and keep
    //     the first.
    //   - Empty name with different values: distinct comment lines, keep both.
    auto out = defines.begin();
    auto in  = defines.begin();
    while (in != defines.end()) {
        if (out != in) {
            *out = std::move(*in);
        }
        auto next = in;
        ++next;
        while (next != defines.end()) {
            if (*next == *out) {
                ++next;
                continue;
            }
            if (!out->first.empty() && (next->first == out->first)) {
                log_program_interface->warn(
                    "Shader_variant_key: duplicate define '{}' with conflicting values '{}' vs '{}'; keeping the first",
                    out->first,
                    out->second,
                    next->second
                );
                ++next;
                continue;
            }
            break;
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

Shader_name_registry& Shader_name_registry::instance()
{
    static Shader_name_registry r;
    return r;
}

void Shader_name_registry::register_name(const std::string& name)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_names[name] = true;
}

auto Shader_name_registry::is_registered(const std::string& name) const -> bool
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_names.find(name) != m_names.end();
}

namespace {

// Canonical set of shader source names accepted by Shader_variant_key.
// Update this list when adding a new .vert/.frag pair to res/shaders or
// res/editor/shaders; a typo at the construction site is caught here
// before it can silently fall through to a vkCreateShaderModule null
// return at first-use time.
const std::unordered_map<std::string, bool>& canonical_shader_names()
{
    static const std::unordered_map<std::string, bool> names = {
        {"error",                    true},
        {"brdf_slice",               true},
        {"brush",                    true},
        {"standard",                 true},
        {"textured",                 true},
        {"sky",                      true},
        {"grid",                     true},
        {"wide_lines",               true},
        {"points",                   true},
        {"id",                       true},
        {"tool",                     true},
        {"visualize_depth",          true},
        {"shadow_debug",             true},
    };
    return names;
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
    // Validate the name against the canonical registry. Unknown
    // names trip ERHE_VERIFY at construction so typos surface here
    // instead of falling through to a vkCreateShaderModule null
    // return.
    const auto& canonical = canonical_shader_names();
    if (canonical.find(name) == canonical.end()) {
        ERHE_VERIFY(false && "Shader_variant_key constructed with unknown shader name; add to canonical_shader_names() if intentional");
    }
    Shader_name_registry::instance().register_name(name);
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

    {
        std::string defines_summary;
        for (const std::pair<std::string, std::string>& define : key.defines) {
            if (!defines_summary.empty()) {
                defines_summary += ' ';
            }
            if (define.first.empty()) {
                // Comment marker emitted as `// <value>` in the shader
                // preamble; show it bare so the disabled axis is visible.
                defines_summary += define.second;
            } else {
                defines_summary += define.first;
                defines_summary += '=';
                defines_summary += define.second;
            }
        }
        log_program_interface->debug(
            "Compiling new shader variant: name='{}', multiview={}, no_vertex_input={}, dump_interface={}, defines=[{}]",
            key.name,
            key.multiview_view_count,
            key.no_vertex_input,
            key.dump_interface,
            defines_summary
        );
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
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        // Reset memoized Shader_stages* on every live handle before
        // freeing the entries; the cached pointers would otherwise
        // dangle past this scope.
        for (Variant_handle* handle : m_handles) {
            handle->reset_memoization();
        }
        erhe::graphics::Shader_monitor& monitor = m_graphics_device.get_shader_monitor();
        for (auto& [key, entry] : m_entries) {
            monitor.remove(*entry);
        }
        m_entries.clear();
    }
    // The Vulkan backend's render-pipeline cache hashes raw shader-module
    // handles. Driver-recycled handles after our shader_stages drop above
    // would otherwise collide with stale cached pipelines and bind the
    // wrong shader. Other backends implement this as a no-op.
    m_graphics_device.clear_render_pipeline_cache();
}

auto Shader_variant_cache::size() const -> std::size_t
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_entries.size();
}

void Shader_variant_cache::register_handle(Variant_handle* handle)
{
    ERHE_VERIFY(handle != nullptr);
    std::lock_guard<std::mutex> lock{m_mutex};
    m_handles.push_back(handle);
}

void Shader_variant_cache::unregister_handle(Variant_handle* handle) noexcept
{
    if (handle == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock{m_mutex};
    std::vector<Variant_handle*>::iterator it = std::find(m_handles.begin(), m_handles.end(), handle);
    if (it != m_handles.end()) {
        m_handles.erase(it);
    }
}

} // namespace erhe::scene_renderer
