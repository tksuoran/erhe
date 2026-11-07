# erhe_asset_store

Stability: experimental

## Purpose

Content-addressed item store in one SQLite file: the container of editor
assets (`<source stem>.erhe_editor`) and prepared assets
(`<source stem>.<target>.erhe_prepared`) of
`doc/plans/prepared_assets.md` (design D2 of
`doc/plans/prepared_assets_design.md`). It knows items, keys and the
container; scenes, glTF and the item formats belong to its callers.

## Key Types

- `Key` -- 16 bytes (`bytes`), comparable (`<=>`), hashable through
  `Key_hash`, with a 32-digit lowercase hex form (`to_hex()`,
  `from_hex()`). It is both an item key and a content hash, in XXH3-128
  canonical (big-endian) byte order.
- `Content_hasher` -- XXH3-128 over byte spans: `update()` any number of
  times, `finish()` (does not consume the state), `reset()`, or the one-shot
  `hash(span)` (R3.4).
- `Key_builder` -- makes an item key. It starts with the `Item_kind` and
  takes typed fields: `add_code_version`, `add_format_stamp`,
  `add_int_setting` / `add_uint_setting` / `add_float_setting` /
  `add_text_setting` (named settings), `add_key`, `add_content_hash`,
  `add_bytes`; `finish()` returns the key.
- `Item_kind` -- `scene`, `local_mesh`, `world_mesh`, `raytrace_bvh`,
  `texture`, `basis_texture`, `pvs`, `editor_data` (R4.1). The numeric
  values are stored in the container and never change.
- `Item_data` -- what `put()` stores: key, kind, target, stamp, `meta`
  (JSON text that makes the item usable on its own), `info` (the node record
  of R4.8) and the payload bytes. Views; valid for the call.
- `Item_info` -- an item without its payload: the same fields plus
  `payload_size`.
- `Source_record` (relative path, scene id; R2.1) and
  `Prepared_scene_record` (target, key of the Scene item).
- `Asset_store` -- one open container.
- `Write_transaction` -- RAII write transaction on an `Asset_store`.

## Public API

- `Asset_store::open(path, Open_mode)` returns the store, or null with the
  reason logged; `open(path, mode, out_error)` also hands the reason to the
  caller ("it has format version 1; this build reads format version 2",
  "it is not an SQLite file (...)", "it is not a readable file"):
  - `editor_read_write` -- WAL journal, `synchronous = NORMAL`. Creates the
    file when absent; recreates it empty (logged as a warning, "Asset
    '<path>': it has format version 1; this build reads format version 2 -
    recreating it empty") when its format version is not
    `Asset_store::format_version` or it is not an SQLite file (R5.2). The `-wal` and `-shm` files exist while it is open and are
    removed when the last connection closes.
  - `prepared_create` -- rollback journal (`DELETE`),
    `synchronous = FULL`, so a prepared asset is one file at rest (R5.1).
    Creates a new container and fails ("the path exists; prepared_create
    writes a new container only") when anything is at the path: a prepared
    asset is written once, by Export's Copy into a temporary file it then
    renames over the target (doc/editor/prepared_assets.md "Export"), and is
    never opened for writing again.
  - `prepared_read_only` -- opens an existing file read-only; fails (logged
    error, "Asset '<path>' cannot be read: <reason>") on a missing file, a
    file that is not an SQLite file, or an unread format version.
- Reads, all thread-safe: `contains(key)`, `get_info(key)`,
  `read_meta(key)`, `read_payload(key, offset, out)` (reads exactly
  `out.size()` bytes at `offset`; false when the item is absent or the
  range leaves the payload; R5.4), `close_payload_handles()` (see
  "Reading payloads"), `for_each_item(callback)` (in key order),
  `get_item_headers()` (key, kind, target, stamp and payload size of every
  item, `meta` and `info` left empty, in key order: one sequential read of
  the item table),
  `get_item_count()`, `get_database_size()` (page count times page size).
- Records: `get_record(name)`, `get_source_record()`,
  `get_prepared_path(target)` (R2.3), `get_prepared_scene()`,
  `get_stored_format_version()`.
- Writes, each in its own transaction: `put(item)` (replaces an item of the
  same key), `remove(key)` (true when an item was removed),
  `copy_item_to(other, key)`.
- `vacuum()` -- `VACUUM`, and in WAL mode a truncating checkpoint, so the
  file shrinks to the items it holds (R8.13).
- `set_busy_timeout(ms)` -- the bounded wait for the writer turn and for
  locks; 10 s by default.
- `Write_transaction{store}` begins with `BEGIN IMMEDIATE`. `is_active()`
  is false when the store is read-only or the writer turn was not granted
  within the busy timeout (logged as a warning; nothing is stored, R5.6).
  `put`, `remove`, `set_info(key, info)` (replaces the info of a held item
  alone; false when the store holds no item of the key),
  `copy_item_from(source, key)`, `set_record`, `set_source_record`,
  `set_prepared_path`, `set_prepared_scene` write into it; `commit()` makes them visible at once and returns false (rolling back)
  when any of them failed; destruction without `commit()` rolls back (R5.3).
- `initialize_logging()` creates the `erhe.asset_store` logger
  (`log_asset_store`); call it before the first `open()`.

## Key_builder

Each field enters the XXH3-128 stream as a one-byte field type, the field
body length as a 64-bit little-endian integer, and the body. A setting body
is the name length (64-bit little-endian), the name and the value (integers
and the bit pattern of a `double` as 64-bit little-endian, text as its
bytes). Field type and length prefix keep every two different field
sequences apart: different splits of the same text, the same bytes as a key,
a content hash or raw bytes, the same number as signed, unsigned or code
version, field order, an empty field against no field, `0.0` against
`-0.0`. Keys are persisted, so the encoding is fixed; the test
`Asset_store_key.builder_reference_value` holds one key as a reference
value. The functions that build the key of each kind (D3) live with the
code that makes that kind of item.

## Container

The file is an SQLite database of format version 2
(`Asset_store::format_version`) with three tables:

- `asset (name TEXT PRIMARY KEY, value TEXT)` -- `format_version`,
  `source.relative_path`, `source.scene_id`, `prepared_path.<target>`,
  `prepared.target`, `prepared.scene_key` (hex).
- `item (id INTEGER PRIMARY KEY, key BLOB UNIQUE, kind INTEGER, target
  TEXT, stamp TEXT, payload_size INTEGER, meta TEXT, info TEXT)` -- the
  metadata of each item.
- `item_payload (id INTEGER PRIMARY KEY, payload BLOB)` -- the payload of
  the item row of the same `id`. Every write of an item writes both rows in
  its transaction, and removing an item removes both; the ids are explicit
  integer primary keys, so `VACUUM` keeps them.
- the index `item_location (key, payload_size)` -- covers the lookup of a
  payload read (`id`, `payload_size` of a key), so opening a payload reads
  index pages and the payload, no item row: a load opens thousands of
  payloads, and the item rows are many more pages than a read connection's
  page cache holds.

The payloads have a table of their own so that a metadata read reads no
payload page. In format version 1 the payload was a column of the item row:
the row's leaf page held the first part of the payload, so every
`get_info` read up to a 64 KiB page of payload, and a column stored after
the payload walked its overflow chain. The item table now holds metadata
alone, a few pages for thousands of items, and `get_item_headers()` reads it
in one statement. Measured with
`Read_throughput_test.DISABLED_item_info_reads` (headless Release, Linux
desktop, 8000 items of 1.2 KB `meta` and 50 - 250 KB payload; "cold" drops
the file from the OS page cache first), in ms:

| | 8000 `get_info` cold / warm | `for_each_item` cold / warm | `get_item_headers` cold / warm |
|---|---|---|---|
| Format version 1 | 981 / 113 | 1808 / 113 | - |
| Format version 2 | 43 / 16 | 44 / 23 | 24 / 3.5 |

A container of format version 1 is an editor asset this build recreates
empty, so the next Export makes its items again, or a prepared asset it
refuses ("prepared asset '<path>' cannot be opened: it has format version 1;
this build reads format version 2"); Export the scene again.

An `Asset_store` holds one writer connection (none in `prepared_read_only`)
used by one `Write_transaction` at a time (a mutex), and a pool of
`Asset_store::read_connection_count` read-only connections; each read takes
a free one. The file has 64 KiB pages, set when it is created (a file made
before keeps its 4 KiB pages and reads the same; the format version does
not depend on it). Payload ranges are read with SQLite's incremental blob
interface ("Reading payloads"). Readers see only committed transactions.
`copy_item_from` reads the source row inside one read transaction and
streams the payload in 1 MiB chunks into a `zeroblob` of the destination,
so a large item is never held in memory.
A payload is at most SQLite's blob limit (about 1 GB).

Other processes open the same file the same way: in WAL mode any number of
readers and one writer work at once; a second writer waits up to the busy
timeout for its turn.

`for_each_item` keeps its read transaction open while the callback runs;
the callback reads, and writes go after the walk (in rollback-journal mode
a commit waits for that read to end).

SQLite 3.54.1 (amalgamation) and xxHash 0.8.3 are fetched download-only by
CPM in the root `CMakeLists.txt` and compiled into this library; their
headers stay private to it. xxHash is built with `XXH_NAMESPACE`, so other
copies of xxHash in the link do not collide.

## Reading payloads

A payload larger than a page is a chain of overflow pages, each naming the
next. A blob handle opened for a read walks the chain from the row to the
range's first page, and a read through SQLite moves whole pages: pages
wholly inside the range are read straight into the caller's buffer, the
partial pages at its two ends through the connection's page cache. A
loader reads several ranges of one mesh item (indices, vertex streams,
points, LOD levels; `doc/erhe/gltf.md` "Mesh items"), so with a new handle
per range and 4 KiB pages that is about 25 times the read calls of the file
path (one positional read per range), and the start of each payload is
walked once per range. Two things bring it to the file path:

- 64 KiB pages: 16 times fewer pages per payload, so 16 times fewer reads
  and a chain 16 times shorter. The cost is the partly filled last page of
  each payload (3 % on Bistro-like items).
- A `prepared_read_only` store keeps the blob handle of the item a read
  connection read last open after the read, and a read of the same item
  prefers that connection: the handle's cursor keeps the overflow page
  numbers it has walked, so the next range of the item starts at its page,
  and the shared page of two adjacent ranges is in the cache. Nothing
  writes a prepared asset in place (an Export renames a new file over it),
  so an open handle blocks no writer. `close_payload_handles()` closes the
  handles, frees the page caches the
  reads filled, and makes every later read close its handle (as in the
  other modes) and free its connection's page cache: after the load a
  prepared run reads rarely (a PVS page, a released soup), and a cache
  refilled by those reads would stay resident for the life of the scene. The prepared run's buffer readers call it at the end of the
  load (`Gltf_buffer_reader::close()` from `close_source_files()`), so the
  load window is the only time handles stay open; a released soup read back
  later opens and closes its own.

`Asset_store::read_connection_count` is 8, so a load with 8 workers reads
in parallel as the file path does.

Each read connection has a page cache of at most 1 MiB (`PRAGMA cache_size =
-1024`, 16 pages): payload reads pass the pages wholly inside a range
straight to the caller's buffer and keep only the pages at the range ends
and the b-tree pages in the cache, so a larger cache holds nothing a load
reads again. SQLite's default of 2 MiB per connection is 16 MiB over the
pool, resident through a load; `DISABLED_mesh_item_reads` reads as fast with
1 MiB as with 2 MiB (with 512 KiB the single-thread ranged reads are 17 %
slower, with 128 KiB 20 %).

Measured with `Read_throughput_test.DISABLED_mesh_item_reads`
(`erhe_asset_store_tests --gtest_also_run_disabled_tests`, headless
Release, Linux desktop, the file in the page cache): 3000 items of 100 -
500 KB (900 MB), each read as 6 back-to-back ranges or whole, against the
same bytes read from a flat file, in ms:

| Threads | File, 6 ranges | Before: 6 ranges / whole | 64 KiB pages | + open handles, 8 connections |
|---|---|---|---|---|
| 1 | 58 | 234 / 122 | 169 / 68 | 75 / 62 |
| 4 | 22 | 71 / 38 | 48 / 24 | 30 / 22 |
| 8 | 15 | 99 / 43 | 73 / 27 | 20 / 17 |

On Quest 3, the Bistro prepared run (7878 items, 1045 MB;
`doc/reference/quest_bench_runs.md`, the entries of 2026-11-07 07:47) is
listed in 4.54 s median against its prepared `.glb`'s 4.68 s and builds its
meshes in 2.04 s against 1.82 s; VmHWM 799 MB against 785 MB.

Reading the whole payload once into a caller buffer is no faster than the
ranges with an open handle, and would need the loader to say when a
primitive's views are read. Memory-mapping the file (`PRAGMA mmap_size`)
shortens the walks too, but the mapped pages count in the process'
resident set (VmHWM), so it is not used.

## Dependencies

- **erhe libraries:** `erhe::file`, `erhe::log`, `erhe::verify` (all private)
- **External:** SQLite, xxHash (compiled in, private), `Threads::Threads`

## Tests

`erhe_asset_store_tests` (`src/erhe/asset_store/test`):

- `test_key.cpp` -- hex round trip, the XXH3-128 reference value of empty
  input, incremental against one-shot hashing, `Key_builder` determinism,
  its reference value and field separation.
- `test_asset_store.cpp` -- put / get / range reads, parallel range reads
  from eight threads, rollback of an uncommitted transaction, a child
  process killed with `SIGKILL` inside an uncommitted 32 MB write leaves no
  findable item and the store recovers (POSIX only; the in-process rollback
  test runs everywhere), two writers through separate stores and a writer
  refused after its busy timeout, format version handling (unread version,
  non-SQLite file, missing file), records, `copy_item_to` into a prepared
  asset that is one file at rest, and remove plus `vacuum()` shrinking both
  journal modes, and a `prepared_read_only` store with open payload
  handles: ranges in any order, `prepared_create` refused on its path,
  reads after `close_payload_handles()`; `get_item_headers()` against
  `get_info()`; payload rows that follow their items through a replace, a
  remove and `VACUUM`; a container of the previous format version refused
  read-only with the reason, and `prepared_create` refusing an existing path.
- `test_read_throughput.cpp` -- the disabled benchmarks of "Reading
  payloads" and "Container".

## Future work

- [plans/prepared_assets_design.md](../plans/prepared_assets_design.md) - the per-kind key functions (D3) and the editor and glTF consumers (D4 - D8).
