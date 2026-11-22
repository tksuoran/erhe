#pragma once

#include "tools/channel_brush.hpp"

#include <geogram/basic/numeric.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace erhe::geometry  { class Geometry; }
namespace erhe::primitive { class Primitive; }
namespace erhe::scene     { class Mesh; }

namespace editor {

class App_context;
class Operation;

// One channel brush stroke on one mesh primitive's corner_color_0
// (doc/editor/tools.md "Channel painting"): begin, any number of dabs, then
// end as one undoable operation or cancel. Dabs write the geometry and patch
// the GPU vertex buffer live.
class Channel_paint_stroke
{
public:
    explicit Channel_paint_stroke(App_context& context);
    ~Channel_paint_stroke() noexcept;

    // Returns the reason when the primitive cannot be painted; the stroke is then not active.
    [[nodiscard]] auto begin(
        const std::shared_ptr<erhe::scene::Mesh>& mesh,
        std::size_t                               primitive_index,
        const Channel_brush_settings&             settings
    ) -> std::optional<std::string>;
    // center is in world space.
    void dab(const glm::vec3& center);
    // The stroke's Paint_colors_operation, null when no corner changed; the caller queues it.
    [[nodiscard]] auto end() -> std::shared_ptr<Operation>;
    // Writes the values before the stroke back and queues nothing.
    void cancel();

    [[nodiscard]] auto is_active             () const -> bool;
    [[nodiscard]] auto get_changed_vertex_count() const -> std::size_t;
    [[nodiscard]] auto get_settings          () -> Channel_brush_settings&;
    [[nodiscard]] auto get_mesh              () const -> const std::shared_ptr<erhe::scene::Mesh>&;
    [[nodiscard]] auto get_primitive_index   () const -> std::size_t;

private:
    void write_corner(GEO::index_t corner, const glm::vec4& color);
    void release();

    App_context&                                m_context;
    std::shared_ptr<erhe::scene::Mesh>          m_mesh;
    std::size_t                                 m_primitive_index{0};
    std::shared_ptr<erhe::primitive::Primitive> m_primitive;
    std::shared_ptr<erhe::primitive::Primitive> m_hold;
    std::shared_ptr<erhe::geometry::Geometry>   m_geometry;
    Channel_brush_settings                      m_settings;
    Channel_brush_stroke                        m_stroke;
    std::vector<glm::vec3>                      m_positions;            // per vertex, world space
    std::vector<glm::vec4>                      m_values;               // per vertex
    std::vector<GEO::index_t>                   m_vertex_corner_offsets; // vertex v owns [offsets[v], offsets[v + 1])
    std::vector<GEO::index_t>                   m_vertex_corners;
    std::vector<std::uint8_t>                   m_vertex_recorded;
    std::vector<GEO::index_t>                   m_recorded_vertices;
    std::vector<GEO::index_t>                   m_corners;              // corners of the recorded vertices
    std::vector<glm::vec4>                      m_before_colors;        // parallel to m_corners
    std::vector<std::uint32_t>                  m_changed;
};

// Builds the geometry's vertex-to-corner table when it has none (a Geometry
// loaded from a scene file), under the mesh's item host mutex. Main thread.
void ensure_vertex_corners(const erhe::scene::Mesh& mesh, erhe::geometry::Geometry& geometry);

enum class Channel_fill_mode : unsigned int
{
    fill = 0, // the masked channels take the value
    invert    // the masked channels take 1 - their value
};

// One compound operation of Paint_colors_operation over the primitives of the
// meshes that have a Geometry, every corner. Null with out_error when none has one.
[[nodiscard]] auto make_channel_fill_operation(
    App_context&                                       context,
    std::span<const std::shared_ptr<erhe::scene::Mesh>> meshes,
    unsigned int                                       channel_mask,
    Channel_fill_mode                                  mode,
    float                                              value,
    std::string&                                       out_error
) -> std::shared_ptr<Operation>;

}
