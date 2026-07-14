#pragma once

#include "erhe_graphics/enums.hpp"
#include "erhe_utility/debug_label.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace erhe::graphics {

class Device;
class Sampler;
class Shader_resource;
class Texture;

enum class Binding_type : unsigned int {
    uniform_buffer,
    storage_buffer,
    combined_image_sampler,
    // Load/store storage image (compute read/write). Uses the raw
    // binding_point (no sampler-binding offset). glsl_type names the image
    // type (e.g. image_2d) and image_format the GLSL format qualifier.
    storage_image
};

class Bind_group_layout_binding
{
public:
    uint32_t     binding_point   {0};
    Binding_type type            {Binding_type::uniform_buffer};
    uint32_t     descriptor_count{1};

    // --- The following fields are only meaningful when
    // type == combined_image_sampler.

    // Which image aspect to bind when set_sampled_image() writes a
    // depth+stencil texture to this binding. A sampling view must commit
    // to a single aspect (VUID-VkDescriptorImageInfo-imageView-01976).
    Sampler_aspect   sampler_aspect {Sampler_aspect::color};

    // The GLSL identifier the shader references this sampler by, e.g.
    // "s_shadow_compare". Required. The Bind_group_layout_impl copies
    // this into an internal Shader_resource during construction, so the
    // view only needs to live until the constructor returns -- string
    // literals (which all erhe call sites use) work.
    std::string_view name           {};

    // GLSL type of the sampler uniform declaration, e.g. sampler_2d,
    // sampler_2d_array_shadow, unsigned_int_sampler_buffer. For
    // storage_image bindings this is the image type (e.g. image_2d).
    Glsl_type        glsl_type      {Glsl_type::sampler_2d};

    // --- Only meaningful when type == storage_image.
    // GLSL format layout qualifier for the storage image declaration, e.g.
    // "rgba16f". Required: emitted inside layout(binding = N, <image_format>)
    // so imageLoad / imageStore are valid.
    std::string_view image_format   {};

    // true if this sampler represents the bindless / argument-buffer /
    // sampler-array texture heap (material textures), false for
    // dedicated samplers bound via set_sampled_image() per draw.
    bool             is_texture_heap{false};

    // Explicit array size for texture-heap samplers. Ignored otherwise.
    // 0 means "scalar" for dedicated samplers, or "use the GL sampler-
    // array implicit s_texture sizing" when this binding is omitted.
    uint32_t         array_size     {0};

    // Optional pre-bound sampler. When non-null on a combined_image_sampler
    // binding the Vulkan backend installs the sampler as a pImmutableSamplers
    // entry on the descriptor set layout, and Render_command_encoder writes
    // VK_NULL_HANDLE for the sampler field in the descriptor write (the
    // layout's immutable sampler is used instead of any runtime-supplied one).
    // Required on MoltenVK for comparison samplers
    // (VkPhysicalDevicePortabilitySubsetFeaturesKHR::mutableComparisonSamplers
    // is VK_FALSE there). The pointed-to Sampler must outlive the
    // Bind_group_layout. OpenGL and Metal backends ignore this field.
    const Sampler*   immutable_sampler{nullptr};

    // Shader stages this binding is accessed in (Shader_stage_flags bitmask).
    // Honored by the Vulkan backend's VkDescriptorSetLayoutBinding::stageFlags;
    // OpenGL and Metal are not stage-scoped and ignore it.
    //
    // Defaults to Shader_stage_flags::none to force every binding -- buffers
    // (UBO/SSBO) and samplers alike -- to declare its stages explicitly. The
    // Vulkan backend treats a none binding as a configuration error rather than
    // silently assuming a stage, so callers must set this to exactly the stages
    // the resource is read in: e.g. a UBO read only in the vertex shader is
    // Shader_stage_flags::vertex; a sampler read in fragment is
    // Shader_stage_flags::fragment; the shadow-texel debug sampler read in the
    // vertex stage is vertex|fragment.
    uint32_t         stage_flags{Shader_stage_flags::none};
};

class Bind_group_layout_create_info
{
public:
    std::vector<Bind_group_layout_binding> bindings{};
    erhe::utility::Debug_label             debug_label;

    // When true, pipelines built with this layout participate in the
    // device-level bindless texture heap (Vulkan: descriptor set 1, sampled
    // via "erhe_texture_heap[]" in GLSL). Shaders that sample material or
    // imgui textures via the heap need this. Compute and other pipelines
    // that do not sample from the heap must set this to false so the
    // shader preamble and pipeline layout omit set 1 -- otherwise
    // validation warns that the declared set is never bound.
    bool                                   uses_texture_heap{true};
};

class Bind_group_layout_impl;

class Bind_group_layout
{
public:
    Bind_group_layout (Device& device, const Bind_group_layout_create_info& create_info);
    ~Bind_group_layout() noexcept;
    Bind_group_layout (const Bind_group_layout&) = delete;
    void operator=    (const Bind_group_layout&) = delete;
    Bind_group_layout (Bind_group_layout&& other) noexcept;
    auto operator=    (Bind_group_layout&& other) noexcept -> Bind_group_layout&;

    [[nodiscard]] auto get_impl                  () -> Bind_group_layout_impl&;
    [[nodiscard]] auto get_impl                  () const -> const Bind_group_layout_impl&;
    [[nodiscard]] auto get_debug_label           () const -> erhe::utility::Debug_label;
    [[nodiscard]] auto get_sampler_binding_offset() const -> uint32_t;
    [[nodiscard]] auto uses_texture_heap         () const -> bool;

    // The Shader_resource of type "samplers" built from the create_info's
    // combined_image_sampler bindings plus (on GL sampler-array and Metal
    // argument-buffer paths) an implicit texture-heap s_texture. The
    // graphics backend emits this into the GLSL preamble as
    // "uniform sampler* s_foo;" declarations and uses it to classify
    // samplers as texture-heap or dedicated. Application code should not
    // need to touch it directly.
    [[nodiscard]] auto get_default_uniform_block () const -> const Shader_resource&;

private:
    std::unique_ptr<Bind_group_layout_impl> m_impl;
    bool                                    m_uses_texture_heap{true};
};

} // namespace erhe::graphics
