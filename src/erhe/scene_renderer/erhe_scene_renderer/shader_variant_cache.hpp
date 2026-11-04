#pragma once

#include "erhe_scene_renderer/shader_key.hpp"
#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
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

// The OVERDRAW_WEIGHT of a cost counting variant (doc/plans/shader_cost.md B2),
// from the program name and variant defines of its OVERDRAW off twin.
using Overdraw_weight_provider = std::function<uint32_t(
    std::string_view                                        program,
    const std::vector<std::pair<std::string, std::string>>& variant_defines
)>;

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

    // Replaces the weights of the cost counting variants; bumps get_overdraw_weight_serial().
    void set_overdraw_weight_provider(Overdraw_weight_provider provider);
    // Resolutions of OVERDRAW cost variants are current while this is unchanged.
    [[nodiscard]] auto get_overdraw_weight_serial() const -> uint64_t { return m_overdraw_weight_serial; }

private:
    [[nodiscard]] auto get_overdraw_weight(const Shader_key& key, const erhe::dataformat::Vertex_format* vertex_format) -> uint32_t;

    erhe::graphics::Device& m_graphics_device;
    Program_interface&      m_program_interface;

    std::unordered_map<
        Shader_key,
        std::unique_ptr<erhe::graphics::Reloadable_shader_stages>,
        Shader_key_hash
    > m_entries;
    mutable std::mutex m_mutex;

    Overdraw_weight_provider                                m_overdraw_weight_provider;
    uint64_t                                                m_overdraw_weight_serial{0};
    std::unordered_map<Shader_key, uint32_t, Shader_key_hash> m_overdraw_weights; // by cost key, weight 0
};

} // namespace erhe::scene_renderer
