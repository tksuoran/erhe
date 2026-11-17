#pragma once

#include <memory>

namespace erhe { class Hierarchy; }

namespace editor {

class App_context;
class Scene_root;

// The Create menu's "Level Kit" submenu (U47, doc/editor/level_tools.md "Create menu"), shared by the Scene Hierarchy
// window's Create menu and the main menu's Commands > Create: per kit its shapes, and every layout kind. A chosen entry
// queues its creation under the parent (none: the scene root) of the scene. Returns true when an entry was chosen.
auto level_kit_create_menu(App_context& context, const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::Hierarchy>& parent) -> bool;

} // namespace editor
