#pragma once

#include "app_message.hpp"
#include "level/kit_shape.hpp"

#include "erhe_message_bus/message_bus.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace erhe::graphics {
    class Command_buffer;
    class Texture;
}
namespace erhe::primitive {
    class Material;
}

namespace editor {

class App_context;
class App_message_bus;
class Level_kits;
class Scene_root;

enum class Kit_thumbnail_state : unsigned int {
    none = 0, // not requested: its row was not drawn yet
    queued,   // waiting for its render; shows the image of the former lengths when it has one
    current,  // rendered at the thumbnails' lengths
    failed    // the shape's defaults at those lengths made no geometry
};
inline constexpr std::size_t c_kit_thumbnail_state_count{static_cast<std::size_t>(Kit_thumbnail_state::failed) + 1};

[[nodiscard]] auto c_str(Kit_thumbnail_state state) -> const char*;

// One shape's thumbnail: one layer of the thumbnail texture, indexed as the library's shapes.
class Kit_thumbnail
{
public:
    Kit_thumbnail_state state       {Kit_thumbnail_state::none};
    bool                has_image   {false};
    std::string         mesh_key;           // of the default values the image shows (Level_kits::get_mesh_key())
    std::uint64_t       render_count{0};
};

// The Level Kit window's shape thumbnails (doc/editor/level_tools.md "Shape thumbnails"): each shape at its defaults
// at the window scene's lengths, rendered once through Brush_preview and kept until a lengths or preset change.
class Kit_thumbnails
{
public:
    static constexpr std::size_t c_renders_per_frame{2};

    Kit_thumbnails(Level_kits& level_kits, App_context& context, App_message_bus& app_message_bus);
    ~Kit_thumbnails() noexcept;
    Kit_thumbnails(const Kit_thumbnails&) = delete;
    auto operator=(const Kit_thumbnails&) -> Kit_thumbnails& = delete;

    // ImGui time, before the rows: the lengths are read again when the scene is another one or after a lengths change.
    void follow_scene(const std::shared_ptr<Scene_root>& scene_root);
    // ImGui time: the shape's square, its image or an empty square; queues its render when none was requested. Returns
    // whether the square was clicked.
    [[nodiscard]] auto draw(const Kit_shape_definition& shape, float size) -> bool;
    // Prepare phase, beside Thumbnails::update(): renders at most c_renders_per_frame queued thumbnails.
    void update(erhe::graphics::Command_buffer& command_buffer);
    // Level_kits::on_level_lengths_changed(): the next follow_scene() reads the lengths again.
    void on_level_lengths_changed();

    [[nodiscard]] auto get_lengths       () const -> const std::optional<Kit_lengths>&;
    [[nodiscard]] auto get_thumbnails    () const -> const std::vector<Kit_thumbnail>&; // by library shape index
    [[nodiscard]] auto get_queued_count  () const -> std::size_t;
    [[nodiscard]] auto get_render_count  () const -> std::uint64_t;
    // The texture's side; before it is made, the editor setting thumbnails.size_pixels it will take.
    [[nodiscard]] auto get_size_pixels   () const -> int;

private:
    void apply_lengths     (const Kit_lengths& lengths);
    void queue             (std::size_t shape_index);
    // A preset change (Thumbnails's preset key) queues every rendered shape again.
    void on_graphics_settings_changed(const Graphics_preset_entry* graphics_preset);
    void make_texture      (erhe::graphics::Command_buffer& command_buffer);
    void render            (erhe::graphics::Command_buffer& command_buffer, std::size_t shape_index);
    [[nodiscard]] auto get_shape_index(const Kit_shape_definition& shape) const -> std::size_t;
    [[nodiscard]] auto get_mesh_key   (const Kit_shape_definition& shape) const -> std::string;
    // The thumbnails' own material of the palette material, listed in the preview scene; null for an unknown name.
    [[nodiscard]] auto get_material   (const std::string& palette_name) -> std::shared_ptr<erhe::primitive::Material>;

    Level_kits&                                             m_level_kits;
    App_context&                                            m_context;
    std::vector<Kit_thumbnail>                              m_thumbnails;
    std::vector<std::size_t>                                m_queue;          // shape indices, oldest first
    std::optional<Kit_lengths>                              m_lengths;
    std::weak_ptr<Scene_root>                               m_scene;
    bool                                                    m_lengths_changed{false};
    std::uint64_t                                           m_render_count{0};
    std::string                                             m_rendered_preset_key;
    erhe::message_bus::Subscription<Graphics_settings_message> m_graphics_settings_subscription;
    std::shared_ptr<erhe::graphics::Texture>                m_texture;        // 2D array, one layer per shape
    std::vector<std::shared_ptr<erhe::graphics::Texture>>   m_layer_views;    // per layer, on devices with texture views
    std::map<std::string, std::shared_ptr<erhe::primitive::Material>> m_materials; // by palette material name
    std::vector<std::string>                                m_slot_names;     // render scratch
    std::vector<std::shared_ptr<erhe::primitive::Material>> m_part_materials; // render scratch
};

} // namespace editor
