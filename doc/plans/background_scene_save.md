# Background scene save: outstanding work

Status: in progress

Scene saves and Prepare run on a worker with the scene read-only
(`doc/editor/scene_serialization.md` "Saving in the background";
`Scene_save_service`). Prepare (PC) and Prepare (Quest) write a LOD chain for
every prepared primitive (`doc/editor/lod.md`). This plan lists what remains.

## Open items

1. **Disabled windows are not dimmed.** During a save,
   `Imgui_windows::set_window_contents_disabled(true)` wraps each window's
   `imgui()` in `ImGui::BeginDisabled()`. Input is blocked (verified: an
   `imgui_click` on "Platonic Solids" adds no nodes while disabled, 6 when
   enabled), but the windows draw at full brightness, also when the flag is
   forced on from startup. `style.DisabledAlpha` is the ImGui default 0.6 and
   nothing in `src/` pushes `ImGuiStyleVar_Alpha`. Find why the alpha
   multiply has no visible effect (check the colors the window draw lists
   carry, `Imgui_renderer`'s shader / vertex color path) and make a running
   save visible on every window. Verify with `capture_screenshot` while a
   Bistro Prepare (PC) runs.
2. **Mcp_test.property_row_is_addressable_by_its_label fails** on Linux:
   `imgui_click` finds no "Local Transform" item in the recorded Properties
   frame. Check it against the commit before the background save and with a
   clean `config/editor/desktop_windows.json` (the user's session copy is
   modified) before treating it as a defect.
3. **KTX2 warnings at load.** Opening the Bistro source glTF and the PC file
   logs about 300 `KTX2: level 0 outside the file` / `KTX2: container parse
   failed` warnings, while the load reports every texture loaded. Find which
   reader produces them (the load-time encoded texture disk cache is a
   candidate) and fix the cause.
4. **Synchronous saves left.** `Asset_manager::save_container` of a scene
   record calls the synchronous `save_scene_gltf()`, and a USD-backed scene
   saves on the main thread inside `Scene_save_service::start()`. Move both
   onto the worker path.
5. **Scene close leak warnings.** Closing Bistro logs `scene-close leak:
   Animation '...' of closed scene ... is still alive 60 frames after close
   (3 holder(s))` for its animations; find the holders.
