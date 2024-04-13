#pragma once

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/hand_tracker.hpp"
#endif

#include "erhe_scene/mesh.hpp"

#include <glm/glm.hpp>

namespace erhe {
    class Item_host;
}
namespace erhe::graphics {
    class Framebuffer;
    class Instance;
    class Sampler;
    class Texture;
}
namespace erhe::primitive {
    class Material;
}

namespace editor
{

class Editor_context;
class Editor_message;
class Forward_renderer;
class Hand_tracker;
class Mesh_memory;
class Render_context;
class Scene_root;
class Scene_view;
class Viewport_config_window;
class Viewport_window;

class Rendertarget_mesh
    : public erhe::scene::Mesh
{
public:
    Rendertarget_mesh(
        erhe::graphics::Instance& graphics_instance,
        Mesh_memory&              mesh_memory,
        int                       width,
        int                       height,
        float                     pixels_per_meter
    );

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Rendertarget_mesh"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    [[nodiscard]] auto get_type     () const -> uint64_t         override;
    [[nodiscard]] auto get_type_name() const -> std::string_view override;

    // Public API
    [[nodiscard]] auto texture         () const -> std::shared_ptr<igl::ITexture>;
    [[nodiscard]] auto framebuffer     () const -> std::shared_ptr<igl::IFramebuffer>;
    [[nodiscard]] auto width           () const -> float;
    [[nodiscard]] auto height          () const -> float;
    [[nodiscard]] auto pixels_per_meter() const -> float;
    [[nodiscard]] auto get_pointer     () const -> std::optional<glm::vec2>;
    [[nodiscard]] auto world_to_window (glm::vec3 world_position) const -> std::optional<glm::vec2>;

#if defined(ERHE_XR_LIBRARY_OPENXR)
    void update_headset();
#endif

    auto update_pointer(Scene_view* scene_view) -> bool;
    void bind          ();
    void clear         (glm::vec4 clear_color);
    void render_done   (Editor_context& context); // generates mipmaps, updates lod bias

    void resize_rendertarget(
        erhe::graphics::Instance& graphics_instance,
        Mesh_memory&              mesh_memory,
        int                       width,
        int                       height
    );

private:
    float                                        m_pixels_per_meter{0.0f};
    float                                        m_local_width     {0.0f};
    float                                        m_local_height    {0.0f};

    std::shared_ptr<igl::ITexture>     m_texture;
    std::shared_ptr<erhe::graphics::Sampler>     m_sampler;
    std::shared_ptr<erhe::primitive::Material>   m_material;
    std::shared_ptr<igl::IFramebuffer> m_framebuffer;
    std::optional<glm::vec2>                     m_pointer;

#if defined(ERHE_XR_LIBRARY_OPENXR)
    std::optional<Finger_point> m_pointer_finger;
    bool                        m_finger_trigger{false};
#endif
};

[[nodiscard]] auto is_rendertarget(const erhe::Item_base* item) -> bool;
[[nodiscard]] auto is_rendertarget(const std::shared_ptr<erhe::Item_base>& item) -> bool;

[[nodiscard]] auto get_rendertarget(const erhe::scene::Node* node) -> std::shared_ptr<Rendertarget_mesh>;

} // namespace editor
