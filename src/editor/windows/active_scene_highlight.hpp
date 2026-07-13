#pragma once

namespace editor {

class App_context;
class Scene_root;

// Active-scene window highlight: commands that target scene-hosted items act
// on the active scene (Selection::get_active_scene_root), so its windows are
// tinted to show where those commands will land. Call from
// Imgui_window::on_begin() (before ImGui::Begin, which reads the title / dock
// tab colors); returns the number of pushed style colors - pass it to
// ImGui::PopStyleColor in on_end(). Returns 0 when scene_root is null or not
// the active scene.
auto push_active_scene_window_tint(App_context& context, const Scene_root* scene_root) -> int;

}
