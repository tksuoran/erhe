#pragma once

// erhe_codegen-generated enums live in the global namespace.
enum class Indirect_diffuse_source : unsigned int;

namespace editor {

class App_context;

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

} // namespace editor
