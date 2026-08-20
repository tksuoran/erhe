# Clearing editor references to content removed by an undo

## Context

Undoing a glTF import takes the imported content back out of the editor, but nothing
tells the editor parts that cached pointers to it. Reported by the user with the
Animation window: select an animation from an imported scene, undo the import, and the
window still shows and plays it.

There is no notification of any kind for this. `App_message_bus`
(`src/editor/app_message_bus.hpp:19-37`) has exactly one teardown channel, `close_scene`,
with 12 subscribers, each with an `on_close_scene(erhe::Item_host*)` handler that asks
"is this reference hosted/defined by the closing scene?" (AGENTS.md, *Scene-hosted
references in editor parts*). An import undo closes no scene, so none of them run.

The stale references are not merely cosmetic. `Asset_manager::unload_record`
(`src/editor/assets/asset_manager.cpp:1023-1088`) drops the container record and then
verifies exclusivity, logging
`undeclared asset user: ... (a raw shared_ptr bypassed Asset_reference)` for every asset
still alive. `Animation_window::m_animation`, `Animation_player::m_animation`,
`Properties::m_inspected_material`, `Material_preview::m_last_material`,
`Brdf_slice`'s node material, `Operations::m_make_mesh_config.material`,
`Brush_tool::m_drag_and_drop_brush`, `Create::m_brush`, `Brush_placement::m_brush` and
`Item_tree_window`'s cached rows are all raw `shared_ptr`s and land in exactly that
report.

**Scope limit, stated up front:** undo alone can never fully release the assets, and this
plan does not change that. The undo entry deliberately keeps everything for redo —
`Content_library_attach_operation::m_item` plus its adopted `m_usership`
(`content_library_attach_operation.hpp:79,84`, adopted at `:64`, *not* released in
`undo()` at `:70-74`), `Item_insert_remove_operation::m_item` (the whole detached
subtree), and `Async_raytrace_kickoff_operation::m_scene_root` / `m_mesh_node_items`
(`async_raytrace_kickoff_operation.hpp:30-31`, undo is a deliberate no-op at
`async_raytrace_kickoff_operation.cpp:230-235`). So the deliverable is: **after an undo,
the only thing keeping the content alive is the undo history**, which `unload_record`
already reports by name (`"undo stack: content library attach '<name>'"` /
`"undo/redo history (clear history to release)"`). Full release = undo + Clear History.

## Design

One notification, produced at the choke points the removals already pass through,
batched and delivered once per frame outside every held lock.

### 1. Pending-removal list on `Asset_manager`

`Asset_manager` already owns the asset bookkeeping and holds the message bus. Add:

```cpp
// Items that left a content library / an unregistered scene since the last
// flush. weak_ptr: an item that dies before the flush needs no announcement,
// and the manager must not become a holder of an asset it is about to unload.
std::vector<std::weak_ptr<erhe::Item_base>> m_pending_removals;

void note_item_detached(const std::shared_ptr<erhe::Item_base>& item); // append
void note_item_attached(const erhe::Item_base* item);                  // cancel
void flush_pending_removals();                                         // move out, then publish
```

`note_item_attached` cancelling a pending entry is what makes the hook false-positive
free: `Hierarchy::set_parent` runs `handle_remove_child` then `handle_add_child` in one
call (`src/erhe/item/erhe_item/hierarchy.cpp:203-209`), and `Hierarchy::remove()` promotes
a folder's children to the grandparent before orphaning itself (`hierarchy.cpp:95-98`), so
folder moves, cross-library moves and folder removal all cancel themselves within the
frame. No path detaches in frame N and re-attaches in frame N+1.
(`remove_all_children_recursively` → `recursive_remove` orphans every descendant with no
re-attach, `hierarchy.cpp:105-119` — those items really are removed, so announcing them is
correct; its only editor caller is the ownerless preview library, which notifies nothing.)

`flush_pending_removals()` must **move** the pending vector into a local before sending,
not clear it afterwards: a handler that appends during the sync dispatch would otherwise
have its append silently discarded.

`flush_pending_removals()` returns immediately when the list is empty — that is what keeps
it clear of AGENTS.md *No "update each frame" patterns* (`AGENTS.md:551`); the producers
are change-driven and the flush is a no-op in the steady state. It must never be reachable
from a subscriber: `Message_bus::send_message` holds its non-recursive `m_receivers_mutex`
across every handler invocation (`src/erhe/message_bus/erhe_message_bus/message_bus.hpp:61-76`).
Single call site, in `Editor::tick`.

### 2. Producers

- **Content library entries** — `release_host_for_subtree`
  (`src/editor/content_library/content_library.cpp:96-112`) and its mirror
  `claim_host_for_subtree` (`:71-89`). These are the single choke point for every library
  removal: `Content_library_node::remove()` (`content_library.hpp:459-480`) and
  `remove_all_children_recursively` both reach `Content_library_node::handle_remove_child`
  (`content_library.cpp:143-155`) → `release_host_for_subtree`. That is the undo path of
  all ten `Content_library_attach_operation` flavours the import compound builds
  (textures, materials, skins, animations, physics materials, collision filters, physics
  joints, brushes, `Graph_texture`, `Graph_mesh` — built at `gltf.cpp:311,334,357,377`,
  `gltf_physics_import.cpp:352,376,433`, `gltf_extensions_import.cpp:396,449,480`).
  The new `note_item_detached` / `note_item_attached` calls go **outside** the existing
  `manager_owned` condition (`content_library.cpp:103-109`):
  `Asset_manager::on_library_node_detached` early-returns for anything that is not
  brush/material/animation (`asset_manager.cpp:1691-1695`, `is_manager_owned_asset_type`
  at `asset_key.cpp:127-130`), which would miss the `Graph_mesh` / `Graph_texture` targets
  the graph windows hold.

  **Decision:** route the note off the library's manager pointer (the existing parameter),
  not off `owner` — no new plumbing, and the two resulting silences are both benign.
  Document them: `handle_remove_child` only notifies when the library has an owner
  (`content_library.cpp:150-157`), so ownerless libraries (material preview, tool scene,
  `Scene_builder`'s palette) stay quiet — nothing outside them points at their items; and
  `on_scene_unregistered` nulls the library's manager pointer
  (`asset_manager.cpp:1512-1517`), so an already-unregistered scene's later library
  removals are silent — that scene's assets were already announced wholesale by the
  `on_scene_unregistered` producer below.

- **Scene nodes** — `Item_insert_remove_operation` (`item_insert_remove_operation.cpp`),
  on the two directions that take the item *out* of the scene: undo of `Mode::insert`
  (which glTF import uses, `gltf.cpp:1030-1073`) and execute of `Mode::remove`. Nodes have
  no editor-level detach hook to hang this on, and this also makes an ordinary node delete
  clear the Properties target.

  **Snapshot timing is load-bearing.** In `Mode::remove`, `execute()` first re-parents
  every non-bone-proxy child to the grandparent through `m_parent_changes`
  (`item_insert_remove_operation.cpp:104-129`) and only then orphans `m_item` (`:130`).
  Walking the subtree from `m_item` before that point would announce nodes that are still
  in the scene and make every subscriber drop a live node. Collect the subtree
  **immediately around the `set_parent` call**, guarded by mode so an *insert* is never
  announced: in `execute()` under `if (m_mode == Mode::remove)` after the
  `m_parent_changes` loop and before `set_parent` (`:130`), and in `undo()` under
  `if (m_mode == Mode::insert)`. Note the `undo()` point is necessarily *after* its
  `set_parent` (`:144`) and before the `m_parent_changes` undo loop (`:146-149`); that
  still yields the right subtree only because `Skin_registered_message` is queued, not
  sync (`app_message_bus.hpp:34`, sent from the unregister direction, `scene_root.cpp:1287-1295`), so
  `Bone_visualization::remove_skin_proxies` (`bone_visualization.cpp:425-440`) has not yet
  detached the proxies. If that ever becomes synchronous, snapshot before `set_parent`.
  Bone proxies are deliberately excluded from `m_parent_changes` (`:52-54`) and do leave
  with the item, so they are announced — that is correct.

- **`Scene_open_operation::undo`** — the second route to the reported symptom, and the one
  the library hook alone does *not* cover: it only calls `unregister_from_editor_scenes` +
  `remove_browser_window` (`scene_open_operation.cpp:107-112`), never undoes the inner
  import compound and never publishes `Close_scene_message`, so "Open Scene, select an
  animation, Ctrl+Z" leaves the Animation window pointing into an unregistered scene. Hook
  the *existing* `Asset_manager::on_scene_unregistered` (`asset_manager.cpp:1506-1546`,
  reached from `App_scenes::unregister_scene_root`, `app_scenes.cpp:88`): the record
  survives unregistration with its `Scene_entries` intact (`asset_manager.cpp:1531-1539`),
  so `visit_record_assets` can enumerate every asset into the pending list. This also
  fires on a normal scene close, where it is harmlessly redundant with the `close_scene`
  handlers. Redo re-registers and re-arms the record (`asset_manager.cpp:1345-1356`) but
  does not restore the Animation window's selection — intended, not a regression; say so
  in the doc. This producer announces the record's **assets** only, so a Properties
  inspector targeting a *node* of the unregistered scene stays stale on this route;
  consistent with the asset scope above, and worth one sentence so the next reader does
  not assume node coverage.

### 3. Delivery

New message in `src/editor/app_message.hpp`, registered `Dispatch_policy::sync_only`:

```cpp
struct Removed_items // struct, matching every other payload in this header
{
    std::unordered_set<const erhe::Item_base*>    lookup; // membership test
    std::vector<std::shared_ptr<erhe::Item_base>> owners; // keeps them alive for the dispatch
};

// Published once per frame for content taken out of the editor without a
// scene closing - undo of a glTF import removes every imported asset from the
// content library and every imported node from the scene. Same contract as
// close_scene (AGENTS.md "Scene-hosted references in editor parts"): a part
// that caches a reference to editor content must drop it when this message
// names that item, or the item survives as an undeclared user in
// Asset_manager::unload_record's exclusivity check. Handlers do a lookup
// against the set ONLY - no manager lookups, no linear scans; undoing a large
// import announces thousands of items at once. The payload is shared because
// Message_bus::send_message takes the message BY VALUE.
struct Items_removed_message
{
    std::shared_ptr<const Removed_items> removed;
};
```

`Asset_manager::flush_pending_removals()` locks the surviving weak pointers into a fresh
`Removed_items`, sends one message and clears the list. Called from `Editor::tick`
immediately before `m_app_message_bus->update()` (`editor.cpp:757`) — i.e. after
`m_operation_stack->update()` (`editor.cpp:735`) so an undo performed this frame is
announced this frame, after `m_imgui_windows->end_frame()` (`editor.cpp:709`) so it is
outside ImGui iteration, and outside `Content_library::mutex`
(`content_library_attach_operation.hpp:72`) and `Item_host_lock_guard`
(`item_insert_remove_operation.cpp:99,138`), both non-recursive and both held by the
producing operations. The one tick path that skips the flush is the Android
swapchain-unavailable early return (`editor.cpp:578-581`); harmless, the weak list simply
carries to the next tick.

### 4. Subscribers

Each drops the references its `on_close_scene` handler drops, keyed on identity via
`message.removed->lookup.contains(ptr)`.

| Part | Reference to drop |
|---|---|
| `animation/animation_window.cpp` | `m_animation` → `set_animation({})` (also clears the player) — **the reported bug** |
| `animation/animation_player.cpp` | `m_animation` (MCP-driven target) |
| `windows/properties.cpp` | `m_target` / `m_target_items` / `m_inspected_material` |
| `windows/item_tree_window.cpp` | `m_hovered_item` / `m_popup_item` (`item_tree_window.hpp:180-181`). A *rendering* tree resets both every frame (`item_tree_window.cpp:1746`, and `:1947-1948` when the popup is not open), so the pin is specifically **hover-then-stop-rendering**: once the window is hidden or closed, nothing clears them and the removed node or brush stays pinned. Plus the `Flat_row` cache (`shared_ptr<Item_base> item` **and** `shared_ptr<Brush> brush`, `:118-119`): a *visible* tree rebuilds it only when the mutation serial moves (`item_tree_window.cpp:1851-1867`), so call the existing `clear_cached_rows()` (`item_tree_window.hpp:69`) rather than re-describing it. (A hidden tree is already safe: `Item_tree_window::hidden()` calls `clear_cached_rows()` at `item_tree_window.cpp:2015-2020`, and `Imgui_windows` calls `hidden()` every frame for every non-visible window, `src/erhe/imgui/erhe_imgui/imgui_windows.cpp:288-290`.) These members are `private` on **`Item_tree`** (`item_tree_window.hpp:85`), so the handler is a new `Item_tree` method and the subscription belongs on `Item_tree` — `Asset_browser_window` inherits `Item_tree_window` (`asset_browser/asset_browser.hpp:149`) and needs the same cleanup |
| `editor.cpp` | `Geometry_graph_window` / `Texture_graph_window` targets incl. the extra windows (factor the existing `on_close_scene` body at `:3313-3332` into a shared helper), and Selection pruning (below) |
| `brushes/brush_tool.cpp` | `m_drag_and_drop_brush`, `m_active_brush` (`Asset_reference` → `set_key({})`) |
| `tools/material_paint_tool.cpp` | `m_material` (`Asset_reference`) |
| `preview/material_preview.cpp` | `m_last_material` |
| `content_library/brdf_slice.cpp` | the node material |
| `operations/operations_window.cpp` | `m_make_mesh_config.material` |
| `create/create.cpp`, `brushes/brush_placement.cpp` | `m_brush` — neither subscribes to `close_scene` today; both are raw-`shared_ptr` gaps |
| `physics/physics_tool.cpp` | `m_last_target_mesh` (node-removal half) |

**Selection.** The node path needs nothing: `Item_insert_remove_operation::undo` already
restores the pre-import snapshot via `context.selection->set_selection(m_selection_before)`
(`item_insert_remove_operation.cpp:151`). What is uncovered is `Scene_open_operation::undo`
(touches Selection not at all, `scene_open_operation.cpp:107-112`) and content-library
items (materials, brushes) sitting in the selection. Prune in **one** batch — build the
filtered vector and call `set_selection(filtered)` (or wrap one outer
`Scoped_selection_change`). Do **not** loop `Selection::remove_from_selection`
(`selection_tool.cpp:1102-1120`): each call opens its own `Scoped_selection_change` whose
`end_selection_change` sorts the whole selection twice and dispatches a sync
`Selection_message` (`selection_tool.cpp:749-775`).

Deliberately **not** wired, and to be stated in the doc: `Hotbar` / `Inventory_window`
slots and `Clipboard` pin their items on purpose through `Asset_reference` (persistent
inventory); they are declared users and already surface as named unload refusals.
`Bone_visualization` and `Operations::m_save_confirm_scene_root` key on `Scene_root` and
need nothing item-keyed.

`Properties::m_inspected_material` needs an explicit decision rather than a copy of the
close-scene handler: that one drops a dirty edit session justified by *"the close drops
the undo history anyway"* (`properties.cpp:125-126`), which is **not** true here. Drop the
reference and reset the state, and warn when `m_material_state != clean`, so a discarded
in-progress material edit is visible instead of silent.

### 5. Documentation

Extend AGENTS.md *Scene-hosted references in editor parts* (`AGENTS.md:518-541`): a cached
reference must handle both its scene closing **and** its item being removed by an undo,
naming `Items_removed_message`. Without this the next part added repeats the bug.

## 6. Testability seam and thread contract

The bookkeeping is extracted into a standalone class so it is unit-testable without an
editor: `Pending_item_removals` (`src/editor/assets/pending_item_removals.{hpp,cpp}`).

```cpp
class Pending_item_removals
{
public:
    void note_detached(const std::shared_ptr<erhe::Item_base>& item); // dedups
    void note_attached(const erhe::Item_base* item);                  // cancels a pending detach
    [[nodiscard]] auto empty() const -> bool;
    [[nodiscard]] auto take() -> std::shared_ptr<const Removed_items>; // moves out; null when empty
};
```

`Asset_manager` owns one; `flush_pending_removals()` is
`if (auto removed = m_pending_removals.take()) { send(...); }` — `take()` moving the state
out *before* the send is what makes a handler's re-entrant `note_detached` land in the
next batch instead of being discarded. (§3's "sends one message and clears the list" is
loose wording for this; the move-out ordering is the contract.)

Two distinct re-entrancy hazards, both handled here rather than by a test that
manufactures a re-entrant subscriber (no editor part is one):

- A subscriber calling `note_detached` during dispatch — **safe by construction** given
  the move-out, whose ordering unit case 7 pins.
- A subscriber reaching `flush_pending_removals` — would **deadlock** on the
  non-recursive `m_receivers_mutex` that `send_message` holds across every handler
  (`src/erhe/message_bus/erhe_message_bus/message_bus.hpp:61-76`). Guard it with an
  `ERHE_VERIFY(!m_in_flush)` scope flag so a future subscriber that does this fails
  loudly instead of hanging.

**Thread contract, stated because glTF loading is now asynchronous (`768113501`):**
`Pending_item_removals` is **main thread only**, like the rest of `Asset_manager`
(`verify_main_thread()`, `asset_manager.cpp:406,1507`). Every producer already runs there
— operations run from `Operation_stack::update()`, library mutation is main thread — so
the class carries no mutex and instead asserts the contract with the same
`verify_main_thread()` pattern. Operation execute/undo is already main-thread verified
(`operation_stack.cpp:80-87`), `on_scene_unregistered` verifies too, and
`assets/gltf_load_task.cpp` touches no `Content_library`. One residual path to note:
`~Content_library` calls `release_host_for_subtree` (`content_library.cpp:213-222`) and a
library could in principle be destroyed on a worker when the last `shared_ptr` drops
there — safe only because the manager pointer is normally already disarmed by then, which
is a further reason to route the note off that pointer (§2).

## 7. Tests

**Implementation is not done until every item below passes.**

### 7.1 Unit tests — new `editor_asset_tests` target

New `src/editor/assets/test/{CMakeLists.txt,main.cpp,test_pending_item_removals.cpp}`.

Wiring, which is more than "link `erhe::item`":

- `add_subdirectory(assets/test)` in `src/editor/CMakeLists.txt`, next to the existing
  `if (${ERHE_BUILD_TESTS}) add_subdirectory(mcp/test) endif()` at file scope
  (`src/editor/CMakeLists.txt:1150-1152`).
- This is the first test target compiling *editor* sources: build
  `src/editor/assets/pending_item_removals.cpp` directly into the test executable
  (`mcp_server_tests` links no erhe code at all, `src/editor/mcp/test/CMakeLists.txt:19-24`,
  so it is no precedent).
- `target_include_directories(${_target} PRIVATE ${CMAKE_SOURCE_DIR}/src/editor)` — the
  seam header includes `app_message.hpp` for `Removed_items`, and `src/editor` is only on
  the editor target's include path. `app_message.hpp:1-5` is forward declarations plus
  `<filesystem> <memory> <vector>`, so this adds no link dependency — but `Removed_items`
  needs `<unordered_set>` added there.
- Carry the full boilerplate from `src/erhe/item/test/CMakeLists.txt:1-9,37-38`:
  `CPMAddPackage(googletest)`, `include(GoogleTest)`, `gtest_discover_tests`. It would
  happen to work without the CPM block if `assets/test` is added after `mcp/test`
  (`src/editor/CMakeLists.txt:1150-1152`, which does its own), but do not rely on
  ordering.
- Copy `src/erhe/item/test/main.cpp` verbatim for the logger bootstrap
  (`erhe::log::initialize_log_sinks()` → bootstrap `erhe::file::log_file` →
  `erhe::item::initialize_logging()`) and its link list
  (`erhe::item erhe::file erhe::log erhe::verify GTest::gtest`,
  `src/erhe/item/test/CMakeLists.txt:26-33`). `erhe::item::log` is a null global otherwise,
  and `hierarchy.cpp` dereferences it unconditionally (`hierarchy.cpp:91,186,210,241,246`)
  — the same trap that crashed the animation tests earlier in this session.

Cases:

1. `note_detached` then `take` announces the item, present in both `lookup` and `owners`.
2. `note_detached` + `note_attached` on the same item → `take` returns null (the
   folder-move cancellation).
3. `note_detached` twice for one item → **`owners.size() == 1`**. Asserting on `lookup`
   alone would be tautological — it is an `unordered_set` and cannot fail; `owners` is a
   vector and holds two entries unless `note_detached` dedups.
4. `note_attached` for an item never detached → no effect, no crash.
5. An item whose last external owner dies before `take` → not announced, and `take` does
   not resurrect it (the `weak_ptr` contract).
6. `take` on an untouched instance returns null; after a batch it empties the list and an
   immediate second `take` returns null.
7. `note_detached` after `take` is announced by the following `take` — plain FIFO
   behaviour. **This case does not cover the re-entrancy hazard:** with the seam, `take()`
   does no sending, so the hazard is unobservable here. See §6 — both hazards are handled
   by construction and by an `ERHE_VERIFY` guard, and no smoke case manufactures one.
8. Mixed batch: three detached, one re-attached → exactly two announced.
9. Invariants: `owners.size() == lookup.size()`, every `owners` entry present in `lookup`.
10. `note_attached` *after* a `take` does not retroactively alter the taken batch.

### 7.2 MCP additions

All take explicit arguments with fixed defaults and consume no UI-widget state
(`doc/mcp_api_guidelines.md`); *reporting* UI state is permitted, as `get_transform_state`
already does. Each needs **both** a handler in the dispatch table
(`src/editor/mcp/mcp_server.cpp:500-640`) **and** a schema entry in
`config/editor/mcp_tools.json`. `validate_tool_list_against_dispatch()` does log a
handler missing from the JSON (`mcp_server_tool_list.cpp:132-148`) as well as the reverse
(`:149-171`), but `refresh_tool_list()` advertises only from the JSON plus registered
commands (`:174-190`), so a handler with no JSON entry is not advertised in `tools/list`.
The asset test hooks already in the table (`acquire_asset` / `release_asset` /
`unload_asset`, `mcp_server_assets.cpp:1-6`) are the precedent.

| Tool | Kind | Why it is needed |
|---|---|---|
| `get_editor_references` | query, no args | **The direct observable for this whole feature**, and the general debugging tool for the "Scene-hosted references" bug class. Reports every cached content reference §4 wires, each as `{name, uid, type}` or null |
| `move_library_item` | action `{item, uid, destination_folder}` | Drives the detach-then-attach false-positive case. `copy_library_item` exists (`mcp_server.cpp:518`); no move variant exists under any name |
| `debug_set_item_tree_hover` | action `{tree, item, uid, clear}` | `m_hovered_item` / `m_popup_item` are set only by ImGui interaction, so the pin they cause is otherwise untestable headless. Explicit-args test hook, same category as `acquire_asset` |

`get_editor_references` additionally reports three counters, without which three of the
smoke cases below have nothing to assert on:

- `items_removed_announcement_count` and `last_announced_uids` — the only way to observe
  that a message *was* or *was not* published. Case 13 asserts that **no further**
  announcement follows a known one (an absence otherwise indistinguishable from "the
  subscriber was never wired"); case 14 asserts the announced content.
- `selection_change_count` — a monotonic counter incremented per `Selection_message`
  dispatch, so case 11 can assert the batching requirement. `Selection` has no serial
  today (`selection_tool.hpp`) and `get_selection` reports contents only.

**No `clear_operation_history` tool is needed** — `clear_undo_history` already exists
(`mcp_server.cpp:468`, `mcp_server_scene_query.cpp:1533-1550`, advertised at
`config/editor/mcp_tools.json:350-357`) and returns `dropped_count`. Case 16 calls it.

**Accessor work these imply — `get_editor_references` is not a pure additive query.**
Only `Animation_window::get_animation()` (`animation_window.hpp:56`),
`Animation_player::get_animation()` (`animation_player.hpp:36`) and
`Brdf_slice::get_material()` (`brdf_slice.hpp:41`) exist today. Getters are needed for
`Brush_tool::m_active_brush` / `m_drag_and_drop_brush` (`brush_tool.hpp:157-158`),
`Material_paint_tool::m_material` (`material_paint_tool.hpp:109`),
`Material_preview::m_last_material` (`material_preview.hpp:45`), `Create::m_brush`
(`create.hpp:58`), `Properties::m_target` / `m_target_items` / `m_inspected_material`
(`properties.hpp:138-142`), `Physics_tool::m_last_target_mesh` (`physics_tool.hpp:113`),
`Operations::m_make_mesh_config.material`, `Item_tree::m_hovered_item` / `m_popup_item`
(`item_tree_window.hpp:180-181`; `get_hovered_item()` is only `protected` at `:81`), and a
cached-row **count** on `Item_tree` (the `Flat_row` vector is private,
`item_tree_window.hpp:96-130`, with only `clear_cached_rows()` public at `:69`).

Two reachability problems must be solved rather than assumed:

- **`Properties` is not in `App_context`** (`Node_properties_window` at
  `app_context.hpp:250` is a different class). Instances live in
  `Editor_windows::m_properties_windows`, private with no accessor
  (`editor_windows.hpp:107`; only the extra-graph-window getters are public, `:87-88`).
  Add `Editor_windows::get_properties_windows()`.
- **`Item_tree` instances have no registry.** They are owned by four unrelated places —
  `Scene_root::m_node_tree_window` (`scene_root.hpp:344`),
  `Tools::m_content_library_tree_window` / `m_tool_scene_browser` (`tools.hpp:102-103`),
  `Asset_browser::m_node_tree_window` (`asset_browser.hpp:219`), and
  `Editor::m_default_scene_browser`, which is not in `App_context` at all. Add one: each
  `Item_tree` registers itself in its constructor and unregisters in its destructor under
  a stable label. The registry is needed regardless — §4 puts the subscription on
  `Item_tree`, and `Asset_browser_window` inherits `Item_tree_window`
  (`asset_browser.hpp:149`) — and it is what gives `debug_set_item_tree_hover` a `tree`
  argument to resolve. Confirm during implementation what label `Item_tree` can supply
  (`Item_tree_window` has a title / ini label; a bare `Item_tree` may need one added).
- **`Brush_placement` is not an editor part** — it is a `Node_attachment`
  (`brush_placement.hpp:15`) with no singleton, so a single `brush_placement.brush` field
  is meaningless. Drop it from `get_editor_references`, and keep it in §4's subscriber
  table only if implementation identifies a concrete owner; otherwise drop that row too.

Already reachable, no work needed: `undo` **and** `redo` are both registered commands
(`operation_stack.cpp:22-26,37-51,67-68`) and `dispatch_tool_call` falls through to
`execute_command` (`mcp_server.cpp:640-647`); `Undo_command::try_call` consults only
`can_undo()` with no host or enabled gating, so it is not a headless no-op, and MCP
handlers already marshal to the main thread (`editor.cpp:695`). `unload_asset` returns
`undeclared_survivors` (`mcp_server_assets.cpp:204`). `get_scene_animations` reports the
player target by **name** (empty string when null) via `playback_state_json`
(`mcp_server_animation.cpp:75-88`) — leave it alone and let `get_editor_references` be the
single uid-bearing source.

### 7.3 Smoke tests — `scripts/undo_reference_clearing_smoke_test.py`

Style of `scripts/geometry_nodes_smoke_test.py:35-52` / `scripts/test_editor_mcp.py:31-50`:
JSON-RPC POST to `http://127.0.0.1:8080/mcp` against an **already-running** editor (neither
script launches one). Test asset `res/editor/assets/RiggedFigure/RiggedFigure.glb` —
tracked, 22 nodes, 1 skin, 1 animation with 13 channels. Cases:

1. **Reported repro, route 1.** `create_scene` → `import_gltf` → `set_animation_target`
   → `get_editor_references` shows the animation on window *and* player → `undo` →
   both null.
2. **Reported repro, route 2.** `open_scene(RiggedFigure.glb)` → `set_animation_target`
   → `undo` → both null (the `Scene_open_operation` path, which no `close_scene`
   subscriber covers today).
3. **Redo is not a resurrection.** After case 1, `redo` → scene content back and the
   animation listed again by `get_scene_animations`, while the window target stays null
   (documented behaviour, asserted so it cannot silently change).
4. **Node references.** Import → select an imported node so `properties.target` is set →
   `undo` → target null.
5. **Snapshot timing — both halves in one run.** An editor delete removes the *whole*
   subtree: `Selection::delete_items` collects recursively and emits one
   `Item_insert_remove_operation{Mode::remove}` per collected item
   (`selection_tool.cpp:519-539,554-587`), and MCP `delete_nodes` routes there
   (`mcp_server_scene_action.cpp:297`), so a plain "delete A with children B, C" leaves A
   childless and never reaches `m_parent_changes` at all. Use a **locked** child instead:
   `collect_item` skips `is_lock_edit()` items and their subtrees
   (`selection_tool.cpp:521-523`) while `execute()` still rebuilds `m_parent_changes` from
   the live children and promotes them (`item_insert_remove_operation.cpp:104-124`). So:
   A with children B (`lock_items` B) and C → one Properties target on **A**, another on
   **B** → `delete_nodes(A)` → C gone, B promoted to A's parent with its reference
   **kept**, A's reference **cleared**. The positive half is what makes this case able to
   fail on a missing or mis-moded implementation; the negative half alone passes on an
   empty one.
6. **Bone proxies.** RiggedFigure is skinned; bone proxies leave with the item
   (`item_insert_remove_operation.cpp:52-54`) and must be announced. Assert on
   `last_announced_uids` rather than on a held reference: proxies carry `bone_proxy` and
   no `show_in_ui` (`bone_visualization.cpp:292-297`), so there is no headless way to
   point a Properties target at one.
7. **False positive.** `move_library_item` on a material held by `material_paint_tool` →
   the reference survives (detach+attach cancels within the frame).
8. **Tree-window pin.** The pin only exists once the tree stops rendering — a visible
   tree resets `m_hovered_item` every frame (`item_tree_window.cpp:1746`) and
   `m_popup_item` whenever the popup is closed (`:1947-1948`), and a hidden one already
   calls `clear_cached_rows()` (`:2015-2020`) — which does **not** touch hover/popup — so
   asserting on a rendering window passes with or without the fix. Order matters: each
   MCP call is a separate frame (handlers marshal to the main thread, `editor.cpp:695`),
   so hovering first and hiding second lets the intervening rendered frame wipe the
   hover. Sequence: `set_window_visibility{visible:false}` (`mcp_server.cpp:460`) **→**
   `debug_set_item_tree_hover` on an imported row → `undo` → `get_editor_references`
   reports hovered/popup null. Hover set while hidden genuinely persists
   (`imgui_windows.cpp:289-291` calls only `hidden()` for a non-visible window).
9. **Graph window targets.** `create_graph_mesh` / `create_graph_texture` in an imported
   library, `open_geometry_graph_window` / `open_texture_graph_window` for an extra
   window, `set_*_graph_target` → `undo` → all targets null, primaries **and** extras.
   This also guards §4's `Editor::on_close_scene` shared-helper refactor.
10. **The otherwise-untested subscribers.** One assertion each that the reference
    **clears**: `brush_tool.active_brush` and `brush_tool.drag_and_drop_brush`
    (`brush_tool.hpp:157-158`), `material_paint_tool.material`
    (`material_paint_tool.hpp:109`), `material_preview.last_material`, `brdf_slice`
    material, `operations.make_mesh_material`, `create.brush`,
    `physics_tool.last_target_mesh`. Case 7 only asserts `material_paint_tool` *survives*
    a move, which is the opposite assertion. `set_tool_asset` (`mcp_server.cpp:532`) arms
    only two of these: its `tool` argument accepts `brush` and `material_paint` only
    (`mcp_server_assets.cpp:224-228`), reaching `Brush_tool::set_active_brush` and
    `Material_paint_tool::set_material`. `m_drag_and_drop_brush` is assigned in exactly
    one place, the public `Brush_tool::preview_drag_and_drop`
    (`brush_tool.cpp:485-492`, `brush_tool.hpp:110`), so **extend `set_tool_asset` with a
    third `tool` value** (`brush_drag_and_drop`) rather than leaving that reference
    unarmed headless. Schema entry in `config/editor/mcp_tools.json` updated to match.
11. **Selection pruning.** Put a content-library material and a brush in the selection,
    take the `Scene_open_operation::undo` route → `get_selection` no longer lists them.
    Assert the *batching* requirement via `selection_change_count` (§7.2): it must
    advance by exactly one, so a future N-call `remove_from_selection` loop regresses
    visibly (`selection_tool.cpp:749-775,1102-1120`).
12. **Scene_open asset-only scope.** On route 2, a Properties target on a *node* of the
    unregistered scene stays stale (§2, documented). Assert it, so the documented limit
    cannot silently flip in either direction.
13. **Deliberate silence.** Drive `close_scene` (`mcp_server.cpp:523`) on a scene whose record actually holds assets — the route-2 `open_scene(RiggedFigure.glb)` scene — since closing an empty one leaves the counter at zero and the assertion below would fail for a *correct* implementation. The close unregisters
    the scene and therefore *does* fire the `on_scene_unregistered` producer:
    `items_removed_announcement_count` (§7.2) must advance by **exactly one** — the
    wholesale record announcement — and must not advance again on later frames for the
    library removals that follow, because `on_scene_unregistered` has by then nulled the
    library's manager pointer (`asset_manager.cpp:1512-1517`). "Must not advance at all"
    would be unsatisfiable. Without that counter an absence is indistinguishable from an
    unwired subscriber. The
    other documented silence, the ownerless library (`content_library.cpp:150-157`), has
    no headless driver — no MCP tool removes a content-library item; only
    `copy_library_item` (`:518`) and the new `move_library_item` touch libraries — so it
    stays documented-only, not asserted.
14. **Announcement content.** After route 1, `last_announced_uids` contains the imported
    animation and material uids — pins the producer independently of whether any
    subscriber happened to hold them.
15. *(re-entrancy: see §6 — the hazard is prevented by construction and by an
    `ERHE_VERIFY` guard, and there is no editor part that re-enters, so no smoke case
    manufactures one.)*
16. **Truly released.** Import → set tool assets → `undo` → `clear_undo_history` →
    `unload_asset` on the container → assert `ok == true` and
    `undeclared_survivors == 0`. This is the user's "assets are truly unloaded"
    criterion, and it fails today.
17. **No regression on close.** `close_scene`, then advance a defined number of frames —
    the leak watchdog is frame-delayed (`editor.cpp:3363` queues, `:3468,3520,3527`
    report) — and grep only the bytes appended since a baseline offset taken **before**
    the close. `logs/log.txt` is cumulative, so a whole-file grep is either vacuous or a
    false failure inherited from an earlier case in the same run.

### 7.4 C++ integration tests — extend `mcp_server_tests`

Add cases 1, 2, 5 and 16 to `src/editor/mcp/test/mcp_server_tests.cpp` — the two repro
routes, the snapshot-timing guard and the release criterion — so they run under `ctest` /
`scripts/run_mcp_tests.ps1` (which launches the editor, runs the suite and tears it down,
`run_mcp_tests.ps1:1-90`) rather than only from the Python script.

### 7.5 Build and run gate

- Editor: `build_vs2026_vulkan` Debug builds clean.
- Unit / integration: configure with `scripts/configure_tests_asan.bat` — AGENTS.md
  (`:148-155`) makes the ASAN tree the one for correctness runs, `configure_tests.bat`
  being the no-ASAN timing tree. `ctest --test-dir build_tests_asan -C Debug` green,
  including the new `editor_asset_tests`.
- `pwsh scripts/run_mcp_tests.ps1` green — note its default is
  `-BuildDir build_vs2026_opengl` (`run_mcp_tests.ps1:11`), so pass the tree actually
  built.
- `python scripts/undo_reference_clearing_smoke_test.py` green against a running editor.
- Interactive sanity pass in the real editor UI, covering the reported repro **and** the
  one case that cannot be driven headless: `Properties::m_inspected_material`. Its dirty
  flag is only set through `use_state(&m_material_state)` inside
  `Properties::material_properties` (`properties.cpp:1729`), i.e. by live ImGui item
  activity, and `m_inspected_material` is assigned only in that same render path
  (`:1712-1722`); MCP `edit_material` mutates `Material::data` directly and never marks
  the session dirty. So: inspect a material, edit it in the UI to dirty it, undo the
  import, and confirm the reference clears **and** the warning is logged (§4) — the one
  place this plan deliberately diverges from the close-scene handler.

---

# Implementation record

Implemented and verified. This section records where the implementation
departed from the plan above, and why - the plan text is left as written so the
reasoning stays readable.

## What was built

- `Removed_items` / `Items_removed_message` (`src/editor/app_message.hpp`),
  registered `sync_only` on the bus.
- `Pending_item_removals` (`src/editor/assets/pending_item_removals.{hpp,cpp}`) -
  the seam, owned by `Asset_manager`, with `note_item_detached` /
  `note_item_attached` / `flush_pending_removals` and the
  `ERHE_VERIFY(!m_in_flush)` guard.
- Producers: the content-library detach walk (`release_host_for_subtree`, for
  every entry type), `Item_insert_remove_operation` (both removing directions,
  snapshotting around `set_parent`), and `Asset_manager::on_scene_unregistered`.
- Delivery from `Editor::tick`, immediately before the message bus pump.
- Subscribers: `Animation_window`, `Animation_player`, `Properties`,
  `Item_tree`, `Editor` (graph editor targets), `Brush_tool`,
  `Material_paint_tool`, `Material_preview`, `Brdf_slice`, `Operations`,
  `Create`, `Physics_tool`, `Selection`.
- MCP: `get_editor_references`, `move_library_item`,
  `debug_set_item_tree_hover`, plus the accessors and the `Item_tree` registry
  they need.

## Departures from the plan

- **`Selection` owns its own cleanup, and had to.** The plan put selection
  pruning in `Editor::on_items_removed`. It moved to
  `Selection::on_items_removed`, because pruning the selection alone was not
  enough: `Operations` re-resolves `get_last_selected<Material>()` into
  `m_make_mesh_config.material` every frame, and `m_last_selected_by_type` is
  weak but a removed item stays *alive in the undo history for redo* - so the
  weak entry still locks and the reference came straight back on the next
  frame. The test caught this. `Selection::on_items_removed` now prunes the
  selection in one batched change and forgets the matching last-selected
  entries.
- **`Properties::m_target` is the #252 pin, not a selection follow.** An
  unpinned Properties window shows the selection and holds nothing across
  frames, so the reference that can outlive content is the pin (and
  `m_inspected_material`). The smoke test pins a window with
  `open_properties_window` rather than selecting a node.
- **The primary `Properties` window is not in `Editor_windows`** - that vector
  holds only the extra pinned ones. It is owned by `Editor`, so
  `App_context::properties` was added to reach it.
- **`Brush_placement` is not wired**, as the plan anticipated: it is a
  `Node_attachment` (`brush_placement.hpp`), not a singleton, and it leaves the
  scene with the node it is attached to.
- **No `clear_operation_history` tool was added** - `clear_undo_history`
  already existed.
- **No `brush_drag_and_drop` value was added to `set_tool_asset`.** The plan
  called for it so the drag-and-drop brush reference could be armed headlessly.
  It turns out `Brush_tool::m_drag_and_drop_brush` is not a cross-frame
  reference at all: `Viewport_window` cancels it on any frame without an ImGui
  drag payload, so it only exists for the duration of a drag. The handler still
  clears it (a removal can land mid-drag), but there is nothing for a headless
  test to arm, and a tool implying otherwise would have been dead API.

## Coverage and its gaps

- Unit: `editor_asset_tests`, 10 cases over the seam (cancellation, dedup, the
  `weak_ptr` contract, move-out-before-send ordering).
- Integration: three cases in `mcp_server_tests` - both repro routes and the
  announcement itself. Verified discriminating: with
  `flush_pending_removals()` disabled, all three fail.
- Smoke: `scripts/undo_reference_clearing_smoke_test.py`, 45 checks over both
  routes, redo, the pinned Properties window, announcement content, the
  library-move false positive, the material-driven tool references, the tree
  hover pin, selection pruning and its batching, the release criterion, and the
  scene-close leak watchdog.
- **Not covered headlessly:** brush references. The test glTF carries no
  `ERHE_brushes`, and `create_shape(add_brush)` records no undoable operation,
  so there is no way to remove a brush from a library over MCP. `Brush_tool`'s
  clearing rides the same handler as the material cases.
- **Not covered:** the ownerless-library silence (no MCP tool removes a
  content-library item), and `Properties::m_inspected_material`'s dirty-edit
  warning, whose dirty flag is only set from the ImGui render path - both are
  documented above rather than asserted.
- The full release criterion ("assets truly unloaded") needs undo **plus**
  Clear History, as the scope limit at the top states. A container unload can
  still be refused afterwards by legitimate declared users - other scenes'
  library entries referencing a shared material - so the smoke test asserts
  that no window, tool, or the undo history is among the refusing users.
