#pragma once

#include "app_message.hpp"
#include "background_work.hpp"
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
    class Device;
    class Texture;
}
namespace erhe::primitive {
    class Material;
    class Primitive;
}
namespace tf {
    class Executor;
}

namespace editor {

class App_context;
class App_message_bus;
class Level_kits;
class Kit_thumbnail_builds;
class Scene_root;

enum class Kit_thumbnail_state : unsigned int {
    none = 0, // not requested: its row was not drawn yet
    queued,   // waiting for its build; shows the image of the former lengths when it has one
    building, // a build task has taken it; shows the former image as queued does
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

// A build task took the request of this mesh key (the queued -> building notice).
class Kit_thumbnail_build_start
{
public:
    std::size_t shape_index{0};
    std::string mesh_key;
};

// What a build task made for one request; update() renders or releases it on the main thread.
class Kit_thumbnail_build_result
{
public:
    std::size_t                                              shape_index{0};
    std::string                                              mesh_key;
    std::vector<std::shared_ptr<erhe::primitive::Primitive>> part_primitives; // one per slot; empty when it failed
    std::string                                              problem;         // why it failed; empty on success
};

// The Level Kit window's shape thumbnails (doc/editor/level_tools.md "Shape thumbnails"): each shape at its defaults
// at the window scene's lengths, built on the background executor, rendered once through Brush_preview and kept until
// a lengths or preset change.
class Kit_thumbnails
{
public:
    Kit_thumbnails(
        Level_kits&             level_kits,
        App_context&            context,
        App_message_bus&        app_message_bus,
        erhe::graphics::Device& graphics_device,
        tf::Executor&           background_executor
    );
    ~Kit_thumbnails() noexcept;
    Kit_thumbnails(const Kit_thumbnails&) = delete;
    auto operator=(const Kit_thumbnails&) -> Kit_thumbnails& = delete;

    // ImGui time, before the rows: the lengths are read again when the scene is another one or after a lengths change.
    void follow_scene(const std::shared_ptr<Scene_root>& scene_root);
    // ImGui time: the shape's square, its image or an empty square; queues its render when none was requested. Returns
    // whether the square was clicked.
    [[nodiscard]] auto draw(const Kit_shape_definition& shape, float size) -> bool;
    // Queues every shape not requested yet, as if each row were drawn (MCP request_all); needs the lengths of
    // follow_scene(). Returns the number queued.
    auto request_all() -> std::size_t;
    // Prepare phase, beside Thumbnails::update(): renders every finished build whose mesh key is still wanted.
    void update(erhe::graphics::Command_buffer& command_buffer);
    // Shutdown, before the executors drain: refuses further requests and drops the queued ones and the finished builds.
    void stop();
    // Level_kits::on_level_lengths_changed(): the next follow_scene() reads the lengths again.
    void on_level_lengths_changed();

    [[nodiscard]] auto get_lengths       () const -> const std::optional<Kit_lengths>&;
    [[nodiscard]] auto get_thumbnails    () const -> const std::vector<Kit_thumbnail>&; // by library shape index
    [[nodiscard]] auto get_queued_count  () const -> std::size_t; // requests no build task has taken yet
    [[nodiscard]] auto get_render_count  () const -> std::uint64_t;
    // The texture's side; before it is made, the editor setting thumbnails.size_pixels it will take.
    [[nodiscard]] auto get_size_pixels   () const -> int;

private:
    void apply_lengths     (const Kit_lengths& lengths);
    void queue             (std::size_t shape_index);
    // A preset change (Thumbnails's preset key) queues every rendered shape again.
    void on_graphics_settings_changed(const Graphics_preset_entry* graphics_preset);
    void make_texture      (erhe::graphics::Command_buffer& command_buffer);
    void apply_result      (erhe::graphics::Command_buffer& command_buffer, const Kit_thumbnail_build_result& result);
    [[nodiscard]] auto get_shape_index(const Kit_shape_definition& shape) const -> std::size_t;
    [[nodiscard]] auto get_mesh_key   (const Kit_shape_definition& shape) const -> std::string;
    // The thumbnails' own material of the palette material, listed in the preview scene; null for an unknown name.
    [[nodiscard]] auto get_material   (const std::string& palette_name) -> std::shared_ptr<erhe::primitive::Material>;

    Level_kits&                                             m_level_kits;
    App_context&                                            m_context;
    std::vector<Kit_thumbnail>                              m_thumbnails;
    std::vector<std::string>                                m_requested_mesh_keys; // per shape: the key of its latest request
    std::shared_ptr<Kit_thumbnail_builds>                   m_builds;         // co-owned by the build tasks in flight
    std::vector<Kit_thumbnail_build_start>                  m_taken_starts;   // update() scratch
    std::vector<Kit_thumbnail_build_result>                 m_taken_results;  // update() scratch
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
    Background_work_owner                                   m_background_work;
};

} // namespace editor
