#pragma once

#include "preview/scene_preview.hpp"

#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"

namespace erhe { class Item_host; }

namespace editor {

class App_message_bus;

class Material_preview : public Scene_preview
{
public:
    Material_preview(
        erhe::graphics::Device&            graphics_device,
        erhe::graphics::Command_buffer&    init_command_buffer,
        App_context&                       app_context,
        App_message_bus&                   app_message_bus,
        erhe::scene_renderer::Mesh_memory& mesh_memory
    );
    ~Material_preview() noexcept;

    void render_preview(const std::shared_ptr<erhe::primitive::Material>& material);
    void render_preview(
        const std::shared_ptr<erhe::graphics::Texture>&   texture,
        const std::shared_ptr<erhe::primitive::Material>& material
    );
    void show_preview  ();

    // Cached reference, for the MCP get_editor_references query
    // (doc/import-undo-reference-clearing.md).
    [[nodiscard]] auto get_last_material() const -> const std::shared_ptr<erhe::primitive::Material>&;
private:
    // Scene close: when the last previewed material is hosted by the closing
    // scene, drop everything that pins it - the cache itself, the reference
    // entry render_preview() left in the preview library, and the preview
    // mesh's primitive material.
    void on_close_scene(erhe::Item_host* closing_host);
    // Content removed without a scene closing (undo of a glTF import):
    // drop the reference when it names one of the removed items.
    void on_items_removed(const Removed_items& removed);

    void make_preview_scene(erhe::scene_renderer::Mesh_memory& mesh_memory);
    //// void generate_torus_geometry();

    erhe::message_bus::Subscription<Close_scene_message> m_close_scene_subscription;
    erhe::message_bus::Subscription<Items_removed_message> m_items_removed_subscription;

    std::shared_ptr<erhe::primitive::Material> m_last_material;

    std::shared_ptr<erhe::scene::Node>         m_node;
    std::shared_ptr<erhe::scene::Mesh>         m_mesh;
    std::shared_ptr<erhe::scene::Node>         m_key_light_node;
    std::shared_ptr<erhe::scene::Light>        m_key_light;

    int                                        m_slice_count{40};
    int                                        m_stack_count{22};
    float                                      m_radius{1.0f};
};

}
