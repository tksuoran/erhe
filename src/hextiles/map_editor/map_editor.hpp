#pragma once

#include "coordinate.hpp"
#include "terrain_type.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>

namespace hextiles
{

class Map;
class Map_editor;
class Tile_renderer;
class Map_window;
class Tiles;

class Map_hover_command final
    : public erhe::application::Command
{
public:
    explicit Map_hover_command(Map_editor& map_editor)
        : Command     {"Map_editor.hover"}
        , m_map_editor{map_editor}
    {
    }
    ~Map_hover_command() noexcept final {}

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Map_editor& m_map_editor;
};

class Map_primary_brush_command final
    : public erhe::application::Command
{
public:
    explicit Map_primary_brush_command(Map_editor& map_editor)
        : Command     {"Map_editor.primary_brush"}
        , m_map_editor{map_editor}
    {
    }
    ~Map_primary_brush_command() noexcept final {}

    auto try_call (erhe::application::Command_context& context) -> bool override;
    void try_ready(erhe::application::Command_context& context) override;

private:
    Map_editor& m_map_editor;
};

class Map_editor
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Map_editor"};
    static constexpr std::string_view c_title{"Map Editor"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Map_editor ();
    ~Map_editor() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Public API
    [[nodiscard]] auto get_map() -> std::shared_ptr<Map>;
    void terrain_palette();
    void render         ();

    // Commands
    void hover        (glm::vec2 window_position);
    void primary_brush(glm::vec2 mouse_position);

private:
    // Component dependencies
    std::shared_ptr<Map_window>     m_map_window;
    std::shared_ptr<Tile_renderer>  m_tile_renderer;
    std::shared_ptr<Tiles>          m_tiles;

    // Commands
    Map_hover_command               m_map_hover_command;
    Map_primary_brush_command       m_map_primary_brush_command;

    std::shared_ptr<Map>            m_map;
    int                             m_brush_size{1};
    terrain_tile_t                  m_left_brush{Terrain_default};

    std::optional<glm::vec2>        m_hover_window_position;
    std::optional<Tile_coordinate>  m_hover_tile_position;
};

} // namespace hextiles
