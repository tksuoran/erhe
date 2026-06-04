#pragma once

#include "erhe_renderer/enums.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include "erhe_item/item.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_utility/debug_label.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <optional>

namespace erhe::graphics { class Base_render_pipeline; }
namespace erhe::scene    { using Layer_id = uint64_t; }

struct Render_style_data;

namespace editor {

class Render_context;
class Scene_root;

class Composition_pass_data
{
public:
    bool                                                                   enabled{true}; // TODO consider using Item visibility flag
    uint32_t                                                               content_wide_line_group{0};
    std::vector<erhe::scene::Layer_id>                                     mesh_layers;
    std::size_t                                                            non_mesh_vertex_count{0};
    std::vector<erhe::graphics::Base_render_pipeline*>                     base_render_pipelines;
    erhe::scene_renderer::Blending_mode_policy                             blending_mode_policy{erhe::scene_renderer::Blending_mode_policy::not_set};
    erhe::primitive::Primitive_mode                                        primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
    erhe::Item_filter                                                      filter{};
    std::shared_ptr<Scene_root>                                            override_scene_root{};
    uint32_t                                                               shader_key_force_enable_mask{0};
    uint32_t                                                               shader_key_force_disable_mask{0};
    erhe::graphics::Shader_stages*                                         shader_stages{nullptr};
    // Grid settings (cell sizes, line widths, per-level colors, axis
    // label settings) forwarded to the camera UBO; read by grid.frag.
    erhe::scene_renderer::Grid_parameters                                  grid_parameters{};
    std::optional<erhe::scene_renderer::Primitive_interface_settings>      primitive_settings;
    std::function<void()>                                                  begin;
    std::function<void()>                                                  end; 
    std::function<const Render_style_data&(const Render_context& context)> get_render_style;
};

class Composition_pass : public erhe::Item<erhe::Item_base, erhe::Item_base, Composition_pass>
{
public:
    explicit Composition_pass(const Composition_pass&);
    Composition_pass& operator=(const Composition_pass&);
    ~Composition_pass() noexcept override;

    explicit Composition_pass(std::string_view name);

    virtual void render(const Render_context& context);
    void imgui();

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Composition_pass"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::composition_pass; }

    Composition_pass_data data;

private:
    std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>> m_mesh_spans;
};

}
