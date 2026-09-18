# Asset browser: two-phase scan

Stability: stable

The asset browser (`src/editor/asset_browser/asset_browser.{hpp,cpp}`)
fills its tree in two phases. Phase 1 is the directory walk: it lists what
files exist and classifies them by extension. Phase 2 is the content peek:
it reads inside one asset file (today `scan_gltf`, a whole-file read plus a
JSON parse) and runs only for the file the user hovers. Both phases run on
executor workers; the main thread only picks results up.

## 1. Requirements

- R1. The directory walk runs on an executor worker and touches nothing the
  main thread reads while it runs. It stats and lists directory entries and
  classifies each entry by extension. It opens no file.
- R2. The walk publishes what it has found at a regular interval, 50 ms,
  and once more when it finishes. Each publication is the ordered run of
  entries found since the previous one, so the main thread can attach them
  in walk order (a parent always precedes its children).
- R3. The main thread turns published entries into `Asset_node` objects and
  attaches them to the shown tree the frame it picks them up, so the
  window shows the tree growing while the walk runs. The "Scanning..."
  label stays until the walk's last publication has been applied.
- R4. The main thread performs no filesystem call when it applies a
  publication: the entry carries the classification the walk made.
- R5. A file saved while a walk is in flight is refreshed against the tree
  once the walk has finished (the existing pending-refresh queue).
- R6. The content peek runs per file, on a worker, only for the hovered
  file (or the file whose context menu is open), and its result is cached
  on the node so the peek runs once per node. The main thread picks each
  file's result up as it lands, independently of other files. This is
  what `ensure_scanned` / `Gltf_scan_request` do today for glTF files.

## 2. Design

- D1. `Asset_scan_entry` is the walk's unit of output: the entry path, the
  path key of its parent (empty for the synthetic root), and the node kind
  the walk classified (`Asset_node_kind`: folder, gltf, geogram, usd,
  texture, other). The walk classifies with the same extension tests
  `make_node` uses today; `make_node` takes the kind as an argument instead
  of calling `is_directory`, so both a walk publication and a single-file
  refresh construct nodes through it (the refresh classifies with one
  `is_directory` call on the main thread, as it does today).
- D2. `Asset_scan_request` holds a mutex, the published-but-unapplied
  entries (`std::vector<Asset_scan_entry>`), the working directory the
  walk sampled (published before the first batch), and the `finished`
  flag. The worker appends to a private batch and moves it into the
  request under the mutex when 50 ms have elapsed since the previous
  publication and when the walk ends; `finished` is set after the last
  batch is in. The main thread reads `finished` before it drains, moves
  the published entries out under the mutex into a browser-owned scratch
  vector and applies them outside the lock; the batch, the published
  vector and the scratch all keep their capacity across publications.
- D3. A walk builds a new `Asset_tree` on the main thread: the entry with
  an empty parent key is the walk's synthetic root, and applying it
  replaces the shown tree with the new (still growing) one, built against
  the working directory the walk published, and calls `set_root` on the
  window; later entries attach to it through its `nodes_by_path`.
  `Asset_tree` is main-thread owned throughout; the worker never holds a
  node.
- D4. `apply_finished_scan` becomes `apply_scan_progress`: it applies every
  publication available now, and when the request is finished after the
  last one, drops the request and replays the pending refresh paths (R5).
  Its callers stay: the window's `imgui()` each frame and `refresh_file`.
- D5. The walk logs one info line per publication (entries in the batch,
  elapsed ms since the walk started) and one when it finishes (total
  entries, total ms), so the cadence is checkable from `logs/log.txt`.

## 3. Steps

- Step 1 (S): D1-D5 in one commit. Verification: build
  `scripts\build_ninja_win_vulkan.bat editor` and the headless editor;
  launch headless with `ERHE_AI_DRIVER=1`; `logs/log.txt` shows several
  `Asset browser: walk published` lines with rising elapsed times about 50
  ms apart before the `walk finished` line; `save_scene` over MCP still
  logs `Asset browser refreshed ... node added|replaced`; a second `Scan`
  during a walk logs "already in flight". The hover peek is unchanged code
  and is user-verified interactively.

## 4. Status

Step 1 landed.
