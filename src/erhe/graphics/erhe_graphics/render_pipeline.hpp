#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/state/input_assembly_state.hpp"
#include "erhe_graphics/state/multisample_state.hpp"
#include "erhe_graphics/state/rasterization_state.hpp"
#include "erhe_graphics/state/viewport_state.hpp"
#include "erhe_utility/debug_label.hpp"

#include <array>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace erhe::dataformat {
    class Vertex_format;
}

namespace erhe::graphics {

class Bind_group_layout;
class Device;
class Render_pass;
class Render_pass_descriptor;
class Shader_stages;
class Vertex_input_state;

class Base_render_pipeline_create_info
{
public:
    erhe::utility::Debug_label debug_label         {};

    Input_assembly_state       input_assembly      {};
    Multisample_state          multisample         {};
    Viewport_depth_range_state viewport_depth_range{};
    Rasterization_state        rasterization       {};
    Depth_stencil_state        depth_stencil       {};
    Color_blend_state          color_blend         {};
    const Bind_group_layout*   bind_group_layout   {nullptr};
};

class Render_pipeline_create_info
{
public:
    // Helper: populate format fields from a Render_pass_descriptor
    void set_format_from_render_pass(const Render_pass_descriptor& desc);

    Base_render_pipeline_create_info        base;

    const Shader_stages*                    shader_stages{nullptr};
    const Vertex_input_state*               vertex_input {nullptr};
    const erhe::dataformat::Vertex_format*  vertex_format{nullptr};

    // Render pass format compatibility
    unsigned int                            color_attachment_count   {0};
    std::array<erhe::dataformat::Format, 4> color_attachment_formats {};
    erhe::dataformat::Format                depth_attachment_format  {erhe::dataformat::Format::format_undefined};
    erhe::dataformat::Format                stencil_attachment_format{erhe::dataformat::Format::format_undefined};
    unsigned int                            sample_count             {1};

    // Attachment usage flags for render pass dependency derivation
    std::array<uint64_t, 4>                 color_usage_before       {};
    std::array<uint64_t, 4>                 color_usage_after        {};
    uint64_t                                depth_usage_before       {0};
    uint64_t                                depth_usage_after        {0};
};

class Render_pipeline_impl;

class Render_pipeline
{
public:
    Render_pipeline (Device& device, const Render_pipeline_create_info& create_info);
    ~Render_pipeline() noexcept;
    Render_pipeline (const Render_pipeline&) = delete;
    void operator=  (const Render_pipeline&) = delete;
    Render_pipeline (Render_pipeline&& other) noexcept;
    auto operator=  (Render_pipeline&& other) noexcept -> Render_pipeline&;

    [[nodiscard]] auto get_impl       () -> Render_pipeline_impl&;
    [[nodiscard]] auto get_impl       () const -> const Render_pipeline_impl&;
    [[nodiscard]] auto get_debug_label() const -> erhe::utility::Debug_label;
    [[nodiscard]] auto is_valid       () const -> bool;

    // Access to the create info for OpenGL state tracker and debug
    [[nodiscard]] auto get_create_info() const -> const Render_pipeline_create_info&;

private:
    Render_pipeline_create_info           m_create_info;
    std::unique_ptr<Render_pipeline_impl> m_impl;
};

// Lazy_render_pipeline: stores pipeline state without format info.
// Creates the actual Render_pipeline on first use, reading format info
// from the active render pass. Caches variants for different formats.
class Lazy_render_pipeline
{
public:
    Lazy_render_pipeline();
    Lazy_render_pipeline(Device& device, const Base_render_pipeline_create_info& create_info);
    ~Lazy_render_pipeline() noexcept;
    Lazy_render_pipeline(const Lazy_render_pipeline&) = delete;
    void operator=(const Lazy_render_pipeline&) = delete;
    Lazy_render_pipeline(Lazy_render_pipeline&& other) noexcept;
    auto operator=(Lazy_render_pipeline&& other) noexcept -> Lazy_render_pipeline&;

    // Get or create a graphics pipeline for the given render pass format.
    // Call this during rendering when the render pass is active.
    [[nodiscard]] auto get_pipeline_for(
        const Render_pass_descriptor&          render_pass_desc,
        const Shader_stages*                   shader_stages,
        const Vertex_input_state*              vertex_input,
        const erhe::dataformat::Vertex_format* vertex_format
    ) -> Render_pipeline*;

    [[nodiscard]] auto get_create_info() const -> const Base_render_pipeline_create_info&;

    // Direct access to the create info for reading topology etc.
    Base_render_pipeline_create_info data;

private:
    Device*                                                           m_device{nullptr};
    std::mutex                                                        m_mutex;
    std::unordered_map<std::size_t, std::unique_ptr<Render_pipeline>> m_variants;
};

} // namespace erhe::graphics
