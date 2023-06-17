#pragma once

#include "erhe/components/components.hpp"

#include <memory>

namespace erhe::graphics
{
    class Sampler;
    class Shader_resource;
    class Shader_stages;
}

namespace example {

enum class Shader_stages_variant : int
{
    standard
};

static constexpr const char* c_shader_stages_variant_strings[] =
{
    "Standard"
};

class IPrograms
{
public:
    virtual ~IPrograms() noexcept;

    static constexpr std::size_t s_texture_unit_count = 15; // one reserved for shadows

    // Public members
    std::unique_ptr<erhe::graphics::Shader_resource> default_uniform_block;   // for non-bindless textures

    int                                              shadow_texture_unit{15};
    int                                              base_texture_unit{0};
    std::unique_ptr<erhe::graphics::Sampler>         nearest_sampler;
    std::unique_ptr<erhe::graphics::Sampler>         linear_sampler;
    std::unique_ptr<erhe::graphics::Sampler>         linear_mipmap_linear_sampler;

    std::unique_ptr<erhe::graphics::Shader_stages> standard;
    //std::unique_ptr<erhe::graphics::Shader_stages> sky;
};

class Programs_impl;

class Programs
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Programs"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Programs      ();
    ~Programs     () noexcept override;
    Programs      (const Programs&) = delete;
    void operator=(const Programs&) = delete;
    Programs      (Programs&&)      = delete;
    void operator=(Programs&&)      = delete;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

private:
    std::unique_ptr<Programs_impl> m_impl;
};

extern IPrograms* g_programs;

}
