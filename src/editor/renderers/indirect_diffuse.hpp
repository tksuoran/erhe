#pragma once

#include "erhe_scene_renderer/light_buffer.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

// erhe_codegen-generated enums live in the global namespace.
enum class Indirect_diffuse_source : unsigned int;

namespace erhe::graphics {
    class Texture;
}
namespace erhe::scene {
    class Xformable;
    using Node = Xformable;
}

namespace editor {

class App_context;

// The indirect diffuse probe field a producer publishes: the DDGI atlas
// format (doc/editor/ddgi.md "Data layout") both producers write, and the
// parameters the forward pass samples it with. Invalid (no field) unless
// parameters are valid and all three atlases exist.
class Probe_field
{
public:
    erhe::scene_renderer::Ddgi_parameters    parameters{};
    std::shared_ptr<erhe::graphics::Texture> irradiance;
    std::shared_ptr<erhe::graphics::Texture> distance;
    std::shared_ptr<erhe::graphics::Texture> probe_data;
    uint64_t                                 update_count{0}; // the producer's field updates so far

    [[nodiscard]] auto is_valid() const -> bool;
};

// Whether an indirect diffuse producer (Ddgi_renderer,
// Radiance_cascades_renderer) is the selected source.
enum class Producer_selection : unsigned int
{
    deselected = 0,
    selected   = 1
};

// The selection a producer gets when current is the selected source.
[[nodiscard]] auto get_producer_selection(Indirect_diffuse_source current, Indirect_diffuse_source producer) -> Producer_selection;

// The single change site of editor_settings.indirect_diffuse_source
// (doc/editor/radiance_cascades.md "Source selection"): stores the source
// and tells both producers. The deselected one releases its textures here;
// the selected one (re)fits on its next tick. Called by the Settings, DDGI
// and Radiance Cascades window combos and the MCP tools set_indirect_diffuse
// and set_ddgi; the producers are constructed with the loaded (and
// migrated) value.
void set_indirect_diffuse_source(App_context& context, Indirect_diffuse_source source);

// The field of the selected producer (doc/editor/radiance_cascades.md
// "Source selection"); invalid while the source is ambient or the selected
// producer has no field. The single source of what Editor::tick() publishes
// to the forward pass and what the MCP irradiance query samples.
[[nodiscard]] auto get_indirect_diffuse_field(const App_context& context) -> Probe_field;

// Temporal history of a round-robin integrator: the items (radiance
// cascades raw texels, DDGI probes) are traced in a fixed cyclic order,
// each update continuing at a cursor, and each traced value is blended into
// the item's history with the hysteresis h (doc/editor/ddgi.md "History
// reset"). After a reset - an allocation, or a change message - the k-th
// trace of an item since the reset (k = 0, 1, ...) is blended with
//
//   h_k = min(h, k / (k + 1))
//
// so the first trace replaces the stale history and the following ones
// form the equal-weight mean of every trace since the reset, until that
// weight reaches h and the exponential blend takes over. Without jitter a
// static scene is exact after one full refresh; with jitter the running
// mean is the minimum-variance estimate of the new state at every k.
//
// The shaders compute k per item from get_shader_parameters() and the
// item's index (res/editor/shaders/erhe_temporal_history.glsl).
class Temporal_history
{
public:
    // Starts the history over at the item the cursor points at: the
    // allocation, or the first update after request_reset().
    void reset(int64_t item_count, int64_t cursor);
    // A change message: the next update starts the history over at its
    // cursor. Cheap; any number of requests in one frame make one reset.
    void request_reset();
    // Called once per update before its dispatches: applies a requested
    // reset at the cursor. Returns true when it did.
    auto begin_update(int64_t item_count, int64_t cursor) -> bool;
    // x = item index the reset started at, y = items traced since the reset
    // before this dispatch, z = item count, w = 1 while any item is still
    // below the hysteresis (0: the shader uses h). For a dispatch whose
    // first item is traced after `items_before_dispatch` more items of this
    // update.
    [[nodiscard]] auto get_shader_parameters(int64_t items_before_dispatch) const -> glm::uvec4;
    // After an update traced item_count_traced items with hysteresis h.
    void end_update(int64_t item_count_traced, float hysteresis);
    // Resets applied since construction (the stats report it).
    [[nodiscard]] auto get_reset_count() const -> uint64_t;

private:
    int64_t  m_item_count     {0};
    int64_t  m_origin         {0};
    int64_t  m_traced         {0};     // items traced since the reset
    bool     m_active         {false}; // some item is below the hysteresis
    bool     m_reset_requested{false};
    uint64_t m_reset_count    {0};
};

// Whether a transform change of the node moves anything the indirect
// diffuse producers trace or shade with: a content-layer mesh or a light in
// the node's subtree. A camera or tool node does not.
[[nodiscard]] auto node_affects_indirect_lighting(const erhe::scene::Node& node) -> bool;

} // namespace editor
