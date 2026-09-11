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
shows>" [--eye-gap "<cause>"]` is how one is recorded.
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

Run: 2026-09-08, 146 entries, 3141 s of survey time.
Verdicts: 47 works, 98 works with a gap, 1 fails, 0 crash.

## Verdicts checked by eye

The capture was read for these entries, and it - not the counts and not
the log - settled the verdict. Every other row is what the counts, the
log and the empty-viewport test decided.

| Entry file | What the capture shows |
| --- | --- |
| full_assets/CarbonFrameBike/CarbonFrameBike.usdz | the frame, wheels and parts sit where the usdrecord render puts them, and the four Schlauch cable meshes are skinned: their world bounds match pxr's UsdSkel ComputeSkinnedPoints to 4 mm, and they run along the fork and the swingarm as they do in the reference render |
| full_assets/McUsd/McUsd.usda | the opaque blocks match the reference; the purple stained glass cube is see-through and both cross-shaped cards - sunflower and fern - show both of their faces |
| full_assets/McUsd/McUsd.usdz | the opaque blocks match the reference; the purple stained glass cube is see-through and both cross-shaped cards - sunflower and fern - show both of their faces |
| full_assets/McUsd/McUsd_10cm.usda | the opaque blocks match the reference; the purple stained glass cube is see-through and both cross-shaped cards - sunflower and fern - show both of their faces |
| full_assets/McUsd/McUsd_10cm.usdz | the opaque blocks match the reference; the purple stained glass cube is see-through and both cross-shaped cards - sunflower and fern - show both of their faces |
| full_assets/OpenChessSet/chess_set.usda | the whole set loads and its pieces stand where the reference puts them, lit and legible; every mesh renders in the unbound default grey because the file's meshes reach erhe with no material bound at all, where the reference shows dark and light stone with green and gold accents |
| full_assets/UsdCookie/UsdCookie.usdz | the cookie carries its baked texture from inside the .usdz and matches the reference, at a brighter tone |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/vehicles/tractor/asset/tractorFullAsset.usda | the tractor renders in the reference's red and grey; the material bound from the kit's own material layer now reaches the meshes |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/wheels/wheelNormal/asset/wheelNormalAsset.usda | the wheel carries a material and its shape matches the reference |
| test_assets/AlphaBlendModeTest/AlphaBlendModeTest.usd | not judged against the reference: the file authors no camera, so the capture is a whole-model view too small to compare panel by panel |
| test_assets/ColorSpaceTests/UsdPreviewSurface/usduvtexture_color_test.usda | one row of the 5x5 chart samples a salmon tone, the other four render white where the reference shows every swatch orange |
| test_assets/NormalsTextureBiasAndScale/NormalsTextureBiasAndScale.usda | the three cubes are light grey and their R glyphs read the right way round, matching the reference |
| test_assets/NormalsTextureBiasAndScale/NormalsTextureBiasAndScale.usdz | same as the .usda: light grey cubes with correctly oriented glyphs |
| test_assets/RoughnessTest/RoughnessTest.usdz | the six bands read the same across the Texture / Constant seam, and a front view puts the specular highlight on the 0.33 band and none on 0.00, as the reference does; the survey's three-quarter framing keeps that highlight off the card |
| test_assets/TextureCoordinateTest/TextureCoordinateTest.usda | the four quadrants carry the reference's yellow, red, blue and green and every label reads the right way round |
| test_assets/TextureFileFormatTests/all_files.usda | both panels load through the file's own camera with their labels the right way round; the eight 8-bit tiles carry their gradient and the 16-bit, 32-bit and CMYK tiles render blank |
| test_assets/TextureTransformTest/TextureTransformTest.usd | the upper row's U, V and UV tiles match the reference's green, blue and teal checks, and, framed face on, the lower row's rotated, scaled and rotated-plus-scaled quads land where usdview puts them: cropped per quad against the reference they correlate at 0.98, 0.94 and 0.94, against 0.97 to 0.98 for the untransformed upper row. The small Correct / Not Supported / Error cards sit near the model's origin rather than inside their quads because the asset itself un-nested those prims in 2023, after the reference screenshot was taken; against the 2022 revision of the file they land where the reference shows them. |

## Entries

`authored` is the prim count `describe_usd_file` reports for the file;
`prims` / `meshes` / `materials` / `lights` are what the loaded scene holds.

| Folder | Entry file | Load | Authored | Prims | Meshes | Composed | Mats | Lights | Storm | Warnings and errors | Screenshot | Verdict |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |
| full_assets/CarbonFrameBike | CarbonFrameBike.usdz | ok | 844 | 965 | 386 | 331 | 31 | 0 | 0.46 | 361 warning; Warning: Used fallback smooth normal | logs/usd_wg_survey/full_assets_CarbonFrameBike_CarbonFrameBike.usdz.png | works, gap: Warning: Used fallback smooth normal |
| full_assets/ElephantWithMonochord | SoC-ElephantWithMonochord.usdc | ok | 23 | 110 | 32 | 3 | 2 | 0 | 0.00 | 17 warning; Nbit sRGB texture is converted to fp32 sRGB texture(without | logs/usd_wg_survey/full_assets_ElephantWithMonochord_SoC-ElephantWithMonochord.usdc.png | works, gap: Nbit sRGB texture is converted to fp32 sRGB texture(without linearlization) |
| full_assets/McUsd | McUsd.usda | ok | 174 | 129 | 23 | 23 | 23 | 1 | 0.53 | 108 warning; Nbit sRGB texture is converted to fp32 sRGB texture(without | logs/usd_wg_survey/full_assets_McUsd_McUsd.usda.png | works, gap: Nbit sRGB texture is converted to fp32 sRGB texture(without linearlization) |
| full_assets/McUsd | McUsd.usdz | ok | 174 | 129 | 23 | 23 | 23 | 1 | 0.53 | 108 warning; Nbit sRGB texture is converted to fp32 sRGB texture(without | logs/usd_wg_survey/full_assets_McUsd_McUsd.usdz.png | works, gap: Nbit sRGB texture is converted to fp32 sRGB texture(without linearlization) |
| full_assets/McUsd | McUsd_10cm.usda | ok | 174 | 129 | 23 | 23 | 23 | 1 | 0.53 | 108 warning; Nbit sRGB texture is converted to fp32 sRGB texture(without | logs/usd_wg_survey/full_assets_McUsd_McUsd_10cm.usda.png | works, gap: Nbit sRGB texture is converted to fp32 sRGB texture(without linearlization) |
| full_assets/McUsd | McUsd_10cm.usdz | ok | 174 | 129 | 23 | 23 | 23 | 1 | 0.53 | 108 warning; Nbit sRGB texture is converted to fp32 sRGB texture(without | logs/usd_wg_survey/full_assets_McUsd_McUsd_10cm.usdz.png | works, gap: Nbit sRGB texture is converted to fp32 sRGB texture(without linearlization) |
| full_assets/OpenChessSet | chess_set.usda | ok | 26 | 269 | 53 | 21 | 0 | 0 | 0.73 | 28 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/full_assets_OpenChessSet_chess_set.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| full_assets/StandardShaderBall | standard_shader_ball_scene.usda | ok | 101 | 55 | 10 | 12 | 13 | 5 | -0.02 | 94 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/full_assets_StandardShaderBall_standard_shader_ball_scene.usda.png | works, gap: 10 of the 12 composed meshes loaded |
| full_assets/SubdivisionSurfaces | Creases_SpinningPyramids.usda | ok | 5 | 41 | 6 | 3 | 0 | 0 | 0.00 | 26 warning; USD prim '*': variant set '*' authors N opinion(s) that erhe | logs/usd_wg_survey/full_assets_SubdivisionSurfaces_Creases_SpinningPyramids.usda.png | works, gap: the scene's bounds are off the composed stage's by 826% of its diagonal |
| full_assets/Teapot | DrawModes.usd | ok | 37 | 170 | 0 | 35 | 0 | 0 | 0.15 | 9 warning; USD prim '*': variant set '*' authors N opinion(s) that erhe | logs/usd_wg_survey/full_assets_Teapot_DrawModes.usd.png | works, gap: no mesh loaded (Tydra converts no geometry for the NodeGraph schema) |
| full_assets/Teapot | Teapot.usd | ok | 1 | 3 | 0 | 1 | 0 | 0 | -0.08 | 1 warning; USD prim '*': variant set '*' authors N opinion(s) that erhe | logs/usd_wg_survey/full_assets_Teapot_Teapot.usd.png | works, gap: no mesh loaded (the geometry sits behind a variant opinion that is not a material binding) |
| full_assets/UsdCookie | UsdCookie.usdz | ok | 8 | 7 | 1 | 1 | 1 | 0 | 0.97 | 3 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/full_assets_UsdCookie_UsdCookie.usdz.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/vehicles/4wd/asset | 4wdBodyAsset.usda | ok | 9 | 16 | 1 | 1 | 6 | 0 | 0.97 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_vehicles_4wd_asset_4wdBodyAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/vehicles/4wd/asset | 4wdFullAsset.usda | ok | 6 | 338 | 25 | 5 | 62 | 0 | 0.83 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_vehicles_4wd_asset_4wdFullAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/vehicles/ambulance/asset | ambulanceBodyAsset.usda | ok | 10 | 18 | 1 | 1 | 7 | 0 | 0.93 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_vehicles_ambulance_asset_ambulanceBodyAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/vehicles/ambulance/asset | ambulanceFullAsset.usda | ok | 6 | 340 | 25 | 5 | 63 | 0 | 0.90 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_vehicles_ambulance_asset_ambulanceFullAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/vehicles/formula/asset | formulaBodyAsset.usda | ok | 7 | 12 | 1 | 1 | 4 | 0 | 0.95 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_vehicles_formula_asset_formulaBodyAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/vehicles/formula/asset | formulaFullAsset.usda | ok | 6 | 334 | 25 | 5 | 60 | 0 | 0.85 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_vehicles_formula_asset_formulaFullAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/vehicles/sedan/asset | sedanBodyAsset.usda | ok | 8 | 14 | 1 | 1 | 5 | 0 | 0.96 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_vehicles_sedan_asset_sedanBodyAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/vehicles/sedan/asset | sedanFullAsset.usda | ok | 6 | 336 | 25 | 5 | 61 | 0 | 0.88 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_vehicles_sedan_asset_sedanFullAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/vehicles/tractor/asset | tractorBodyAsset.usda | ok | 11 | 18 | 2 | 2 | 6 | 0 | 0.96 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_vehicles_tractor_asset_tractorBodyAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/vehicles/tractor/asset | tractorFullAsset.usda | ok | 6 | 340 | 26 | 6 | 62 | 0 | 0.87 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_vehicles_tractor_asset_tractorFullAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/vehicles/truckFlat/asset | truckFlatBodyAsset.usda | ok | 9 | 16 | 1 | 1 | 6 | 0 | 0.96 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_vehicles_truckFlat_asset_truckFlatBodyAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/vehicles/truckFlat/asset | truckFlatFullAsset.usda | ok | 6 | 338 | 25 | 5 | 62 | 0 | 0.94 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_vehicles_truckFlat_asset_truckFlatFullAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/vehicles/van/asset | vanBodyAsset.usda | ok | 9 | 16 | 1 | 1 | 6 | 0 | 0.92 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_vehicles_van_asset_vanBodyAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/vehicles/van/asset | vanFullAsset.usda | ok | 6 | 338 | 25 | 5 | 62 | 0 | 0.81 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_vehicles_van_asset_vanFullAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/vehicles | vehicleVariants.usda | ok | 8 | 2372 | 176 | 6 | 432 | 0 | 0.87 | 396 error, 198 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_vehicles_vehicleVariants.usda.png | works, gap: the scene's bounds are off the composed stage's by 11% of its diagonal |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/wheels/wheelBlack/asset | wheelBlackAsset.usda | ok | 8 | 11 | 1 | 1 | 2 | 0 | 0.95 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_wheels_wheelBlack_asset_wheelBlackAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/wheels/wheelLargeRim/asset | wheelLargeRimAsset.usda | ok | 8 | 11 | 1 | 1 | 2 | 0 | 0.96 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_wheels_wheelLargeRim_asset_wheelLargeRimAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/wheels/wheelNormal/asset | wheelNormalAsset.usda | ok | 8 | 11 | 1 | 1 | 2 | 0 | 0.96 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_wheels_wheelNormal_asset_wheelNormalAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/wheels/wheelRed/asset | wheelRedAsset.usda | ok | 10 | 14 | 1 | 1 | 3 | 0 | 0.96 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_wheels_wheelRed_asset_wheelRedAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/wheels | wheelVariants.usda | ok | 7 | 79 | 6 | 1 | 14 | 0 | 0.96 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_wheels_wheelVariants.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/wheels/wheelVintage/asset | wheelVintageAsset.usda | ok | 10 | 14 | 1 | 1 | 3 | 0 | 0.92 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_wheels_wheelVintage_asset_wheelVintageAsset.usda.png | works |
| full_assets/Vehicles/USD_Mini_Car_Kit/assets/wheels/wheelWide/asset | wheelWideAsset.usda | ok | 8 | 11 | 1 | 1 | 2 | 0 | 0.96 | none | logs/usd_wg_survey/full_assets_Vehicles_USD_Mini_Car_Kit_assets_wheels_wheelWide_asset_wheelWideAsset.usda.png | works |
| intent-vfx/scenes | simpleAssetScene.usd | ok | 659 | 9861 | 2056 | 0 | 0 | 0 | -0.01 | none | logs/usd_wg_survey/intent-vfx_scenes_simpleAssetScene.usd.png | works |
| intent-vfx/scenes | teapotScene.usd | ok | 690 | 8533 | 988 | 524 | 0 | 0 | 0.77 | 925 error, 58 warning; breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: scene_commit | logs/usd_wg_survey/intent-vfx_scenes_teapotScene.usd.png | works, gap: breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: scene_commit_queue flush |
| intent-vfx/scenes | teapotScene_animCycle.usd | ok | 664 | 664 | 0 | 0 | 0 | 0 | 0.00 | none | logs/usd_wg_survey/intent-vfx_scenes_teapotScene_animCycle.usd.png | works |
| intent-vfx/scenes | teapotScene_camera.usd | ok | 3 | 3 | 0 | 0 | 0 | 0 | 0.00 | 1 warning; USD prim '*': its time-sampled transform is not animated - t | logs/usd_wg_survey/intent-vfx_scenes_teapotScene_camera.usd.png | works, gap: USD prim '*': its time-sampled transform is not animated - the stack has a suffi |
| intent-vfx/scenes | teapotScene_layout.usd | ok | 588 | 8807 | 1023 | 494 | 0 | 0 | 0.67 | 1980 error, 45 warning; Main loop STALLED: tick has not progressed for N s. Stuck in | logs/usd_wg_survey/intent-vfx_scenes_teapotScene_layout.usd.png | works, gap: Main loop STALLED: tick has not progressed for N s. Stuck in phase: '*' (tick th |
| intent-vfx/scenes | teapotScene_layoutOverrides.usd | ok | 492 | 701 | 30 | 0 | 0 | 0 | 0.00 | 30 warning; USD prim '*' inherits from '*', which names no prim of the f | logs/usd_wg_survey/intent-vfx_scenes_teapotScene_layoutOverrides.usd.png | works, gap: USD prim '*' inherits from '*', which names no prim of the file |
| test_assets/AlphaBlendModeTest | AlphaBlendModeTest.usd | ok | 37 | 22 | 9 | 9 | 6 | 0 | 0.97 | 31 warning; Nbit sRGB texture is converted to fp32 sRGB texture(without | logs/usd_wg_survey/test_assets_AlphaBlendModeTest_AlphaBlendModeTest.usd.png | works, gap: Nbit sRGB texture is converted to fp32 sRGB texture(without linearlization) |
| test_assets/AlphaBlendSortTest | AlphaBlendSortTest.usda | ok | 17 | 18 | 3 | 3 | 2 | 0 | 0.57 | 1 warning; USD material '*': inputs:opacity reads an image of its own - | logs/usd_wg_survey/test_assets_AlphaBlendSortTest_AlphaBlendSortTest.usda.png | works, gap: USD material '*': inputs:opacity reads an image of its own - erhe takes the alph |
| test_assets/ColorSpaceTests/MaterialX | mtlx_by_reference.usda | ok | 4 | 4 | 1 | 1 | 0 | 0 | 0.66 | 3 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_ColorSpaceTests_MaterialX_mtlx_by_reference.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/ColorSpaceTests/MaterialX | orange_squares_8x6_lin_rec709.usda | failed | - | 0 | 0 | 1 | 0 | 0 | - | 30 error; Loading USD stage '*' failed: Failed to parse USDA | logs/usd_wg_survey/test_assets_ColorSpaceTests_MaterialX_orange_squares_8x6_lin_rec709.usda.png | fails: no scene appeared within 400 s |
| test_assets/ColorSpaceTests/MaterialX | orange_squares_8x6_lin_rec709_csapi.usda | ok | 43 | 7 | 1 | 1 | 1 | 0 | 0.79 | 5 warning; <path>'*'t authored; producing an unshaded material. (set ma | logs/usd_wg_survey/test_assets_ColorSpaceTests_MaterialX_orange_squares_8x6_lin_rec709_csapi.usda.png | works, gap: <path>'*'t authored; producing an unshaded material. (set material_config.strict |
| test_assets/ColorSpaceTests/UsdPreviewSurface | usduvtexture_color_test.usda | ok | 189 | 95 | 37 | 37 | 37 | 0 | 0.95 | 40 warning; Loading USD stage '*': <path> Attribute `*`: `*` is not an a | logs/usd_wg_survey/test_assets_ColorSpaceTests_UsdPreviewSurface_usduvtexture_color_test.usda.png | works, gap: Loading USD stage '*': <path> Attribute `*`: `*` is not an allowed token. Ignore |
| test_assets/MaterialXTest | basic.usda | ok | 2 | 2 | 1 | 0 | 0 | 0 | 0.87 | 1 warning; USD prim '*' references '*': a MaterialX document is not a U | logs/usd_wg_survey/test_assets_MaterialXTest_basic.usda.png | works, gap: USD prim '*' references '*': a MaterialX document is not a USD layer - the arc i |
| test_assets/MaterialXTest | basicTextured.usda | ok | 2 | 4 | 1 | 1 | 0 | 0 | -0.95 | 8 warning; USD prim '*': variant '*' of set '*' binds '*' to material ' | logs/usd_wg_survey/test_assets_MaterialXTest_basicTextured.usda.png | works, gap: USD prim '*': variant '*' of set '*' binds '*' to material '*', which the file h |
| test_assets/MaterialXTest | basicTextured_flatten.usda | ok | 18 | 11 | 1 | 1 | 2 | 0 | 0.07 | 71 warning; <path>'*'t authored; producing an unshaded material. (set ma | logs/usd_wg_survey/test_assets_MaterialXTest_basicTextured_flatten.usda.png | works, gap: <path>'*'t authored; producing an unshaded material. (set material_config.strict |
| test_assets/MaterialXTest | basic_flatten.usda | ok | 7 | 5 | 1 | 0 | 1 | 0 | 0.93 | 22 warning; <path>'*'t authored; producing an unshaded material. (set ma | logs/usd_wg_survey/test_assets_MaterialXTest_basic_flatten.usda.png | works, gap: <path>'*'t authored; producing an unshaded material. (set material_config.strict |
| test_assets/NormalsTextureBiasAndScale | NormalsTextureBiasAndScale.usda | ok | 24 | 19 | 3 | 3 | 3 | 4 | 0.94 | 3 warning; Nbit sRGB texture is converted to fp32 sRGB texture(without | logs/usd_wg_survey/test_assets_NormalsTextureBiasAndScale_NormalsTextureBiasAndScale.usda.png | works, gap: Nbit sRGB texture is converted to fp32 sRGB texture(without linearlization) |
| test_assets/NormalsTextureBiasAndScale | NormalsTextureBiasAndScale.usdz | ok | 24 | 19 | 3 | 3 | 3 | 4 | 0.94 | 3 warning; Nbit sRGB texture is converted to fp32 sRGB texture(without | logs/usd_wg_survey/test_assets_NormalsTextureBiasAndScale_NormalsTextureBiasAndScale.usdz.png | works, gap: Nbit sRGB texture is converted to fp32 sRGB texture(without linearlization) |
| test_assets/References | OverridingReferencedInternalReferencesTest.usda | ok | 16 | 53 | 10 | 8 | 10 | 3 | 0.80 | 13 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_References_OverridingReferencedInternalReferencesTest.usda.png | works, gap: the scene's bounds are off the composed stage's by 19% of its diagonal |
| test_assets/RelationshipEncapsulationTests | ExternalReferenceBadTargetTest.usda | ok | 11 | 11 | 2 | 2 | 1 | 3 | 0.75 | 2 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_RelationshipEncapsulationTests_ExternalReferenceBadTargetTest.usda.png | works, gap: the scene's bounds are off the composed stage's by 20% of its diagonal |
| test_assets/RelationshipEncapsulationTests | InternalReferenceTest.usda | ok | 24 | 34 | 9 | 7 | 2 | 3 | 0.80 | 9 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_RelationshipEncapsulationTests_InternalReferenceTest.usda.png | works, gap: the scene's bounds are off the composed stage's by 19% of its diagonal |
| test_assets/RelationshipEncapsulationTests | SublayeredInternalReferenceTest.usda | ok | 24 | 34 | 9 | 7 | 2 | 3 | 0.80 | 9 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_RelationshipEncapsulationTests_SublayeredInternalReferenceTest.usda.png | works, gap: the scene's bounds are off the composed stage's by 19% of its diagonal |
| test_assets/RoughnessTest | RoughnessTest.usdz | ok | 49 | 26 | 6 | 6 | 6 | 0 | 0.92 | 12 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_RoughnessTest_RoughnessTest.usdz.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/TextureCoordinateTest | TextureCoordinateTest.usda | ok | 39 | 28 | 5 | 5 | 5 | 0 | 0.90 | 11 warning; Nbit sRGB texture is converted to fp32 sRGB texture(without | logs/usd_wg_survey/test_assets_TextureCoordinateTest_TextureCoordinateTest.usda.png | works, gap: Nbit sRGB texture is converted to fp32 sRGB texture(without linearlization) |
| test_assets/TextureCoordinateTest | TextureCoordinateTestMaterialX.usda | ok | 21 | 21 | 5 | 5 | 0 | 0 | 0.91 | 11 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_TextureCoordinateTest_TextureCoordinateTestMaterialX.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/TextureFileFormatTests | all_files.usda | ok | 12 | 76 | 24 | 24 | 16 | 0 | 0.12 | 8 warning; Nbit sRGB texture is converted to fp32 sRGB texture(without | logs/usd_wg_survey/test_assets_TextureFileFormatTests_all_files.usda.png | works, gap: the scene's bounds are off the composed stage's by 57% of its diagonal |
| test_assets/TextureFileFormatTests | jpeg_cmyk_8-bit.usda | ok | 14 | 10 | 3 | 3 | 2 | 0 | 0.98 | 1 warning; Nbit sRGB texture is converted to fp32 sRGB texture(without | logs/usd_wg_survey/test_assets_TextureFileFormatTests_jpeg_cmyk_8-bit.usda.png | works, gap: Nbit sRGB texture is converted to fp32 sRGB texture(without linearlization) |
| test_assets/TextureFileFormatTests | jpeg_grayscale_8-bit.usda | ok | 14 | 10 | 3 | 3 | 2 | 0 | 0.97 | 1 warning; Nbit sRGB texture is converted to fp32 sRGB texture(without | logs/usd_wg_survey/test_assets_TextureFileFormatTests_jpeg_grayscale_8-bit.usda.png | works, gap: Nbit sRGB texture is converted to fp32 sRGB texture(without linearlization) |
| test_assets/TextureFileFormatTests | jpeg_rgb_8-bit.usda | ok | 14 | 10 | 3 | 3 | 2 | 0 | 0.97 | 1 warning; Nbit sRGB texture is converted to fp32 sRGB texture(without | logs/usd_wg_survey/test_assets_TextureFileFormatTests_jpeg_rgb_8-bit.usda.png | works, gap: Nbit sRGB texture is converted to fp32 sRGB texture(without linearlization) |
| test_assets/TextureTransformTest | TextureTransformTest.usd | ok | 56 | 29 | 12 | 12 | 9 | 0 | 0.98 | 53 warning; Nbit sRGB texture is converted to fp32 sRGB texture(without | logs/usd_wg_survey/test_assets_TextureTransformTest_TextureTransformTest.usd.png | works, gap: Nbit sRGB texture is converted to fp32 sRGB texture(without linearlization) |
| test_assets/USDZ/AnimatedCube | AnimatedCube.usdz | ok | 9 | 10 | 1 | 1 | 1 | 0 | 0.99 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_USDZ_AnimatedCube_AnimatedCube.usdz.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/USDZ/AnimatedTriangle | AnimatedTriangle.usdz | ok | 6 | 7 | 1 | 1 | 1 | 0 | 0.98 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_USDZ_AnimatedTriangle_AnimatedTriangle.usdz.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/USDZ/BoxAnimated | BoxAnimated.usdz | ok | 11 | 11 | 2 | 2 | 2 | 0 | 0.98 | 3 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_USDZ_BoxAnimated_BoxAnimated.usdz.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/USDZ/BrainStem | BrainStem.usdz | ok | 187 | 189 | 78 | 59 | 59 | 0 | 0.54 | 116 error, 59 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_USDZ_BrainStem_BrainStem.usdz.png | works, gap: skin Skeleton already in scene cameras |
| test_assets/USDZ/CesiumMan | CesiumMan.usdz | ok | 14 | 74 | 20 | 1 | 1 | 0 | 0.60 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_USDZ_CesiumMan_CesiumMan.usdz.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/USDZ/DamagedHelmet | DamagedHelmet.usdz | ok | 12 | 11 | 1 | 1 | 1 | 0 | 0.74 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_USDZ_DamagedHelmet_DamagedHelmet.usdz.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/USDZ/InterpolationTest | InterpolationTest.usdz | ok | 37 | 29 | 10 | 10 | 10 | 0 | 0.99 | 10 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_USDZ_InterpolationTest_InterpolationTest.usdz.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/USDZ/RiggedFigure | RiggedFigure.usdz | ok | 12 | 72 | 20 | 1 | 1 | 0 | 0.80 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_USDZ_RiggedFigure_RiggedFigure.usdz.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/USDZ/RiggedSimple | RiggedSimple.usdz | ok | 12 | 21 | 3 | 1 | 1 | 0 | 0.53 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_USDZ_RiggedSimple_RiggedSimple.usdz.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/_common | animated_cube_translation.usda | ok | 3 | 5 | 1 | 1 | 0 | 0 | 0.69 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets__common_animated_cube_translation.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/_common | axis.usda | ok | 4 | 4 | 3 | 0 | 0 | 0 | 0.75 | none | logs/usd_wg_survey/test_assets__common_axis.usda.png | works |
| test_assets/_common | teapot.usda | ok | 2 | 2 | 1 | 1 | 0 | 0 | 0.93 | 3 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets__common_teapot.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/foundation/stage_composition | active.usda | ok | 3 | 3 | 2 | 0 | 0 | 0 | 0.92 | none | logs/usd_wg_survey/test_assets_foundation_stage_composition_active.usda.png | works, gap: the scene's bounds are off the composed stage's by 87% of its diagonal |
| test_assets/foundation/stage_composition | class_inherit.usda | ok | 4 | 4 | 2 | 0 | 0 | 0 | 0.90 | none | logs/usd_wg_survey/test_assets_foundation_stage_composition_class_inherit.usda.png | works |
| test_assets/foundation/stage_composition | inherit_and_specialize.usda | ok | 7 | 10 | 6 | 0 | 0 | 0 | 0.90 | 3 warning; USD prim '*': the referencing layer defines prims over the r | logs/usd_wg_survey/test_assets_foundation_stage_composition_inherit_and_specialize.usda.png | works, gap: USD prim '*': the referencing layer defines prims over the reference (source) - |
| test_assets/foundation/stage_composition | over.usda | ok | 4 | 4 | 3 | 0 | 0 | 0 | 0.59 | none | logs/usd_wg_survey/test_assets_foundation_stage_composition_over.usda.png | works, gap: the scene's bounds are off the composed stage's by 52% of its diagonal |
| test_assets/foundation/stage_composition/payload | payload_child_folder.usda | ok | 1 | 3 | 1 | 0 | 0 | 0 | 0.04 | none | logs/usd_wg_survey/test_assets_foundation_stage_composition_payload_payload_child_folder.usda.png | works, gap: the scene's bounds are off the composed stage's by 4950% of its diagonal |
| test_assets/foundation/stage_composition/payload | payload_invalid.usda | ok | 3 | 3 | 1 | 0 | 0 | 0 | 0.94 | 2 error; Prefab source file not found: <path> | logs/usd_wg_survey/test_assets_foundation_stage_composition_payload_payload_invalid.usda.png | works, gap: Prefab source file not found: <path> |
| test_assets/foundation/stage_composition/payload | payload_parent_folder.usda | ok | 1 | 3 | 1 | 0 | 0 | 0 | 0.94 | none | logs/usd_wg_survey/test_assets_foundation_stage_composition_payload_payload_parent_folder.usda.png | works |
| test_assets/foundation/stage_composition/payload | payload_same_folder.usda | ok | 1 | 3 | 1 | 0 | 0 | 0 | 0.94 | none | logs/usd_wg_survey/test_assets_foundation_stage_composition_payload_payload_same_folder.usda.png | works |
| test_assets/foundation/stage_composition/references | reference_child_folder.usda | ok | 1 | 3 | 1 | 0 | 0 | 0 | 0.94 | none | logs/usd_wg_survey/test_assets_foundation_stage_composition_references_reference_child_folder.usda.png | works |
| test_assets/foundation/stage_composition/references | reference_invalid.usda | ok | 3 | 3 | 1 | 0 | 0 | 0 | 0.94 | 2 error; Prefab source file not found: <path> | logs/usd_wg_survey/test_assets_foundation_stage_composition_references_reference_invalid.usda.png | works, gap: Prefab source file not found: <path> |
| test_assets/foundation/stage_composition/references | reference_parent_folder.usda | ok | 1 | 3 | 1 | 0 | 0 | 0 | 0.94 | none | logs/usd_wg_survey/test_assets_foundation_stage_composition_references_reference_parent_folder.usda.png | works |
| test_assets/foundation/stage_composition/references | reference_same_folder.usda | ok | 1 | 3 | 1 | 0 | 0 | 0 | 0.94 | none | logs/usd_wg_survey/test_assets_foundation_stage_composition_references_reference_same_folder.usda.png | works |
| test_assets/foundation/stage_composition/references_prim | reference_prim_in_other_file.usda | ok | 4 | 5 | 1 | 0 | 0 | 0 | 0.94 | 5 error; Prefab '*': prim '*' is not in '*' | logs/usd_wg_survey/test_assets_foundation_stage_composition_references_prim_reference_prim_in_other_file.usda.png | works, gap: Prefab '*': prim '*' is not in '*' |
| test_assets/foundation/stage_composition/references_prim | reference_prim_in_same_file.usda | ok | 4 | 4 | 1 | 0 | 0 | 0 | 0.94 | 6 error; Prefab '*': prim '*' is not in '*' | logs/usd_wg_survey/test_assets_foundation_stage_composition_references_prim_reference_prim_in_same_file.usda.png | works, gap: Prefab '*': prim '*' is not in '*' |
| test_assets/foundation/stage_composition/subLayer | sublayer_child_folder.usda | ok | 2 | 2 | 1 | 0 | 0 | 0 | 0.94 | none | logs/usd_wg_survey/test_assets_foundation_stage_composition_subLayer_sublayer_child_folder.usda.png | works |
| test_assets/foundation/stage_composition/subLayer | sublayer_invalid.usda | ok | 2 | 2 | 1 | 0 | 0 | 0 | 0.94 | 4 warning; file_does_not_exist.usda not found in path: [ <path> ] | logs/usd_wg_survey/test_assets_foundation_stage_composition_subLayer_sublayer_invalid.usda.png | works, gap: file_does_not_exist.usda not found in path: [ <path> ] |
| test_assets/foundation/stage_composition/subLayer | sublayer_parent_folder.usda | ok | 2 | 2 | 1 | 0 | 0 | 0 | 0.94 | none | logs/usd_wg_survey/test_assets_foundation_stage_composition_subLayer_sublayer_parent_folder.usda.png | works |
| test_assets/foundation/stage_composition/subLayer | sublayer_same_folder.usda | ok | 2 | 2 | 1 | 0 | 0 | 0 | 0.94 | none | logs/usd_wg_survey/test_assets_foundation_stage_composition_subLayer_sublayer_same_folder.usda.png | works |
| test_assets/foundation/stage_configuration/framesPerSecond | framesPerSecond_-1.usda | ok | 6 | 8 | 4 | - | 0 | 0 | - | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_foundation_stage_configuration_framesPerSecond_framesPerSecond_-1.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/foundation/stage_configuration/framesPerSecond | framesPerSecond_0.usda | ok | 6 | 8 | 4 | - | 0 | 0 | - | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_foundation_stage_configuration_framesPerSecond_framesPerSecond_0.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/foundation/stage_configuration/framesPerSecond | framesPerSecond_1.usda | ok | 6 | 8 | 4 | 1 | 0 | 0 | 0.74 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_foundation_stage_configuration_framesPerSecond_framesPerSecond_1.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/foundation/stage_configuration/framesPerSecond | framesPerSecond_100.usda | ok | 6 | 8 | 4 | 1 | 0 | 0 | 0.74 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_foundation_stage_configuration_framesPerSecond_framesPerSecond_100.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/foundation/stage_configuration/framesPerSecond_timeCodesPerSecond_mixed | 24_24.usda | ok | 6 | 8 | 4 | 1 | 0 | 0 | 0.74 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_foundation_stage_configuration_framesPerSecond_timeCodesPerSecond_mixed_24_24.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/foundation/stage_configuration/framesPerSecond_timeCodesPerSecond_mixed | 24_48.usda | ok | 6 | 8 | 4 | 1 | 0 | 0 | 0.74 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_foundation_stage_configuration_framesPerSecond_timeCodesPerSecond_mixed_24_48.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/foundation/stage_configuration/framesPerSecond_timeCodesPerSecond_mixed | 48_24.usda | ok | 6 | 8 | 4 | 1 | 0 | 0 | 0.74 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_foundation_stage_configuration_framesPerSecond_timeCodesPerSecond_mixed_48_24.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/foundation/stage_configuration/framesPerSecond_timeCodesPerSecond_mixed | 48_48.usda | ok | 6 | 8 | 4 | 1 | 0 | 0 | 0.74 | none | logs/usd_wg_survey/test_assets_foundation_stage_configuration_framesPerSecond_timeCodesPerSecond_mixed_48_48.usda.png | works |
| test_assets/foundation/stage_configuration/invalid_defaultPrim | invalid_defaultPrim.usda | ok | 2 | 2 | 1 | 0 | 0 | 0 | 0.94 | none | logs/usd_wg_survey/test_assets_foundation_stage_configuration_invalid_defaultPrim_invalid_defaultPrim.usda.png | works |
| test_assets/foundation/stage_configuration/metersPerUnit | metersPerUnit_1.usda | ok | 5 | 5 | 4 | 0 | 0 | 0 | 0.91 | 30 error; Item_base::set_flag_bits(true) on '*': Visible is a property | logs/usd_wg_survey/test_assets_foundation_stage_configuration_metersPerUnit_metersPerUnit_1.usda.png | works, gap: Item_base::set_flag_bits(true) on '*': Visible is a property (Item_base::visible |
| test_assets/foundation/stage_configuration/metersPerUnit | metersPerUnit_10.usda | ok | 5 | 5 | 4 | 0 | 0 | 0 | 0.91 | none | logs/usd_wg_survey/test_assets_foundation_stage_configuration_metersPerUnit_metersPerUnit_10.usda.png | works |
| test_assets/foundation/stage_configuration/metersPerUnit | metersPerUnit_mix.usda | ok | 7 | 7 | 6 | 0 | 0 | 0 | 0.92 | none | logs/usd_wg_survey/test_assets_foundation_stage_configuration_metersPerUnit_metersPerUnit_mix.usda.png | works |
| test_assets/foundation/stage_configuration/multiple_root_prims | multiple_root_prims_no_defaultPrim.usda | ok | 2 | 2 | 2 | 0 | 0 | 0 | 0.93 | none | logs/usd_wg_survey/test_assets_foundation_stage_configuration_multiple_root_prims_multiple_root_prims_no_defaultPrim.usda.png | works |
| test_assets/foundation/stage_configuration/multiple_root_prims | multiple_root_prims_with_defaultPrim.usda | ok | 2 | 2 | 2 | 0 | 0 | 0 | 0.93 | none | logs/usd_wg_survey/test_assets_foundation_stage_configuration_multiple_root_prims_multiple_root_prims_with_defaultPrim.usda.png | works |
| test_assets/foundation/stage_configuration/start_end_timeCode | large_start_end_timeCodes.usda | ok | 6 | 8 | 4 | 1 | 0 | 0 | 0.74 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_foundation_stage_configuration_start_end_timeCode_large_start_end_timeCodes.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/foundation/stage_configuration/start_end_timeCode | missing_endTimeCode.usda | ok | 6 | 8 | 4 | 1 | 0 | 0 | 0.74 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_foundation_stage_configuration_start_end_timeCode_missing_endTimeCode.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/foundation/stage_configuration/start_end_timeCode | missing_startTimeCode.usda | ok | 6 | 8 | 4 | 1 | 0 | 0 | 0.74 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_foundation_stage_configuration_start_end_timeCode_missing_startTimeCode.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/foundation/stage_configuration/start_end_timeCode | missing_start_end_timeCodes.usda | ok | 6 | 8 | 4 | 1 | 0 | 0 | 0.74 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_foundation_stage_configuration_start_end_timeCode_missing_start_end_timeCodes.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/foundation/stage_configuration/timeCodesPerSecond | timeCodesPerSecond_-1.usda | ok | 6 | 8 | 4 | 1 | 0 | 0 | 0.74 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_foundation_stage_configuration_timeCodesPerSecond_timeCodesPerSecond_-1.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/foundation/stage_configuration/timeCodesPerSecond | timeCodesPerSecond_0.usda | ok | 6 | 8 | 4 | 1 | 0 | 0 | 0.74 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_foundation_stage_configuration_timeCodesPerSecond_timeCodesPerSecond_0.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/foundation/stage_configuration/timeCodesPerSecond | timeCodesPerSecond_1.usda | ok | 6 | 8 | 4 | 1 | 0 | 0 | 0.74 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_foundation_stage_configuration_timeCodesPerSecond_timeCodesPerSecond_1.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/foundation/stage_configuration/timeCodesPerSecond | timeCodesPerSecond_100.usda | ok | 6 | 8 | 4 | 1 | 0 | 0 | 0.74 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_foundation_stage_configuration_timeCodesPerSecond_timeCodesPerSecond_100.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/foundation/stage_configuration/upAxis | upAxis_X.usda | ok | 4 | 4 | 3 | 0 | 0 | 0 | 0.75 | 1 warning; USD stage up axis '*' has no erhe counterpart - imported as | logs/usd_wg_survey/test_assets_foundation_stage_configuration_upAxis_upAxis_X.usda.png | works, gap: USD stage up axis '*' has no erhe counterpart - imported as Y-up |
| test_assets/foundation/stage_configuration/upAxis | upAxis_Y.usda | ok | 4 | 4 | 3 | 0 | 0 | 0 | 0.75 | none | logs/usd_wg_survey/test_assets_foundation_stage_configuration_upAxis_upAxis_Y.usda.png | works |
| test_assets/foundation/stage_configuration/upAxis | upAxis_Z.usda | ok | 4 | 4 | 3 | 0 | 0 | 0 | 0.79 | 30 error; Item_base::set_flag_bits(true) on '*': Visible is a property | logs/usd_wg_survey/test_assets_foundation_stage_configuration_upAxis_upAxis_Z.usda.png | works, gap: Item_base::set_flag_bits(true) on '*': Visible is a property (Item_base::visible |
| test_assets/foundation/stage_configuration/upAxis | upAxis_invalid.usda | ok | 4 | 4 | 3 | 0 | 0 | 0 | 0.75 | 2 warning; Ignore unknown `*` value. Must be "*", "*" or "*", but got " | logs/usd_wg_survey/test_assets_foundation_stage_configuration_upAxis_upAxis_invalid.usda.png | works, gap: Ignore unknown `*` value. Must be "*", "*" or "*", but got "*"(Note: Case sensit |
| test_assets/schemaTests/usdGeom/extent | inverse_extent.usda | ok | 1 | 1 | 1 | 1 | 0 | 0 | 0.68 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_extent_inverse_extent.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/schemaTests/usdGeom/extent | no_extent.usda | ok | 1 | 1 | 1 | 1 | 0 | 0 | 0.68 | none | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_extent_no_extent.usda.png | works |
| test_assets/schemaTests/usdGeom/extent | regular_extent.usda | ok | 1 | 1 | 1 | 1 | 0 | 0 | 0.68 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_extent_regular_extent.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/schemaTests/usdGeom/extent | scaled_extent.usda | ok | 3 | 3 | 2 | 2 | 0 | 0 | 0.69 | 2 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_extent_scaled_extent.usda.png | works, gap: the scene's bounds are off the composed stage's by 17% of its diagonal |
| test_assets/schemaTests/usdGeom/meshes/5_face | 5_face.usda | ok | 1 | 1 | 1 | 1 | 0 | 0 | 0.60 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_meshes_5_face_5_face.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/schemaTests/usdGeom/meshes/doubleSided | doubleSided_quad.usda | ok | 3 | 3 | 2 | 2 | 0 | 0 | 0.97 | 2 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_meshes_doubleSided_doubleSided_quad.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/schemaTests/usdGeom/meshes/empty_mesh | empty.usda | ok | 1 | 1 | 0 | 1 | 0 | 0 | 0.00 | none | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_meshes_empty_mesh_empty.usda.png | works, gap: no mesh loaded (cause not in the log) |
| test_assets/schemaTests/usdGeom/meshes/mixed_faceVertexCounts | mixed.usda | ok | 1 | 1 | 1 | 1 | 0 | 0 | 0.68 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_meshes_mixed_faceVertexCounts_mixed.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/schemaTests/usdGeom/meshes/normals_types | normalsTypes.usda | ok | 18 | 18 | 17 | 17 | 0 | 0 | 0.47 | 41 warning; Attribute `*` declared with a non-conformant type: Property | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_meshes_normals_types_normalsTypes.usda.png | works, gap: Attribute `*` declared with a non-conformant type: Property type mismatch. norma |
| test_assets/schemaTests/usdGeom/meshes/points_types | pointsTypes.usda | ok | 17 | 17 | 4 | 16 | 0 | 0 | 0.67 | 38 warning; Attribute `*` declared with a non-conformant type: Property | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_meshes_points_types_pointsTypes.usda.png | works, gap: 4 of the 16 composed meshes loaded |
| test_assets/schemaTests/usdGeom/meshes/quad_mesh | quads.usda | ok | 1 | 1 | 1 | 1 | 0 | 0 | 0.59 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_meshes_quad_mesh_quads.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/schemaTests/usdGeom/meshes/singleSided | singleSided.usda | ok | 3 | 3 | 2 | 2 | 0 | 0 | 0.96 | 2 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_meshes_singleSided_singleSided.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/schemaTests/usdGeom/meshes/subdiv_bilinear | subdiv_bilinear.usda | ok | 1 | 1 | 1 | 1 | 0 | 0 | 0.94 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_meshes_subdiv_bilinear_subdiv_bilinear.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/schemaTests/usdGeom/meshes/subdiv_catmullClark | subdiv_catmullClark.usda | ok | 1 | 1 | 1 | 1 | 0 | 0 | 0.59 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_meshes_subdiv_catmullClark_subdiv_catmullClark.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/schemaTests/usdGeom/meshes/subdiv_loop_quads | subdiv_loop_quads.usda | ok | 1 | 1 | 1 | 1 | 0 | 0 | - | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_meshes_subdiv_loop_quads_subdiv_loop_quads.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/schemaTests/usdGeom/meshes/subdiv_loop_triangles | subdiv_loop_triangles.usda | ok | 1 | 1 | 1 | 1 | 0 | 0 | 0.62 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_meshes_subdiv_loop_triangles_subdiv_loop_triangles.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/schemaTests/usdGeom/meshes/subdiv_none | subdiv_none.usda | ok | 1 | 1 | 1 | 1 | 0 | 0 | 0.94 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_meshes_subdiv_none_subdiv_none.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/schemaTests/usdGeom/meshes/triangled_mesh | triangles.usda | ok | 1 | 1 | 1 | 1 | 0 | 0 | 0.68 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_meshes_triangled_mesh_triangles.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/schemaTests/usdGeom/primitives | all_primitives.usda | ok | 6 | 6 | 5 | 0 | 0 | 0 | 0.90 | none | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_primitives_all_primitives.usda.png | works |
| test_assets/schemaTests/usdGeom/primitives | capsule.usda | ok | 1 | 1 | 1 | 0 | 0 | 0 | 0.88 | none | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_primitives_capsule.usda.png | works |
| test_assets/schemaTests/usdGeom/primitives | cone.usda | ok | 1 | 1 | 1 | 0 | 0 | 0 | 0.89 | none | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_primitives_cone.usda.png | works |
| test_assets/schemaTests/usdGeom/primitives | cube.usda | ok | 1 | 1 | 1 | 0 | 0 | 0 | 0.94 | none | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_primitives_cube.usda.png | works |
| test_assets/schemaTests/usdGeom/transforms | complex_transform.usda | ok | 3 | 7 | 4 | 1 | 0 | 0 | 0.39 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_transforms_complex_transform.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |
| test_assets/schemaTests/usdGeom/transforms | matrix_transform.usda | ok | 3 | 7 | 4 | 1 | 0 | 0 | 0.59 | none | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_transforms_matrix_transform.usda.png | works |
| test_assets/schemaTests/usdGeom/transforms | scopes_and_xforms_nested.usda | ok | 7 | 11 | 5 | 0 | 0 | 0 | 0.90 | none | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_transforms_scopes_and_xforms_nested.usda.png | works |
| test_assets/schemaTests/usdGeom/transforms | simple_transform.usda | ok | 3 | 7 | 4 | 1 | 0 | 0 | 0.59 | 1 warning; Attribute `*` does not exist in Prim <prim> | logs/usd_wg_survey/test_assets_schemaTests_usdGeom_transforms_simple_transform.usda.png | works, gap: Attribute `*` does not exist in Prim <prim> |

## Gaps

Each distinct failure, error or warning once, with the number of entry
assets it affects and what the editor would have to support to clear it.

| Assets | Kind | Cause | What the editor would have to support |
| ---: | --- | --- | --- |
| 68 | warning | Attribute `*` does not exist in Prim <prim> | nothing: LightUSD's Tydra probes every optional Gprim attribute (extent, doubleSided) for time samples and reports the ones a prim does not author; the schema fallback is used and the mesh loads (.cpm_cache lightusd src/tydra/render-data-anim.cc) |
| 16 | warning | Nbit sRGB texture is converted to fp32 sRGB texture(without linearlization) | read 16-bit and 32-bit sRGB images directly instead of Tydra's un-linearized float conversion, which loses the transfer function |
| 12 | note | Threading is disabled for this build. | nothing: the line reports how this build is configured |
| 12 | failure | the scene's bounds disagree with the composed stage's | place, scale or instance the content the way OpenUSD composes it: compare the prim's world transform with pxr's XformCache (scripts/usd_wg_pxr_stage.py reads the composed bounds). Only the root layer's upAxis / metersPerUnit apply to a stage, so a reference or payload target is loaded with erhe::usd::Stage_metrics::referenced and contributes no correction of its own |
| 5 | warning | MCP server: dropped expired '*' before processing | not a USD gap: editor-internal noise this survey happens to capture |
| 5 | warning | USD prim '*' references '*': a MaterialX document is not a USD layer - the arc is not instantiated | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 4 | warning | USD light '*': DomeLight texture '*' is not sampled - erhe has no environment map, so the dome contributes its constant color only | sample a DomeLight's texture as an environment map; erhe takes only the light's intensity and colour, so an HDRI-lit stage loses the image |
| 4 | warning | USD prim '*': variant set '*' authors N opinion(s) that erhe has no place for - they are not carried | carry variant opinions beyond material bindings; X4 reads bindings only (src/erhe/usd/notes.md, Variant sets) |
| 3 | warning | <path>'*'t authored; producing an unshaded material. (set material_config.strict_material_check=true to make this an error.) | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 3 | error | Prefab source file not found: <path> | resolve a reference asset path relative to the layer that authored it before opening it as a prefab template |
| 3 | warning | USD prim '*': variant '*' of set '*' binds '*' to material '*', which the file has no prim for - the binding is dropped | diagnose the message and add the support it asks for |
| 3 | warning | `*` is declared with no authored value; treating the primvar as un-indexed. | read an indexed primvar whose indices attribute is declared but carries no value |
| 2 | warning | ) [Not a NodeGraph]; using the parameter default instead. | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 2 | warning | Failed to convert UsdPreviewSurface : <path> | diagnose the message and add the support it asks for |
| 2 | error | Item_base::set_flag_bits(true) on '*': Visible is a property (Item_base::visible_property / Item_base::active_property / Mesh::shadow_cast_property / Mesh::lightmapped_property); the derived bits are dropped from the mask | diagnose the message and add the support it asks for |
| 2 | error | Main loop STALLED: tick has not progressed for N s. Stuck in phase: '*' (tick thread Nxca6af7ee846453e). | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | warning | MaterialX connection for normal could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 2 | warning | MaterialX connection for opacity could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 2 | error | Prefab '*' produced no nodes - not caching | instantiate a reference whose target prim path is absent from the target layer (Prefab_library::get_or_load, X1) |
| 2 | error | Prefab '*': prim '*' is not in '*' | instantiate a reference whose target prim path is absent from the target layer (Prefab_library::get_or_load, X1) |
| 2 | warning | TODO: Prim type NodeGraph | build geometry for the UsdGeom schemas Tydra does not convert (Cube, Sphere, Cone, Cylinder, Capsule, PointInstancer): the prim loads with no mesh (lightusd src/tydra/scene-access.cc) |
| 2 | warning | TODO: Prim type RenderSettings | build geometry for the UsdGeom schemas Tydra does not convert (Cube, Sphere, Cone, Cylinder, Capsule, PointInstancer): the prim loads with no mesh (lightusd src/tydra/scene-access.cc) |
| 2 | warning | USD material '*' has no UsdPreviewSurface shader - erhe material defaults are used | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 2 | warning | USD material '*': inputs:opacity reads an image of its own - erhe takes the alpha of the base color texture | diagnose the message and add the support it asks for |
| 2 | error | USD prim '*': failed to load reference target '*' (missing file, no prims, or a reference cycle - see log) | load a reference target that names a missing file, holds no prims, or closes a cycle (X1, src/editor/parsers/usd.cpp) |
| 2 | error | USD prim '*': failed to load reference target '*'<path> (missing file, no prims, or a reference cycle - see log) | load a reference target that names a missing file, holds no prims, or closes a cycle (X1, src/editor/parsers/usd.cpp) |
| 2 | warning | USD prim '*': variant '*' of set '*' binds material '*', which the file has no prim for - the binding is dropped | diagnose the message and add the support it asks for |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: Brdf_slice_rendergraph_node | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: Overlay for Default Viewport | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: Overlay for Viewport_scene_view N | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: Post processing for Default Viewport | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: Post processing for Viewport_scene_view N | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: Viewport window | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: config<path> | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: shadow_maps | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: asset loads | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: ddgi | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: draw_imgui_windows | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: fixed_step (physics) | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: flush_draw_lists | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: hotbar update | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: imgui process_events + commands | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: lightmap stream | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: rendergraph execute | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: scene_commit_queue flush | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: submit + end_frame | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: texture file loads | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: thumbnails update | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: update_hover_info | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: update_layout_nodes | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: update_material_sets | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: update_transforms | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | error | breadcrumb t=Ns thread=Nxca6af7ee846453e: tick: wait_frame | not a USD gap: editor-internal noise this survey happens to capture |
| 2 | warning | connection Path's property part must be `*`, `*`, `*`, `*` or `*` for UsdUVTexture, but got `*`(prim_part: <path>). | diagnose the message and add the support it asks for |
| 2 | failure | some composed meshes are not loaded | load every UsdGeomMesh the composed stage holds: the missing ones name the prim kind or the composition arc the importer skips |
| 1 | appearance | 16-bit, 32-bit and CMYK images do not load: their tiles render blank where the reference shows the same gradient the 8-bit tiles carry | decode the image depths and colour models the assets use beyond 8-bit RGB: 16-bit and 32-bit PNG and CMYK JPEG produce no texture, so their tiles render blank |
| 1 | warning | <path> ()():N Skipping animated attribute '*' for <path> due to unsupported or inconsistent sample type. | diagnose the message and add the support it asks for |
| 1 | warning | <path> Attribute `*`: `*` is not an allowed token. Ignore it. | diagnose the message and add the support it asks for |
| 1 | error | > uniform token info:id = "ND_... | diagnose the message and add the support it asks for |
| 1 | warning | Attribute `*` declared with a non-conformant type: Property type mismatch. normals expects type `*` but defined as type `*` Preserving the authored value as a custom property. | diagnose the message and add the support it asks for |
| 1 | warning | Attribute `*` declared with a non-conformant type: Property type mismatch. points expects type `*` but defined as type `*` Preserving the authored value as a custom property. | diagnose the message and add the support it asks for |
| 1 | warning | Composite subLayers failed. | diagnose the message and add the support it asks for |
| 1 | error | Error stack: | diagnose the message and add the support it asks for |
| 1 | warning | Failed to get texture coordinate for `*` : | diagnose the message and add the support it asks for |
| 1 | warning | Failed to load texture image: `*`. Skip loading. reason = Failed to load image file: Unknown image format. STB cannot decode image data for image: <path> | resolve a texture asset path against the layer that authored it, including inside a .usdz package |
| 1 | error | Failed to parse Attribute meta. | read the USDA constructs LightUSD's parser rejects; the file then loads as an empty stage |
| 1 | error | Failed to parse Prim attribute. | read the USDA constructs LightUSD's parser rejects; the file then loads as an empty stage |
| 1 | error | Failed to parse USDA | read the USDA constructs LightUSD's parser rejects; the file then loads as an empty stage |
| 1 | error | Failed to parse `*` block. | read the USDA constructs LightUSD's parser rejects; the file then loads as an empty stage |
| 1 | warning | Found shader <path> but it's not a supported MaterialX surface shader (expected ND_open_pbr_surface_surfaceshader or ND_standard_surface_surfaceshader or ND_UsdPreviewSurface_surfaceshader, got ); using default material appearance. | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | Ignore unknown `*` value. Must be "*", "*" or "*", but got "*"(Note: Case sensitive). Use default upAxis `*`. | diagnose the message and add the support it asks for |
| 1 | error | Loading USD stage '*' failed: Failed to parse USDA | read the USDA constructs LightUSD's parser rejects; the file then loads as an empty stage |
| 1 | warning | Loading USD stage '*': <path> Attribute `*`: `*` is not an allowed token. Ignore it. | diagnose the message and add the support it asks for |
| 1 | error | Main loop STALLED: tick has not progressed for N s. Stuck in phase: '*' (tick thread Nx5cbf17a9a104a862). | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | warning | MaterialX connection for base_diffuse_roughness could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for clearcoat could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for clearcoatRoughness could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for coat_affect_color could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for coat_affect_roughness could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for coat_anisotropy could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for coat_ior could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for coat_normal could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for coat_rotation could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for displacement could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for emission_color could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for emission_luminance could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for emissiveColor could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for metallic could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for occlusion could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for opacityThreshold could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for sheen_color could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for sheen_roughness could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for sheen_weight could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for specularColor could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for specular_anisotropy could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for specular_color could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for specular_ior could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for specular_rotation could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for subsurface_anisotropy could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for subsurface_color could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for subsurface_radius_scale could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for subsurface_scale could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for subsurface_weight could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for tangent could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for thin_film_ior could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for thin_film_thickness could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for transmission_color could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for transmission_depth could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for transmission_dispersion could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for transmission_scatter could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for transmission_scatter_anisotropy could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | MaterialX connection for transmission_weight could not be resolved to a texture or constant (<path> is not a NodeGraph, prim_type: Material | convert MaterialX and non-UsdPreviewSurface shading networks (plan step E2) |
| 1 | warning | Primvar `*` has no authored value (or an empty array); skipped. | read an indexed primvar whose indices attribute is declared but carries no value |
| 1 | warning | Skipping empty `*` declaration in SkelAnimation Prim : <<path>> | diagnose the message and add the support it asks for |
| 1 | warning | TODO: Prim type SpatialAudio | build geometry for the UsdGeom schemas Tydra does not convert (Cube, Sphere, Cone, Cylinder, Capsule, PointInstancer): the prim loads with no mesh (lightusd src/tydra/scene-access.cc) |
| 1 | warning | USD image '*' could not be decoded | decode the image formats the assets use that the loader rejects (Radiance .hdr above all) |
| 1 | warning | USD image '*' not found | resolve a texture asset path against the layer that authored it, including inside a .usdz package |
| 1 | warning | USD material prim '*' has no converted material - it is not placed in the tree | convert a Material prim Tydra hands over without a converted network (a MaterialX or non-preview surface), so it can be placed where the file puts it |
| 1 | warning | USD prim '*' inherits from '*', which is not a class prim - it becomes no style | diagnose the message and add the support it asks for |
| 1 | warning | USD prim '*' inherits from '*', which names no prim of the file | diagnose the message and add the support it asks for |
| 1 | warning | USD prim '*': its time-sampled transform is not animated - the ops are not in translate, rotate, scale order | diagnose the message and add the support it asks for |
| 1 | warning | USD prim '*': its time-sampled transform is not animated - the stack has a suffixed op (a pivot pair) | diagnose the message and add the support it asks for |
| 1 | warning | USD prim '*': the referencing layer defines prims over the reference (Materials, Materials_1) - a reference protects its structure, so they are dropped | diagnose the message and add the support it asks for |
| 1 | warning | USD prim '*': the referencing layer defines prims over the reference (mtl) - a reference protects its structure, so they are dropped | diagnose the message and add the support it asks for |
| 1 | warning | USD prim '*': the referencing layer defines prims over the reference (neutral_objects) - a reference protects its structure, so they are dropped | diagnose the message and add the support it asks for |
| 1 | warning | USD prim '*': the referencing layer defines prims over the reference (source) - a reference protects its structure, so they are dropped | diagnose the message and add the support it asks for |
| 1 | warning | USD prim '*': visibility and purpose are not readable from a '*' prim | diagnose the message and add the support it asks for |
| 1 | warning | USD stage up axis '*' has no erhe counterpart - imported as Y-up | carry a Z-up stage's up axis into the scene instead of importing it as Y-up |
| 1 | warning | Warning: Used fallback smooth normal | diagnose the message and add the support it asks for |
| 1 | warning | [InternalError] Attribute is invalid.); using default (false). | diagnose the message and add the support it asks for |
| 1 | error | ^ | diagnose the message and add the support it asks for |
| 1 | warning | `*` is authored, but hole face removal is only applied when triangulation is enabled. Hole faces are kept in the polygonal output. | diagnose the message and add the support it asks for |
| 1 | appearance | a UsdUVTexture's colour reaches only one row of the colour-space chart; the other four render white | sample a UsdUVTexture through every colour-space path the file exercises (raw / sRGB / auto / omit / lin_ap1_scene); only one row reaches the surface, the rest render white |
| 1 | appearance | a file whose meshes arrive with no material binding renders in the unbound default grey: the chess set imports 21 meshes and 0 materials | diagnose the message and add the support it asks for |
| 1 | error | breadcrumb t=Ns thread=Nx1731787caa3ee117: raytrace: BVH commit | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: Brdf_slice_rendergraph_node | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: Overlay for Default Viewport | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: Overlay for Viewport_scene_view N | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: Post processing for Default Viewport | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: Post processing for Viewport_scene_view N | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: Viewport window | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: config<path> | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: geometry: compute_smooth_vertex_normals | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: geometry: facets.connect | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: geometry: process | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: geometry: update_connectivity + build_edges | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: primitive: allocate_and_bind_writers | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: primitive: build_centroid_points | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: primitive: build_edge_lines | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: primitive: build_expanded_polygon_fill | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: primitive: build_polygon_fill facets=N verts=N corners=N | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: primitive: optimized variant | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: primitive: take_optimizable_snapshot | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: raytrace: BVH commit | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: shadow_maps | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: asset loads | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: ddgi | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: draw_imgui_windows | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: fixed_step (physics) | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: flush_draw_lists | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: hotbar update | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: imgui process_events + commands | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: lightmap stream | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: rendergraph execute | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: scene_commit_queue flush | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: submit + end_frame | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: texture file loads | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: thumbnails update | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: update_hover_info | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: update_layout_nodes | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: update_material_sets | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: update_transforms | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nx5cbf17a9a104a862: tick: wait_frame | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nxe3a0f56765bf265f: geometry: compute_facet_centroids | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nxe3a0f56765bf265f: primitive: allocate_and_bind_writers | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nxe3a0f56765bf265f: primitive: build_centroid_points | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nxe3a0f56765bf265f: primitive: build_edge_lines | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nxe3a0f56765bf265f: primitive: build_expanded_polygon_fill | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nxe3a0f56765bf265f: primitive: build_polygon_fill facets=N verts=N corners=N | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nxe3a0f56765bf265f: primitive: optimized variant | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nxe3a0f56765bf265f: primitive: take_optimizable_snapshot | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nxe3a0f56765bf265f: raytrace: BVH commit | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | breadcrumb t=Ns thread=Nxe3a0f56765bf265f: raytrace: build_buffer_mesh facets=N verts=N | not a USD gap: editor-internal noise this survey happens to capture |
| 1 | error | color3f inputs:in1 = (N, N, N) ( | diagnose the message and add the support it asks for |
| 1 | warning | file_does_not_exist.usda not found in path: [ <path> ] | diagnose the message and add the support it asks for |
| 1 | warning | glTF export: reference material '*' has no file-scope asset key - exporting full data (an independent definition) | diagnose the message and add the support it asks for |
| 1 | warning | glTF export: texture '*' has no retained source image bytes - texture slot skipped | diagnose the message and add the support it asks for |
| 1 | failure | load failure: no scene appeared within 400 s | open a stage whose root layer is a MaterialX document reference; the load never produces a scene and the open never answers |
| 1 | failure | no mesh loaded: Tydra converts no geometry for the NodeGraph schema | see the cause named in the parentheses; the file authors geometry that produced no mesh |
| 1 | failure | no mesh loaded: cause not in the log | see the cause named in the parentheses; the file authors geometry that produced no mesh |
| 1 | failure | no mesh loaded: the geometry sits behind a variant opinion that is not a material binding | see the cause named in the parentheses; the file authors geometry that produced no mesh |
| 1 | error | open_scene_usd '*' failed: Failed to parse USDA | read the USDA constructs LightUSD's parser rejects; the file then loads as an empty stage |
| 1 | error | skin Skeleton already in scene cameras | diagnose the message and add the support it asks for |
| 1 | error | skin Skeleton not in scene cameras | diagnose the message and add the support it asks for |
| 1 | error | { | diagnose the message and add the support it asks for |

