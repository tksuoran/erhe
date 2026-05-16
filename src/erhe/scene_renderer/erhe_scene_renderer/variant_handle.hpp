#if 0
#pragma once

#include "erhe_scene_renderer/shader_variant_cache.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

namespace erhe::graphics { class Shader_stages; }

namespace erhe::scene_renderer {

class Variant_handle
{
public:
    Variant_handle(Shader_variant_cache& cache, Shader_key key);

    ~Variant_handle() noexcept;

    Variant_handle(const Variant_handle&)            = delete;
    Variant_handle(Variant_handle&&)                 = delete;
    Variant_handle& operator=(const Variant_handle&) = delete;
    Variant_handle& operator=(Variant_handle&&)      = delete;

    [[nodiscard]] auto shader_stages() -> const erhe::graphics::Shader_stages*;

private:
    Shader_variant_cache& m_cache;
    Shader_key            m_key;

    const erhe::graphics::Shader_stages* m_cached_shader_stages{nullptr};
};

} // namespace erhe::scene_renderer
#endif