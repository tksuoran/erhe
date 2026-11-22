#pragma once

#include "level/foliage_palette.hpp"
#include "level/foliage_sampler.hpp"
#include "level/kit_mesh_builder.hpp"
#include "level/kit_shape.hpp"
#include "level/level_kits.hpp"
#include "operations/live_edit.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace erhe { class Item_base; }

// One foliage paint or remove stroke, for the tool and the MCP tools alike (doc/editor/level_tools.md "Foliage paint").
namespace editor {

class App_context;
class Scene_root;

enum class Foliage_stroke_kind : unsigned int {
    paint = 0,
    remove
};

// F9.3: which foliage items a remove stroke may take.
enum class Foliage_remove_filter : unsigned int {
    all_shapes = 0, // every foliage item, kit piece or not
    palette_shapes  // Remove Only Palette Shapes: kit pieces of a palette entry's shape
};

// F5.5: the up axis of a placed item.
enum class Foliage_up : unsigned int {
    world = 0,     // upright along world +Y
    surface_normal // Align To Surface
};

// The tool's values at the press (FD9.1).
class Foliage_stroke_settings
{
public:
    float                 brush_radius  {2.0f}; // meters, above 0
    float                 paint_density {0.5f}; // items per square meter per stamp, >= 0
    Foliage_up            up            {Foliage_up::world};
    float                 remove_density{1.0f}; // share of the items in the brush a remove stamp takes, 0 - 1
    Foliage_remove_filter remove_filter {Foliage_remove_filter::all_shapes};
};

class Foliage_stroke_result
{
public:
    std::size_t stamps            {0};
    std::size_t attempts          {0};
    std::size_t placed            {0};
    std::size_t removed           {0}; // foliage items, each with its subtree
    std::size_t refused_by_spacing{0}; // candidates
    std::size_t without_surface   {0}; // candidates
    std::size_t on_sealed_support {0}; // candidates whose hit mesh is inside a reference instance
    bool        recorded          {false}; // the stroke's one entry was recorded (or joined an open batch group)
};

// One foliage item a remove stamp visits: its index entry and the handle the node is read back by.
class Foliage_remove_candidate
{
public:
    Foliage_index_entry   entry;
    Foliage_index::Handle handle{0};
};

class Foliage_stroke
{
public:
    explicit Foliage_stroke(App_context& context);
    ~Foliage_stroke() noexcept;
    Foliage_stroke(const Foliage_stroke&) = delete;
    auto operator=(const Foliage_stroke&) -> Foliage_stroke& = delete;

    // Checks the inputs and resolves each entry's values, variants and materials once (a remove stroke reads the
    // entries' shapes with Foliage_remove_filter::palette_shapes, else no palette); blocks undo, redo and scene saves
    // until end() or cancel(). Returns the refusal (nothing opened), empty on success.
    [[nodiscard]] auto begin(
        Scene_root&                            scene_root,
        Foliage_stroke_kind                    kind,
        const Foliage_stroke_settings&         settings,
        std::span<const Foliage_palette_entry> palette,
        int                                    seed
    ) -> std::string;

    // One stamp about the brush center across the normal (FD9.4, FD10; a remove stamp does not read the normal);
    // returns the refusal, empty on success.
    // After a refusal the stroke stays open; the caller cancels or goes on.
    [[nodiscard]] auto stamp(glm::vec3 center, glm::vec3 normal) -> std::string;

    // The brush radius of the open stroke's later stamps (the tool's Shift + wheel); above 0.
    void set_brush_radius(float radius);

    // Records the stroke's operations as one entry, none when nothing changed.
    [[nodiscard]] auto end() -> Foliage_stroke_result;

    // Undoes the stroke's operations and records nothing (FD9.3). The destructor cancels a stroke left open.
    void cancel();

    [[nodiscard]] auto is_open() const -> bool;
    // The open stroke's counts so far; after end() the last stroke's.
    [[nodiscard]] auto get_result() const -> const Foliage_stroke_result&;
    // Called after the stack abandoned the stroke (a scene close, the shutdown), its state already cleared.
    void set_abandon_callback(std::function<void()> callback);
    // The nodes the open stroke placed so far, in placement order; end(), cancel() and an abandon release them.
    [[nodiscard]] auto get_placed_nodes() const -> std::span<const std::shared_ptr<erhe::scene::Node>>;
    // The foliage nodes the open stroke removed so far, in removal order; released as the placed ones.
    [[nodiscard]] auto get_removed_nodes() const -> std::span<const std::shared_ptr<erhe::scene::Node>>;

private:
    class Entry
    {
    public:
        const Kit_shape_definition*       shape{nullptr};
        std::vector<Kit_parameter_values> variants;  // one per variant, its seed applied
        std::vector<Kit_slot_material>    materials; // by slot
        float                             radius{0.0f};
    };

    [[nodiscard]] auto resolve_entry(const Foliage_palette_entry& entry, Entry& out_entry) const -> std::string;
    [[nodiscard]] auto open_live_edit() -> std::string;
    [[nodiscard]] auto begin_remove(Foliage_remove_filter filter, std::span<const Foliage_palette_entry> palette) -> std::string;
    [[nodiscard]] auto paint_stamp(const Foliage_brush_frame& frame) -> std::string;
    [[nodiscard]] auto remove_stamp(glm::vec3 center) -> std::string;
    [[nodiscard]] auto is_removable(const erhe::scene::Node& node) const -> bool;
    void close();
    // The stack ended the stroke (a scene close, the shutdown) or cancel() did: the stroke's state is cleared.
    void on_abandoned();

    App_context&                                    m_context;
    std::shared_ptr<Scene_root>                     m_scene_root;
    Foliage_stroke_kind                             m_kind{Foliage_stroke_kind::paint};
    Foliage_stroke_settings                         m_settings;
    std::vector<Entry>                              m_entries;
    Foliage_sampler_palette                         m_sampler_palette;
    std::vector<Foliage_sampler_entry>              m_sampler_entries;
    Kit_random                                      m_random{0};
    Foliage_sampler_scratch                         m_scratch;
    std::vector<Foliage_placement>                  m_placements;
    std::vector<Kit_placement>                      m_kit_placements;
    std::vector<std::shared_ptr<erhe::scene::Node>> m_placed_nodes;
    std::vector<const Kit_shape_definition*>        m_remove_shapes;     // palette_shapes: the entries' shapes
    std::uint32_t                                   m_remove_threshold{0};
    std::vector<Foliage_remove_candidate>           m_remove_candidates; // a remove stamp's, in remove order
    std::vector<std::shared_ptr<erhe::scene::Node>> m_remove_nodes;      // a remove stamp's drawn nodes
    std::vector<std::shared_ptr<erhe::Item_base>>   m_remove_items;      // a remove stamp's drawn items, as items
    std::vector<std::shared_ptr<erhe::scene::Node>> m_removed_nodes;
    Foliage_stroke_result                           m_result;
    Live_edit_token                                 m_token; // the stack's handle of the stroke's live edit
    std::function<void()>                           m_abandon_callback;
    bool                                            m_open{false};
};

} // namespace editor
