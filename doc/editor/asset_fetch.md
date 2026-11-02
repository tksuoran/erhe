# Asset fetch

Stability: experimental

`Asset_fetch` (`src/editor/assets/asset_fetch.{hpp,cpp}`) downloads zip
archives of assets that are too large for the repository and extracts them
into `res/editor/assets/`. The Operations window's Scenes group has one button
per source:

| Button | Archives | Result |
|---|---|---|
| Fetch Bistro Scene | Google Drive zip of `bistro_lights_compressed/` (the one `scripts/bistro_frame_bench.py` downloads) | `bistro_lights_compressed/bistro_lights_c.gltf` and every other entry |
| Fetch Sponza Scene | Intel Sample Library "Sponza Base Scene" (`https://cdrdv2.intel.com/v1/dl/getContent/830833`) and "Colorful Curtains" (`.../getContent/726650`) | `sponza/base/` and `sponza/curtains/` (glTF + dependencies + `credits_license.txt`), and the host file `sponza/sponza.gltf` |

The Intel scenes are CC BY 4.0; `credits_license.txt` carries the required
attribution. The cdrdv2 links redirect to signed CloudFront URLs that expire,
so the source names the cdrdv2 link, never the redirect target.

## Source

An `Asset_fetch_source` names the extraction root (working-directory
relative, `res/editor/assets`), a marker file relative to that root, the
archives, and optionally a host file. Each `Asset_fetch_archive` has:

- a direct `url` (redirects followed) or a Google Drive file id;
- an `entry_prefix`: the archive directory taken as the archive's root (only
  entries below it are considered, with the prefix stripped);
- a `gltf_path` the archive must contain;
- a selection: `all_entries`, or `gltf_dependencies` - the glTF, every
  relative buffer and image URI it names (percent-decoded, resolved against
  the glTF's directory), and `extra_paths`. A dependency the archive lacks
  fails the fetch. The Sponza base archive unpacks to 6.7 GB in 156 entries;
  its glTF needs 74 of them (2.2 GB) - the 3ds Max / FBX / USD variants,
  unreferenced texture maps and the HDR environment map are not extracted;
- a `destination` directory relative to the root.

## Host file

A source with a `host_path` gets a generated glTF 2.1 file
(`asset.version` and `minVersion` 2.1) that holds no content of its own: a
`files` entry per instanced glTF (URI relative to the host file),
an `externalAssets` entry per file, and one node per instance referencing it
with `externalAsset`, all roots of one scene. Opening it instantiates the
fetched glTFs as prefabs (`doc/plans/gltf_prefabs.md`). Sponza's base scene
and curtains are authored in one Y-up space, so the instance nodes carry no
transform. The host file is the Sponza marker: it is written last.

## Flow

1. The button queues `Asset_fetch::fetch()` out of the ImGui iteration
   (`doc/editor/coding_rules.md` "ImGui-time work").
2. `fetch()` checks the marker file. When it exists nothing is downloaded and
   the status line reads "already present".
3. Otherwise a worker thread (not a Taskflow worker: it blocks on the network
   for minutes) handles the archives in order: download into
   `res/editor/assets/.fetch_<name>/archive.zip`, extract the selected entries
   into the staging `extracted/` directory, delete the zip. Google Drive
   answers a large file with a virus-scan warning page; its form action plus
   hidden fields (`confirm`, `uuid`, ...) is the download URL.
4. Extraction refuses absolute entry paths and paths that leave the root;
   miniz checks each entry's CRC32.
5. After every archive (and the host file), the staging directory's top-level
   entries are moved into the root. A destination that already exists
   (without the marker) fails the fetch rather than merging into it. The
   staging directory is removed in every outcome, so a failed or cancelled
   fetch leaves nothing that the marker check could mistake for a fetch.
6. The worker hands completion to the main thread through the scene commit
   queue; on success the asset browser rescans.

While a fetch runs its button is replaced by a progress bar (download bytes,
then extracted bytes, per archive: "downloading 1/2") and a **Cancel Fetch**
button; the other sources' buttons are disabled. Editor shutdown cancels and
joins a running fetch before the commit queue is cleared.

## Dependencies

- HTTPS: cpp-httplib's client with Mbed TLS 3.6 (LTS) as its TLS backend on
  every platform, fetched by CPM. Root certificates come from the OS: the
  Windows certificate store, the macOS keychain, the Linux CA bundle paths
  httplib searches, and on Android the conscrypt APEX directory (or
  `/system/etc/security/cacerts`), passed explicitly.
- Zip: miniz, fetched by CPM, built with `MINIZ_NO_ZLIB_COMPATIBLE_NAMES`
  (only the `mz_` API is used; the zlib-named aliases collide with zlib
  headers httplib includes on Linux).
- glTF dependency scan and host file: nlohmann/json.
