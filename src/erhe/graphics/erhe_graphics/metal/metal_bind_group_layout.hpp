#pragma once

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_utility/debug_label.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace MTL { class ArgumentEncoder; }

namespace erhe::graphics {

class Device;

class Bind_group_layout_impl
{
public:
    Bind_group_layout_impl(Device& device, const Bind_group_layout_create_info& create_info);
    ~Bind_group_layout_impl() noexcept;
    Bind_group_layout_impl(const Bind_group_layout_impl&)            = delete;
    Bind_group_layout_impl& operator=(const Bind_group_layout_impl&) = delete;
    Bind_group_layout_impl(Bind_group_layout_impl&&)                 = delete;
    Bind_group_layout_impl& operator=(Bind_group_layout_impl&&)      = delete;

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

    // The Shader_stage_flags the dedicated sampler at glsl_binding declared
    // (Bind_group_layout_binding::stage_flags): the render stages
    // Render_command_encoder_impl::set_sampled_image() binds it to. Asserts
    // like get_metal_sampler_slot().
    [[nodiscard]] auto get_sampler_stage_flags(uint32_t glsl_binding) const -> uint32_t;

    // The fragment argument buffer (doc/erhe/metal_backend.md "Two Sampler
    // Binding Paths"): a fragment function reads every dedicated sampler
    // whose stage_flags include the fragment stage from one argument buffer
    // at [[buffer(15)]] (descriptor set 6), not from [[texture(N)]] /
    // [[sampler(M)]], so a fragment function binds no texture or sampler
    // directly and its pipeline may execute indirect command buffers
    // (Indirect_count_draw_support::enabled). Entry k (ascending GLSL
    // binding) holds its texture at [[id(2k)]] and its sampler at
    // [[id(2k + 1)]]; the shader-stages prototype pins the same ids and
    // Render_command_encoder_impl encodes the table set_sampled_image()
    // filled before a draw. Vertex and compute functions keep the direct
    // bindings.
    class Fragment_argument
    {
    public:
        uint32_t glsl_binding{0};
        uint32_t texture_id  {0};
        uint32_t sampler_id  {0};
    };
    static constexpr uint32_t fragment_argument_buffer_index         {15};
    static constexpr uint32_t fragment_argument_buffer_descriptor_set{6};
    // The entry index of the dedicated sampler at glsl_binding in the
    // fragment argument buffer; empty when it is not read there.
    [[nodiscard]] auto get_fragment_argument_index  (uint32_t glsl_binding) const -> std::optional<std::size_t>;
    [[nodiscard]] auto get_fragment_arguments       () const -> std::span<const Fragment_argument>;
    // Null when the layout has no fragment-stage dedicated sampler.
    [[nodiscard]] auto get_fragment_argument_encoder() const -> MTL::ArgumentEncoder*;

private:
    // A dedicated sampler's compact Metal sampler index and declared stages.
    class Dedicated_sampler
    {
    public:
        uint32_t sampler_slot{0};
        uint32_t stage_flags {0};
    };
    [[nodiscard]] auto get_dedicated_sampler(uint32_t glsl_binding) const -> const Dedicated_sampler&;

    erhe::utility::Debug_label                        m_debug_label;
    uint32_t                                          m_sampler_binding_offset{0};
    Shader_resource                                   m_default_uniform_block;
    std::unordered_map<uint32_t, Dedicated_sampler>   m_dedicated_samplers; // by GLSL binding
    std::vector<Fragment_argument>                    m_fragment_arguments; // ascending GLSL binding
    MTL::ArgumentEncoder*                             m_fragment_argument_encoder{nullptr};
};

} // namespace erhe::graphics
