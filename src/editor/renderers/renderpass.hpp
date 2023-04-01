#pragma once

#include "renderers/enums.hpp"
#include "renderers/primitive_buffer.hpp"

#include "erhe/primitive/enums.hpp"
#include "erhe/scene/item.hpp"

#include <functional>
#include <initializer_list>
#include <string_view>

namespace erhe::scene
{
    using Layer_id = uint64_t;
}

namespace editor
{

class Pipeline_renderpass;
class Render_context;
class Render_style_data;
class Scene_root;

class Renderpass
    : public erhe::scene::Item
{
public:
    Renderpass(const std::string_view name);
    ~Renderpass() noexcept override;

    void render(const Render_context& context) const;

    // Implements Item
    [[nodiscard]] static auto static_type     () -> uint64_t;
    [[nodiscard]] static auto static_type_name() -> const char*;
    [[nodiscard]] auto get_type () const -> uint64_t override;
    [[nodiscard]] auto type_name() const -> const char* override;

    std::vector<erhe::scene::Layer_id> mesh_layers;
    std::vector<Pipeline_renderpass*>  passes;
    erhe::primitive::Primitive_mode    primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
    erhe::scene::Item_filter           filter{};
    std::shared_ptr<Scene_root>        override_scene_root{};
    bool                               allow_shader_stages_override{true};

    std::function<void()>                                                  begin;
    std::function<void()>                                                  end; 
    std::function<const Render_style_data*(const Render_context& context)> get_render_style;
};

} // namespace editor
