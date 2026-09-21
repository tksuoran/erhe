#pragma once

#include <glm/glm.hpp>

#include <memory>

namespace erhe::scene { class Mesh; class Xformable; using Node = Xformable; }

namespace editor {

class App_context;
class Draw_mode_data;

// Builds the card proxy of one model prim: the generated quad geometry a
// `cards` draw mode supplies in place of the subtree it prunes
// (doc/erhe/usd_compatibility.md, "Draw modes"). `data` is the prim's
// effective draw-mode values and `min` / `max` the box the cards are cut
// from, both read by the caller (`Draw_mode_system::rebuild_card_proxy`,
// which has already established that the prim resolves to `cards`).
//
// The result is one Mesh with one primitive per drawn face, each with its own
// unlit material - the face's `cardTexture` when it has one, the draw-mode
// color when it has none. The mesh and its materials carry
// Item_flags::draw_mode_proxy and Item_flags::session_only, so the pruning
// does not reach them, the item tree does not list them and no exporter
// writes them. The caller places the mesh under the model prim and owns its
// lifetime.
//
// Returns null when the prim is in no scene or no face is drawn.
[[nodiscard]] auto build_draw_mode_card_proxy(
    App_context&          context,
    erhe::scene::Node&    node,
    const Draw_mode_data& data,
    const glm::vec3&      min,
    const glm::vec3&      max
) -> std::shared_ptr<erhe::scene::Mesh>;

} // namespace editor
