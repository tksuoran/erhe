#pragma once

#include "erhe/components/components.hpp"
#include "renderers/renderpass.hpp" // TODO remove - for Fill_mode, Blend_mode, Selection_mode

#include <memory>

namespace editor
{

class Editor_rendering;
class Render_context;



class IEditor_rendering
{
public:
    virtual ~IEditor_rendering() noexcept;
    virtual void trigger_capture     () = 0;
    virtual void render              () = 0;
    virtual void render_viewport_main(const Render_context& context, bool has_pointer) = 0;
    virtual void render_composer     (const Render_context& context) = 0;
    virtual void render_id           (const Render_context& context) = 0;
    virtual void begin_frame         () = 0;
    virtual void end_frame           () = 0;
};

class Editor_rendering_impl;

class Editor_rendering
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Editor_rendering"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Editor_rendering ();
    ~Editor_rendering() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;
    void post_initialize            () override;

private:
    std::unique_ptr<Editor_rendering_impl> m_impl;
};

extern IEditor_rendering* g_editor_rendering;

static constexpr unsigned int s_stencil_edge_lines               =  1u; // 0 inc
static constexpr unsigned int s_stencil_tool_mesh_hidden         =  2u;
static constexpr unsigned int s_stencil_tool_mesh_visible        =  3u;

static constexpr unsigned int s_stencil_line_renderer_grid_minor =  8u;
static constexpr unsigned int s_stencil_line_renderer_grid_major =  9u;
static constexpr unsigned int s_stencil_line_renderer_selection  = 10u;
static constexpr unsigned int s_stencil_line_renderer_tools      = 11u;


}
