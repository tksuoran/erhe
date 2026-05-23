#pragma once

#include "erhe_scene_renderer/shader_key.hpp"
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

class Shader_variant_cache
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

    [[nodiscard]] auto get(
        const Shader_key&                      key,
        const erhe::dataformat::Vertex_format* vertex_format // = nullptr
    ) -> erhe::graphics::Reloadable_shader_stages*;

private:
    erhe::graphics::Device& m_graphics_device;
    Program_interface&      m_program_interface;

    std::unordered_map<
        Shader_key,
        std::unique_ptr<erhe::graphics::Reloadable_shader_stages>,
        Shader_key_hash
    > m_entries;
    mutable std::mutex m_mutex;
};

} // namespace erhe::scene_renderer
