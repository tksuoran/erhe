#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace erhe::graphics {
    class Device;
    class Reloadable_shader_stages;
    class Shader_stages;
}

namespace erhe::scene_renderer {

class Program_interface;

// Registry of shader source names that Shader_variant_key accepts.
// Acts as a name-typo guard: passing an unknown name to the ctor will
// trip ERHE_VERIFY at construction so a stringly-typed typo can't fall
// through to a vkCreateShaderModule null return at render time.
// The registry is populated by `register_shader_name` from Programs's
// ctor so it can be extended without touching this header.
class Shader_name_registry
{
public:
    static Shader_name_registry& instance();

    void register_name(const std::string& name);
    [[nodiscard]] auto is_registered(const std::string& name) const -> bool;

private:
    Shader_name_registry() = default;
    mutable std::mutex                     m_mutex;
    std::unordered_map<std::string, bool>  m_names;
};

// Generic cache key: a shader is uniquely identified by its source name plus
// the set of GLSL defines it was compiled with, the multiview view count
// (0 = single-view, >= 2 = multiview), and a couple of create-info flags
// that change the prelude. The defines vector is canonicalized at key
// construction (sorted by name) so two keys built in different orders
// compare equal.
class Shader_variant_key
{
public:
    // Sorts and dedups the defines vector so equal logical keys hash the
    // same regardless of insertion order. No default constructor is
    // exposed -- every key must go through this ctor so the
    // canonicalization invariant holds for hash / equality.
    //
    // The shader name is auto-registered with Shader_name_registry
    // on construction. Once a name is in the registry, downstream
    // tooling can verify against it to catch typos before the
    // mistyped name reaches Metal / Vulkan and silently returns
    // nullptr from the pipeline ctor.
    Shader_variant_key(
        std::string                                      name,
        std::vector<std::pair<std::string, std::string>> defines,
        uint32_t                                         multiview_view_count = 0,
        bool                                             no_vertex_input      = false,
        bool                                             dump_interface       = false
    );

    [[nodiscard]] auto operator==(const Shader_variant_key& other) const noexcept -> bool = default;

    std::string                                      name;
    std::vector<std::pair<std::string, std::string>> defines;
    uint32_t                                         multiview_view_count{0};
    bool                                             no_vertex_input{false};
    bool                                             dump_interface{false};
};

class Shader_variant_key_hash
{
public:
    [[nodiscard]] auto operator()(const Shader_variant_key& key) const noexcept -> std::size_t;
};

// On-demand cache of compiled shader stages, keyed by Shader_variant_key.
// Replaces the per-shader Reloadable_shader_stages members + eager compile
// pattern: callers construct a key, the cache compiles on miss, registers
// the entry with the device's Shader_monitor for hot reload, and returns
// a const Shader_stages*. The fallback shader on compile failure is
// itself a key in the cache (callers pass a "fallback" key, typically
// the editor's "error" shader); the cache lazily compiles it the first
// time it is needed.
//
// Callers must hold the GL context (the render thread) when calling
// get_or_compile; the cache compiles synchronously.
class Shader_variant_cache final
{
public:
    Shader_variant_cache(
        erhe::graphics::Device& graphics_device,
        Program_interface&      program_interface
    );
    ~Shader_variant_cache() noexcept;

    Shader_variant_cache(const Shader_variant_cache&)            = delete;
    Shader_variant_cache(Shader_variant_cache&&)                 = delete;
    Shader_variant_cache& operator=(const Shader_variant_cache&) = delete;
    Shader_variant_cache& operator=(Shader_variant_cache&&)      = delete;

    // Look up the compiled shader stages for `key`, compiling on miss.
    // Returns a stable Shader_stages* that lives as long as the cache or
    // until clear() is called. Returns the fallback Shader_stages* (looked
    // up by `fallback_key`) when the requested key fails to compile;
    // returns nullptr if the fallback also fails.
    [[nodiscard]] auto get_or_compile(
        const Shader_variant_key& key,
        const Shader_variant_key& fallback_key
    ) -> const erhe::graphics::Shader_stages*;

    // Variant without a fallback. Returns nullptr on compile failure.
    [[nodiscard]] auto get_or_compile(const Shader_variant_key& key) -> const erhe::graphics::Shader_stages*;

    // Drop every cached variant. Returned pointers become dangling.
    // Shader_monitor entries are detached first so the polling thread
    // does not call reload() on freed memory.
    void clear();

    [[nodiscard]] auto size() const -> std::size_t;

private:
    // Internal compile path. Caller holds m_mutex.
    [[nodiscard]] auto compile_locked(const Shader_variant_key& key) -> erhe::graphics::Reloadable_shader_stages*;

    erhe::graphics::Device& m_graphics_device;
    Program_interface&      m_program_interface;

    // unique_ptr so map rehash / erase doesn't move the
    // Reloadable_shader_stages (the Shader_monitor keeps a stable pointer
    // to the .shader_stages member).
    std::unordered_map<
        Shader_variant_key,
        std::unique_ptr<erhe::graphics::Reloadable_shader_stages>,
        Shader_variant_key_hash
    > m_entries;
    mutable std::mutex m_mutex;
};

} // namespace erhe::scene_renderer
