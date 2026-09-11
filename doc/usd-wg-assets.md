# USD Assets Working Group survey

Every entry asset of the ASWF USD Assets Working Group repository
(github.com/usd-wg/assets) opened in a headless erhe editor, with what
the editor made of it. This is the checklist of USD support the editor
still lacks, ordered in the Gaps section by how many assets each gap
affects; `doc/usd-compatibility-plan.md` step S1 owns the survey and its
later steps take their next fix from that list.

Re-run it from the repo root against a local clone of the repository
(`<usd-wg-assets>`), with the headless Vulkan build current:

```
py -3 scripts/usd_wg_asset_survey.py --root <usd-wg-assets>
```

The script launches and relaunches the editor itself, writes the raw
per-entry data to `logs/usd_wg_survey/summary.json` and regenerates this
document from it (`--from-summary` regenerates without a run). Its
docstring states which files of a folder count as entry assets.
A run restricted to some entries (`--only`, `--exclude`, `--limit`) surveys those and
keeps every other entry's record, so the document always states the whole
survey; each record carries the date it was surveyed on. The by-eye
verdicts of the next section come from `doc/usd-wg-assets-eye.json`, which
a run reads and never writes; `--eye-note <entry> "<what the capture
shows>" [--eye-gap "<cause>"]` is how one is recorded. The expected
results of the section after it come from `doc/usd-wg-assets-expected.json`,
hand-edited: the diagnostics an entry reports by design (and, where stated,
its by-eye appearance gap) do not count against its verdict.
Screenshot paths are under `logs/`, which is gitignored: the column is a
pointer into the last run's output, not a committed file.

A capture waits for the editor to report itself idle first
(`get_async_status`: pending, running, queued_operations,
pending_scene_commits and asset_loads all 0 over two reads), because
`open_scene` answers as soon as the scene exists while its meshes and
textures keep arriving on worker threads; the counts, the framing and the
image all come from the finished asset. An entry whose load is still in
flight after `--load-timeout` says so as its gap.

Each capture is taken through `frame_scene`, which binds the opened scene
into a viewport, gives it a camera when the file authors none and places
that camera on the union world AABB of the scene's meshes: three quarters
round, or straight down the shared axis when every mesh is coplanar, so a
test card is seen the way its own reference image shows it. A scene whose
file authors no light is lit by the editor's own headlight - one white
directional light along the viewport camera's axis, the way usdview lights
a stage that authors none - so the capture shows the geometry. That light
is no scene item, and the `Lights` column is what the file itself authored.
The editor's own sky background and grid are off for every capture
(`set_graphics_settings {"sky_enabled": false, "grid_visible": false}` once
per editor launch, a session-only override that leaves the stored settings
untouched), so an image holds only what the file authors and compares
cleanly against the asset's own reference render.

The `Reference` column names the renders the repository ships beside each
asset (`screenshots/` first, then `thumbnails/`), repo-relative to
`<usd-wg-assets>`. `--compose-comparisons` puts the capture, the Storm render
and the first of those side by side under `logs/usd_wg_survey/compare/`,
which is how the appearance verdicts below were reached.

With a prebuilt OpenUSD named by `--usd-root` (or `ERHE_USD_ROOT`), each
entry also carries what OpenUSD itself makes of the file: the `Composed`
column is the composed stage's `UsdGeomMesh` count (`scripts/usd_wg_pxr_stage.py`,
instance proxies included, so an instanced prototype counts once per
instance and a `PointInstancer`'s instances not at all), and the `Storm`
column is the normalized cross-correlation between the capture's 3D view and
a `usdrecord` Storm render of the same file through the same camera - the
editor's computed camera authored into a session layer in the stage's own
space, or the file's first camera named by path - at the capture's aspect
(1.00 = identical grey images; lighting differs by design, so a lit, matching
scene scores well below 1). Under `--storm-threshold` a lower score is a gap
of its own; the score is what orders the by-eye reads. The composed stage's
world bounds are compared with the scene's too (`bounds_deviation` in the
summary): a disagreement over a tenth of the diagonal is a gap, the way a
dropped transform, an unapplied skin or a stray prototype shows up.

Run: 2026-09-11, 2 entries, 40 s of survey time.
Verdicts: 0 works, 2 works with a gap, 0 fails, 0 crash.

## Verdicts checked by eye

The capture was read for these entries, and it - not the counts and not
the log - settled the verdict. Every other row is what the counts, the
log and the empty-viewport test decided.

| Entry file | What the capture shows |
| --- | --- |
| full_assets/OpenChessSet/chess_set.usda | the whole set loads and its pieces stand where the reference puts them, lit and legible; every mesh renders in the unbound default grey because the file's meshes reach erhe with no material bound at all, where the reference shows dark and light stone with green and gold accents |
| test_assets/TextureFileFormatTests/all_files.usda | all 24 labelled tiles show their texture and the magenta background frames render as in the Storm render (the 32-bit PNG is dark in Storm too) |

## Entries

`authored` is the prim count `describe_usd_file` reports for the file;
`prims` / `meshes` / `materials` / `lights` are what the loaded scene holds.

| Folder | Entry file | Load | Authored | Prims | Meshes | Composed | Mats | Lights | Storm | Warnings and errors | Screenshot | Verdict |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |
| full_assets/OpenChessSet | chess_set.usda | ok | 26 | 270 | 53 | 21 | 0 | 0 | 0.56 | 14 warning; USD prim '*' references '*': a MaterialX document is not a U | logs/usd_wg_survey/full_assets_OpenChessSet_chess_set.usda.png | works, gap: USD prim '*' references '*': a MaterialX document is not a USD layer - the arc i |
| test_assets/TextureFileFormatTests | all_files.usda | ok | 12 | 76 | 24 | 24 | 16 | 0 | 0.15 | none | logs/usd_wg_survey/test_assets_TextureFileFormatTests_all_files.usda.png | works, gap: displayColor on a mesh without a material renders dark: the fallback material's |

## Gaps

Each distinct failure, error or warning once, with the number of entry
assets it affects and what the editor would have to support to clear it.

| Assets | Kind | Cause | What the editor would have to support |
| ---: | --- | --- | --- |
| 1 | note | Threading is disabled for this build. | nothing: the line reports how this build is configured |
| 1 | warning | USD prim '*' references '*': a MaterialX document is not a USD layer - the arc is not instantiated | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | USD prim '*': variant '*' of set '*' binds '*' to material '*', which the file has no prim for - the binding is dropped | diagnose the message and add the support it asks for |
| 1 | appearance | a file whose meshes arrive with no material binding renders in the unbound default grey: the chess set imports 21 meshes and 0 materials | diagnose the message and add the support it asks for |

