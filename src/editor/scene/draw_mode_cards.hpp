#pragma once

#include <memory>

namespace erhe::scene { class Mesh; }

namespace editor {

class App_context;
class Draw_mode;

// Builds the card proxy of one draw-mode attachment: the generated quad
// geometry a `cards` draw mode supplies in place of the subtree it prunes
// (doc/erhe/usd_compatibility.md, "Draw modes").
//
// The result is one Mesh with one primitive per drawn face, each with its own
// unlit material - the face's `cardTexture` when it has one, the draw-mode
// color when it has none. The mesh and its materials are the attachment's
// own: they carry Item_flags::draw_mode_proxy and Item_flags::session_only,
// so the pruning does not reach them, the item tree does not list them and no
// exporter writes them. The caller places the mesh under the model prim and
// owns its lifetime.
//
// Returns null when the attachment asks for no cards or has no extent to cut
// them from.
[[nodiscard]] auto build_draw_mode_card_proxy(App_context& context, Draw_mode& draw_mode) -> std::shared_ptr<erhe::scene::Mesh>;

} // namespace editor
