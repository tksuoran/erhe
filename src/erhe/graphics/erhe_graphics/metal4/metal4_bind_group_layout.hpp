#pragma once

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_utility/debug_label.hpp"

#include <cstdint>
#include <unordered_map>

namespace erhe::graphics {

class Device;

class Bind_group_layout_impl
{
public:
    Bind_group_layout_impl(Device& device, const Bind_group_layout_create_info& create_info);
    ~Bind_group_layout_impl() noexcept = default;

    [[nodiscard]] auto get_debug_label           () const -> erhe::utility::Debug_label;
    [[nodiscard]] auto get_sampler_binding_offset() const -> uint32_t;
    [[nodiscard]] auto get_default_uniform_block () const -> const Shader_resource&;

    // Metal binds samplers from an index space that is separate from -- and
    // far smaller than -- the texture index space: [[sampler(N)]] only accepts
    // N in [0, 15], while [[texture(N)]] accepts up to 128. The GLSL binding
    // number cannot serve as the Metal sampler index, because it is offset
    // past the buffer bindings (see m_sampler_binding_offset) and therefore
    // runs out of range well before 16 dedicated samplers are declared.
    //
    // So dedicated (non-texture-heap) samplers get their own compact index,
    // allocated here in ascending GLSL binding order starting at 0.
    // compile_spirv_to_mtl_function pins msl_sampler to this index and
    // Render_/Compute_command_encoder_impl::set_sampled_image() binds the
    // sampler state to the same slot. Texture-heap samplers live in the
    // argument buffer and do not consume a direct sampler slot.
    //
    // The argument is the GLSL binding number (user-facing binding_point plus
    // get_sampler_binding_offset()). Asserts if the binding is not a dedicated
    // sampler of this layout.
    [[nodiscard]] auto get_metal_sampler_slot(uint32_t glsl_binding) const -> uint32_t;

private:
    erhe::utility::Debug_label                m_debug_label;
    uint32_t                                  m_sampler_binding_offset{0};
    Shader_resource                           m_default_uniform_block;
    std::unordered_map<uint32_t, uint32_t>    m_metal_sampler_slots;
};

} // namespace erhe::graphics
