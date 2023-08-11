#pragma once

#include "erhe/renderer/enums.hpp"
#include "erhe/scene_renderer/primitive_buffer.hpp"

#include "erhe/primitive/enums.hpp"
#include "erhe/item/item.hpp"

#include <functional>
#include <initializer_list>
#include <string_view>

namespace erhe::renderer {
    class Pipeline_renderpass;
}
namespace erhe::scene {
    using Layer_id = uint64_t;
}

namespace editor
{

class Render_context;
class Render_style_data;
class Scene_root;

class Renderpass
    : public erhe::Item
{
public:
    explicit Renderpass(const std::string_view name);

    void render(const Render_context& context) const;

    // Implements Item
    static constexpr std::string_view static_type_name{"Renderpass"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    [[nodiscard]] auto get_type     () const -> uint64_t         override;
    [[nodiscard]] auto get_type_name() const -> std::string_view override;

    std::vector<erhe::scene::Layer_id>                mesh_layers;
    std::vector<erhe::renderer::Pipeline_renderpass*> passes;
    erhe::primitive::Primitive_mode                   primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
    erhe::Item_filter                                 filter{};
    std::shared_ptr<Scene_root>                       override_scene_root{};
    bool                                              allow_shader_stages_override{true};

    std::optional<erhe::scene_renderer::Primitive_interface_settings> primitive_settings;
    std::function<void()>                                                  begin;
    std::function<void()>                                                  end; 
    std::function<const Render_style_data&(const Render_context& context)> get_render_style;
};

} // namespace editor
