#pragma once

#include "erhe_scene_renderer/light_buffer.hpp"

#include <cstdint>
#include <memory>

// erhe_codegen-generated enums live in the global namespace.
enum class Indirect_diffuse_source : unsigned int;

namespace erhe::graphics {
    class Texture;
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

} // namespace editor
