# Vegetation: L-systems, plant sway physics, wind

Recipes for plants - L-system generation, the one-spine physics LOD,
rest-pose motor sway and scene wind. Reference code: creations 9-10
(L-systems), 13 (windswept glade), 15 (tree garden / lsystem_trees.py).

## L-system vegetation (creations 9-10 carry reference code)

- String rewrite with stochastic rules; interpret with a 3D turtle
  (heading/left vectors + Rodrigues rotation). Symbols: `F` segment,
  `[`/`]` push/pop, `&`/`^` pitch, `+`/`-` yaw or pinna side, `/` roll,
  terminal `A -> L`/`K` leaf/bloom.
- Trees: per-species parameter dicts (trunk, seg falloffs, pitch range,
  branch-count weights); leaf tips greedily clustered into canopy
  spheres (radius ~ sqrt(count)); one wobble pitch per `F` for gnarl.
- Ferns: rachis of ~6 short segments with progressive gravity droop and
  a pinna pair at nearly every node (~12 pinnae/frond reads realistic);
  basal pinnae expand a second L-system level (midrib + pinnule pair +
  tip). Flowers: stem segments + bracketed leaf blades + side-bloom
  stalks; blooms = center sphere + 5 aligned petal blades.
- 3-slice cones make triangles/prisms (TR-3B slab, portal ring); stand
  upright with `qx(90)`, then roll `qz(180)` for vertex-up.
- Tree age variety (creation 13): pass an age/scale parameter driving
  size (~ age^0.7-0.8), L-system depth (+1 iteration when age >= 1.3,
  -1 for saplings), branch density, gnarl and canopy clump size; sway
  mass ~ age^2, receptivity ~ age (old trees lumber, saplings whip).

## Physics LOD for whole plants (creation 13 carries reference code)

For a scene full of swaying vegetation, do NOT chain bodies per
segment - give each plant ONE spine body and let the visual subtree
ride it:

- Visual parts (branches, canopies, pinnae, petals) are created with
  `motion_mode="none"` - NO rigid body at all. A static child body
  would grind against the dynamic spine and block the sway; "none"
  costs nothing and needs no strip pass.
- The spine node (tree trunk / frond rachis 0 / stem segment 0) gets
  `common.body(node_id, shape="auto", motion_mode="dynamic",
  mass=<explicit>, gravity_factor=0, wind_receptivity=...)` - "auto"
  hulls the node's own mesh, so the collision follows the visual.
- Coincident anchor child at the spine base, jointed to a CARRIER on
  the same plant (root group / trunk / branch, given a tiny static
  sensor body: shape="sphere" radius=0.05 is_trigger=true) - NEVER to
  the world (2026-08-08: world anchors pin foliage in world space, so
  moving the plant leaves it floating behind; carrier joints are
  body-relative and the whole plant moves as one object). Collect
  (node, base, settings, mass, receptivity, carrier) jobs during the
  build and rig them AFTER settle() with the simulation still disabled.
- Lift plant bases a few cm (trees 0.05) so spine hulls clear the
  floor instead of grinding on it.
- `common.wind(...)` enables the scene wind (it carries the required
  `"_version": 2`). set_scene_settings supports `merge: true`
  (server-side deep merge, 2026-08-08) - common uses it, so wind and
  ambience never clobber each other and no client accumulator exists.
  Raw set_scene_settings WITHOUT merge still REPLACES the whole object,
  and versionless sub-objects are now rejected loudly instead of
  silently dropping newer fields.
- Verified per-scale tuning (mass / stiffness / damping / max_force /
  range / receptivity): tree 25 / 300 / 30 / 600 / 0.12 / 6.0,
  fern frond 0.08 / 6 / 0.5 / 8 / 0.35 / 0.3,
  flower stem 0.03 / 0.8 / 0.08 / 3 / 0.50 / 0.35, with wind speed 3,
  gusts 2.2 @ 0.4 Hz, turbulence 0.45, wavelength 9.
- Print a sway probe (sample tip tilt every 0.5 s): healthy plants
  OSCILLATE and recover; a monotonic tilt ramp that parks at the
  angular limit means receptivity is too high for the drive stiffness
  (iteration-1 ferns blew flat exactly this way).

## Bendy plants & wind (rest-pose motor joints; verified 2026-08-08)

Foliage that bends under impact / wind and springs back to its authored
pose needs no new machinery - six-dof drives ARE the rest-pose motor:

- **L-system trees are a shared module**: `scripts/creations/`
  `lsystem_trees.py` - expand_lsystem + grow_tree (moved from the glade,
  rng-identical) plus species-level generators (grow_conifer whorls with
  tip_rise, grow_columnar, grow_shrub, broadleaf_species_params from a
  REAL height in meters). Realism knobs (2026-08-09 research: ABOP
  tropism, Weber-Penn curve/crown shape, pipe-model radii, golden-angle
  phyllotaxis) are opt-in species keys: tropism / tip_tropism /
  phyllotaxis / pipe_exponent / curve_res. Reference use: creation 15
  tree garden (28 Finnish species, one PartBatch flush per tree, 281
  MCP calls total). Branches want SUBDIVISION + SUBTREES or they read
  as lollipop sticks (2026-08-09): broadleaf curve_res default is 4,
  lower trunk branches arch as sub-cone chains (per-sub downward blend
  + lateral jitter) carrying none / a few 2-cone FORKS with their own
  smaller canopies, conifer boughs are curved 3-segment chains with
  0-2 side twigs + foliage tufts (twig pitch inherits species droop -
  spruce hangs its branchlets), shrub stems carry a 3-cone
  outward-arching continuation above the single spine hull with side
  shoots + small crown tufts. Extra parts cost frame time (~19 ->
  ~27 ms on the 28-tree garden) - keep fork/twig counts in the 0-3
  range. `sway_setting_for_height(c, h, stiffness_scale, range_scale)`
  tunes habits off the tapered-beam rule: columnar juniper x4
  stiffness / half range (its sway body is only the short inner trunk,
  so joint angle is amplified over the full visual column), shrub
  stems x3 / 0.6 + receptivity trimmed. Small DENSE evergreens hit the
  same trap through the plain conifer path (the 8.4 m yew parked at
  its angular limit like the juniper): creation 15 exposes
  sway_stiffness_scale / sway_range_scale / sway_receptivity_frac as
  species keys for that.
- **Rig**: chain of Y-axis capsules (`create_shape capsule` is
  center-origin), coincident anchor child pairs at each pivot as above;
  the root anchor joints to a sensor-body carrier on the plant root
  (world joints pin the plant in place - see the sway bullet above).
  The anchor pair is BOTH-SIDED: jointing the spine anchor straight to
  the carrier NODE puts the carrier-side constraint frame at the
  carrier's origin, and locked linear axes drag the spine body there -
  creation 13's willow curtains (tip anchor, carrier at the branch
  ring base) all snapped to the trunk top this way for two days before
  anyone noticed (fixed 2026-08-10; rig_tree_sway had the same fix
  earlier as "crowns slumped to half height"). Plants whose anchor and
  carrier positions coincide mask the bug.
- **Sibling spines need a collision filter** (2026-08-09): a joint
  disables collision only for ITS pair, so spines jointed to the same
  carrier still collide with each other - shrub stems fanning out of one
  base point (and limbs crowding a crown) sit permanently
  interpenetrated and the solver push-out reads as wobble/jitter. Fix:
  one `create_collision_filter` whose `collision_systems` and
  `not_collide_with_systems` are both `["sway_spines"]` (self-denylist),
  passed as `filter_name` on every spine body -
  `lsystem_trees.rig_tree_sway` does this. The filter insert is queued;
  settle() before the first body references it. Unfiltered bodies
  (lawn, props) still collide with spines.
  Joint settings:
  lock linear XYZ (0..0) + angular Y (0..0), limit angular X/Z (about
  +/-0.9 rad), and add angular drives on axes 0 and 2 with
  `position_target 0`, `stiffness ~30`, `damping ~2`, finite
  `max_force ~60`. Target 0 = the pose at joint creation; the motor
  springs back to it, `max_force` is the yield threshold, the limits cap
  the bend.
- **Build with physics DISABLED, enable after the joints exist.**
  Segments free-fall for the frames between create_shape and the
  gravity_factor edit, and joints capture that fallen pose as the rest
  pose (a few degrees of permanent lean). Verified: disabled-build gives
  rest tilt exactly 0.
- `gravity_factor 0` on segments so the motor spring does not fight
  gravity (no droop below the authored pose); modest
  `angular_damping ~0.1`.
- Stiffness/max_force scale with segment thickness: stiff base, floppy
  tip reads plant-like.
- **Wind**: set `wind_receptivity` (kg/s) on segment bodies via
  create/edit_physics_body - increasing toward the tip (e.g. 0.7 base ->
  1.5 tip). Enable scene wind through `set_scene_settings` physics
  override; the object MUST carry `"_version": 2` or the wind fields are
  silently dropped by version migration (same trap as sky `_version`).
  Verified values: `wind_speed 6, wind_gust_amplitude 4,
  wind_gust_frequency 0.5, wind_turbulence 0.4, wind_wavelength 8` =
  dramatic 7-78 deg sway; quarter those receptivities for subtle
  ambient foliage. Wind force is
  `receptivity * (wind_velocity - body_velocity)` at the COM each fixed
  step; zero receptivity bodies are never touched (they sleep).
  `wind_receptivity` persists in the ERHE_physics extension on save.
