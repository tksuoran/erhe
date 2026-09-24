# Clearing editor references to content removed by an undo

Stability: mostly stable

Undoing a glTF import takes the imported content back out of the editor
without closing a scene, so the `close_scene` channel - the one teardown
notification editor parts subscribe to - never fires. `Items_removed_message`
is the second teardown channel, published once per frame for content that left
the editor without a scene closing, and it carries the same contract as
`close_scene` (doc/editor/coding_rules.md, "Scene-hosted references in editor parts"): a part
that caches a reference to editor content drops it when this message names
that item.

Stale references here are not cosmetic. `Asset_manager::unload_record` drops
the container record and then verifies exclusivity, logging
`undeclared asset user: ... (a raw shared_ptr bypassed Asset_reference)` for
every asset still alive, so a window or tool holding a raw `shared_ptr` lands
in that report and the container never releases cleanly.

**Scope limit.** Undo alone can never fully release the assets, by design: the
undo entry keeps the imported objects so redo can put them back
(doc/editor/reloadable_asset_loads.md is what makes an undone import drop that
payload when nothing needs it). What this mechanism delivers is that after an
undo the only thing keeping the content alive is the undo history, which
`unload_record` already reports by name. Full release is undo plus Clear
History.

## The message

`src/editor/app_message.hpp`, registered `Dispatch_policy::sync_only`:

```cpp
struct Removed_items
{
    std::unordered_set<const erhe::Item_base*>    lookup; // membership test
    std::vector<std::shared_ptr<erhe::Item_base>> owners; // keeps them alive for the dispatch
};

struct Items_removed_message
{
    std::shared_ptr<const Removed_items> removed;
};
```

The payload is shared because `Message_bus::send_message` takes the message by
value. A handler does a lookup against the set ONLY - no manager lookups, no
linear scans - because undoing a large import announces thousands of items at
once.

## The seam

`Pending_item_removals` (`src/editor/assets/pending_item_removals.{hpp,cpp}`)
holds the bookkeeping, so it is unit-testable without an editor:

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

`Asset_manager` owns one, and `flush_pending_removals()` is
`if (auto removed = m_pending_removals.take()) { send(...); }`. Moving the
state out BEFORE the send is the contract: a subscriber that calls
`note_detached` during the dispatch lands in the next batch instead of having
its append silently discarded.

Entries are `weak_ptr`: an item that dies before the flush needs no
announcement, and the manager must not become a holder of an asset it is about
to unload.

`note_attached` cancelling a pending entry is what makes the producers
false-positive free. `Hierarchy::set_parent` runs `handle_remove_child` then
`handle_add_child` in one call, and `Hierarchy::remove()` promotes a folder's
children to the grandparent before orphaning itself, so folder moves,
cross-library moves and folder removal all cancel themselves within the frame;
no path detaches in frame N and re-attaches in frame N+1.
`remove_all_children_recursively` orphans every descendant with no re-attach,
so those items really are removed and announcing them is correct.

**Thread contract.** `Pending_item_removals` is main-thread only, like the
rest of `Asset_manager`, and asserts it with the same `verify_main_thread()`
pattern rather than carrying a mutex. Every producer already runs there:
operations run from `Operation_stack::update()`, library mutation is main
thread, and the asynchronous load task touches no `Content_library`. One
residual path: `~Content_library` takes its kind scopes out of the tree, so
it announces, and a library could in principle be destroyed on a worker when
the last `shared_ptr` drops there - safe only because the manager pointer is
normally already disarmed by then, which is a further reason the note is
routed off that pointer (see Producers).

Two re-entrancy hazards are handled by construction rather than by a test that
manufactures a re-entrant subscriber, because no editor part is one: a
subscriber calling `note_detached` during dispatch is safe given the move-out,
and a subscriber reaching `flush_pending_removals` would deadlock on the
non-recursive `m_receivers_mutex` that `send_message` holds across every
handler, so an `ERHE_VERIFY(!m_in_flush)` scope flag makes that fail loudly
instead of hanging.

## Producers

- **Content library entries** - `Content_library::announce_detached()` and
  its mirror `announce_attached()` (`content_library.cpp`). These are the
  single choke point for every library removal: a resource prim leaving the
  scene tree reaches `Content_library::unregister_prim` through the item-host
  hook (`erhe::Typed::handle_item_host_update`), and that announces. It covers
  every flavour of
  `Content_library_attach_operation` an import compound builds (textures,
  materials, skins, animations, physics materials, collision filters, physics
  joints, brushes, `Graph_texture`, `Graph_mesh`).

  The note is routed off the library's manager pointer, not off the item's
  owner, and it is made for EVERY kind rather than only the manager-owned
  ones: `Asset_manager::on_library_prim_detached` early-returns for anything
  that is not brush, material or animation, which would miss the `Graph_mesh`
  / `Graph_texture` targets the graph windows hold. Two silences follow and are
  both intended: an ownerless library (material preview, tool scene, the
  `Scene_builder` palette) stays quiet, since nothing outside it points at its
  items; and `on_scene_unregistered` nulls the library's manager pointer, so
  an already-unregistered scene's later library removals are silent, because
  that scene's assets were announced wholesale by the producer below.
- **Scene nodes** - `Item_insert_remove_operation`, on the two directions that
  take the item out of the scene: undo of `Mode::insert` (which glTF import
  uses) and execute of `Mode::remove`. Nodes have no editor-level detach hook
  to hang this on, and this also makes an ordinary node delete clear the
  Properties target.

  **Snapshot timing is load-bearing.** In `Mode::remove`, `execute()` first
  re-parents every non-bone-proxy child to the grandparent through
  `m_parent_changes` and only then orphans `m_item`; walking the subtree from
  `m_item` before that point would announce nodes that are still in the scene
  and make every subscriber drop a live node. The subtree is therefore
  collected immediately around the `set_parent` call and guarded by mode, so
  an insert is never announced: in `execute()` after the `m_parent_changes`
  loop and before `set_parent`, and in `undo()` after its `set_parent` and
  before the `m_parent_changes` undo loop. The `undo()` point yields the right
  subtree only because `Skin_registered_message` is queued rather than sync,
  so `Bone_visualization::remove_skin_proxies` has not yet detached the
  proxies; if that ever becomes synchronous, snapshot before `set_parent`.
  Bone proxies are deliberately excluded from `m_parent_changes` and do leave
  with the item, so they are announced, which is correct.
- **Scene unregistration** - `Asset_manager::on_scene_unregistered`, reached
  from `App_scenes::unregister_scene_root`. This is the route
  `Scene_open_operation::undo` takes: it unregisters the scene and removes the
  browser window, never undoes the inner import compound and never publishes
  `Close_scene_message`, so without this producer "Open Scene, select an
  animation, Ctrl+Z" would leave the Animation window pointing into an
  unregistered scene. The record survives unregistration with its scene
  entries intact, so every asset of the record is enumerated into the pending
  list. It fires on a normal scene close too, harmlessly redundant with the
  `close_scene` handlers.

  Two documented consequences of that route: redo re-registers and re-arms the
  record but does not restore the Animation window's selection; and the
  producer announces the record's ASSETS only, so a Properties inspector
  targeting a NODE of the unregistered scene stays stale.

## Delivery

`Asset_manager::flush_pending_removals()` is called from `Editor::tick`
immediately before the message bus pump, and from nowhere else. That point is
chosen: it is after `m_operation_stack->update()`, so an undo performed this
frame is announced this frame; after `m_imgui_windows->end_frame()`, so it is
outside ImGui iteration; and outside both `Content_library::mutex` and the
`Item_host_lock_guard` the producing operations hold, which are non-recursive.
The flush returns immediately when the list is empty, which is what keeps it
clear of the AGENTS.md "No update each frame patterns" rule: the producers are
change-driven and the flush is a no-op in the steady state. The one tick path
that skips the flush is the Android swapchain-unavailable early return; the
weak list simply carries to the next tick.

## Subscribers

Each drops the references its `on_close_scene` handler drops, keyed on
identity through `message.removed->lookup.contains(ptr)`:
`Animation_window` (also clearing the player), `Animation_player`,
`Properties` (its pinned target, its target items and the inspected
material), `Item_tree` (hovered and popup item plus the cached rows),
`Editor` (the geometry and texture graph window targets, primaries and
extras), `Brush_tool`, `Material_paint_tool`, `Material_preview`,
`Brush_preview`, `Brdf_slice`, `Operations`, `Create`, `Physics_tool`,
`Thumbnails`, `Scene_root` (a brush's material), `Variant_table`,
`Transform_tool`, `Physics_driven_drag` and `Selection`.

`Item_tree`'s hovered and popup items pin specifically in the
hover-then-stop-rendering case: a rendering tree resets both every frame, and
a hidden one already calls `clear_cached_rows()`, but nothing clears hover and
popup once the window is hidden or closed. The subscription belongs on
`Item_tree` rather than on the window, because `Asset_browser_window` inherits
`Item_tree_window` and needs the same cleanup; `Item_tree` instances therefore
register themselves in a registry under a stable label, which is also what
gives `debug_set_item_tree_hover` a `tree` argument to resolve.

**`Selection` owns its own cleanup, and has to.** Pruning the selection is not
enough on its own: `Operations` re-resolves `get_last_selected<Material>()`
into `m_make_mesh_config.material` every frame, and `m_last_selected_by_type`
is weak but a removed item stays alive in the undo history for redo, so the
weak entry still locks and the reference comes straight back on the next
frame. `Selection::on_items_removed` prunes the selection in ONE batched
change - build the filtered vector and call `set_selection(filtered)`, never a
loop of `Selection::remove_from_selection`, each call of which opens its own
`Scoped_selection_change` that sorts the whole selection twice and dispatches
a sync `Selection_message` - and forgets the matching last-selected entries.

`Properties`' inspected material diverges from the close-scene handler on
purpose: that one drops a dirty edit session because the close drops the undo
history anyway, which is not true here, so the handler drops the reference,
resets the state, and warns when the material state is not clean, making a
discarded in-progress edit visible instead of silent.

Deliberately not wired: the hotbar and inventory slots and the clipboard pin
their items on purpose through `Asset_reference` (a persistent inventory);
they are declared users and surface as named unload refusals.
`Brush_placement.brush` is a value of the placed node rather than a reference
held by an editor part, so it leaves the scene with that node.

An undo also takes out the kind `Scope` a resource's insert had to create;
that rule, and why the scope's undo is conditional on the scope being
childless at that moment, is in doc/editor/content_library.md ("A kind scope
belongs to the operation that needed it").

## Observing it

Three MCP tools exist for this (all with explicit arguments, per
doc/agents/mcp_api_guidelines.md):

- `get_editor_references` (query, no arguments) - every cached content
  reference the subscribers above hold, each as `{name, uid, type}` or null.
  It is the direct observable for this mechanism and the general debugging
  tool for the whole bug class. It also reports
  `items_removed_announcement_count` and `last_announced_uids`, the only way
  to observe that a message was or was not published, and
  `selection_change_count`, so the batching requirement above is assertable.
- `reparent_item` - moves any prim under any prim, which drives the
  detach-then-attach cancellation case.
- `debug_set_item_tree_hover` - hover and popup are set only by ImGui
  interaction, so the pin they cause is otherwise unreachable headless.

## Verification

- Unit (`editor_asset_tests`): the seam - cancellation, dedup, the `weak_ptr`
  contract, and the move-out-before-send ordering.
- Integration (`mcp_server_tests`): both repro routes and the announcement
  itself. Verified discriminating: with `flush_pending_removals()` disabled,
  all three fail.
- Smoke (`scripts/undo_reference_clearing_smoke_test.py`): both routes, redo,
  the pinned Properties window, announcement content, the library-move false
  positive, the material-driven tool references, the tree hover pin, selection
  pruning and its batching, the release criterion, the kind scopes the import
  created, and the scene-close leak watchdog. It runs against an already
  running editor.

Two cases in that script need their setup spelled out, because the obvious
setup passes on an empty implementation:

- **Snapshot timing.** An editor delete removes the whole subtree -
  `Selection::delete_items` collects recursively and emits one
  `Item_insert_remove_operation{Mode::remove}` per collected item - so a plain
  "delete A with children B, C" leaves A childless and never reaches
  `m_parent_changes`. Lock one child instead: `collect_item` skips
  `is_lock_edit()` items and their subtrees while `execute()` still rebuilds
  `m_parent_changes` from the live children and promotes them. So A with
  children B (locked) and C, a Properties target on A and another on B,
  `delete_nodes(A)`: C is gone, B is promoted with its reference KEPT, and A's
  reference is cleared.
- **Tree-window pin.** Each MCP call is a separate frame, so hovering first
  and hiding second lets the intervening rendered frame wipe the hover. The
  order is `set_window_visibility{visible:false}`, then
  `debug_set_item_tree_hover` on an imported row, then `undo`.

Two documented silences have no headless driver and stay asserted only by
their producers: the ownerless-library silence (no MCP tool removes a
content-library item) and the inspected-material dirty-edit warning, whose
dirty flag is set only from the ImGui render path.

## Future work

- [plans/content_library.md](../plans/content_library.md) - headless coverage for brush removal and the two silences above.
