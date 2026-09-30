# erhe infrastructure audit: platform libraries, build system, tests, docs (2026-09-30)

Read-only audit of the repository at commit c982af6d6 (main). Scope: the small/platform
libraries, the build system, tests and CI, the documentation system and memory
bank, the cross-library dependency graph, repo statistics and follow-through on
the 2026-06-21 audit. All paths are repo-relative; every claim was checked by
reading the cited file.

## 0. Headline numbers

- 44 library directories under src/erhe (src/erhe/CMakeLists.txt:13-63), 45
  CPMAddPackage calls in the root CMakeLists.txt plus one in cmake/jemalloc.cmake.
- Own code under src/erhe: 393,567 lines (62,300 header / 331,267 source),
  of which ~70k are tests and ~92k are vendored single-file libraries kept
  inside erhe libraries (src/erhe/graphics/erhe_graphics/wuffs-v0.4.c alone is
  79,019 lines; imgui_node_editor + crude_json + imgui_canvas in
  src/erhe/imgui/erhe_imgui total 12,559 lines). Net own library code is
  roughly 230k lines.
- 3,842 commits in total, 1,096 in the last 30 days (git rev-list).
- 27 test directories, 3 ctest labels (gpu, editor, unlabeled), 7 CI build
  matrix entries + a Quest APK job + a grep-level task-spawn guard.
- 261 documents under doc/, 81 of them library documents; scripts/check_doc_links.py
  reports 0 problems on the current tree.

## 1. Layering and dependency architecture

Derived from target_link_libraries in every src/erhe/*/CMakeLists.txt (PUBLIC
and PRIVATE together). Third-party links shown in brackets where they matter.

```
L0  leaf      verify[cpptrace]  defer  hash[glm]  utility[SDL3-static!]  pch
L1  base      log[spdlog,fmt,glm,geogram!] -> hash verify
              profile[tracy|nvtx, mimalloc|jemalloc, volk | erhe::gl on GL]
              time -> log profile        message_bus -> profile
              frame_pacing -> profile    file -> defer log verify
              net -> log verify          texgen -> verify      codegen -> file
L2  data      dataformat -> hash log verify        property -> log profile utility verify
              gl -> dataformat log verify           math -> dataformat log profile verify
              circular_ring_buffer -> utility verify buffer -> profile utility verify
L3  model     item -> log message_bus profile property utility verify
              graph -> defer item log verify
              window[SDL3|glfw] -> dataformat defer gl log profile time utility verify
L4  gpu       graphics -> circular_ring_buffer dataformat defer file frame_pacing gl item
                          log math profile time utility verify window   (PUBLIC: window, item, frame_pacing)
L5  gpu-adj   task -> graphics verify        xr -> codegen dataformat gl graphics log profile utility verify window
              geometry -> log math profile verify   voxel -> geometry math
              raytrace -> buffer dataformat file geometry hash log math profile time verify
L6  content   primitive -> buffer dataformat file geometry graphics hash item log math profile raytrace verify
              ui -> file graphics log primitive profile window
              commands -> log profile verify window xr
L7  scene     scene -> item log math primitive profile utility
              rendergraph -> graph graphics log math profile ui verify
              physics -> geometry item log primitive profile renderer(!)
L8  render    renderer -> codegen defer graphics log math primitive profile scene ui utility verify
              gltf -> math scene (PUBLIC) + task file profile geometry graphics log primitive
              usd -> dataformat geometry item log primitive profile scene verify
              geometry_renderer -> geometry math profile renderer verify
L9  app-lib   scene_renderer -> codegen dataformat file graphics log math message_bus primitive profile renderer scene ui
              imgui -> codegen commands defer file gl graphics log math profile renderer rendergraph time window
L10 apps      editor (32 erhe::* libs), example, hello_swap, hextiles
```

No cycles: the graph is a DAG (checked by hand across all 44 targets; the
only suspicious pairs, profile<->gl and window<->time, are one-directional:
src/erhe/profile/CMakeLists.txt:30-32 states explicitly that erhe_gl does not
link erhe_profile).

Layering violations and smells (each verified):

1. erhe_log links geogram PUBLIC (src/erhe/log/CMakeLists.txt:16-22) because of
   src/erhe/log/erhe_log/log_geogram.hpp:5-6 (fmt formatters for GEO::vec*).
   A level-1 logging library therefore drags the heaviest geometry dependency in
   the project into every consumer of erhe::log, i.e. into everything. The
   formatters belong in erhe::geometry (the only geogram user).
2. erhe_utility links SDL3::SDL3-static (src/erhe/utility/CMakeLists.txt:25-30)
   for clipboard.cpp; a level-0 utility library now has a window-backend
   `#ifdef ERHE_WINDOW_LIBRARY_SDL` (src/erhe/utility/erhe_utility/clipboard.cpp:6,29).
   Clipboard is a window concern; it belongs in erhe::window.
3. erhe_profile links erhe::gl on OpenGL (src/erhe/profile/CMakeLists.txt:30-32)
   and volk on Vulkan (:21-23) because profile.hpp aliases gl* symbols for
   Tracy (src/erhe/profile/erhe_profile/profile.hpp:14-49). Profiling sits
   below logging in the layering yet knows both graphics APIs.
4. erhe_task (84 header lines) links erhe::graphics PRIVATE
   (src/erhe/task/CMakeLists.txt:12-18) because task.cpp reads the GL worker
   context state (src/erhe/task/erhe_task/task.cpp:3,10-18). The spawn guard
   is worth keeping, but "task" is now a graphics-dependent library that
   gltf (L8) must link to get taskflow. A guard hook set by graphics at
   startup would invert the dependency.
5. erhe_window's public event header forward-declares erhe::xr types and
   holds raw pointers to them (src/erhe/window/erhe_window/window_event_handler.hpp:7-12,320-338).
   window (L3) knows xr (L5) by name; the XR input events should live in xr
   or in a neutral input library.
6. erhe_commands links erhe::xr PUBLIC unconditionally
   (src/erhe/commands/CMakeLists.txt:48), so imgui -> commands -> xr ->
   graphics: every ImGui consumer transitively links the OpenXR-facing library
   even in ERHE_XR_LIBRARY=none builds (xr then compiles only xr_action.*,
   src/erhe/xr/CMakeLists.txt:4-8).
7. erhe_physics links erhe::renderer (src/erhe/physics/CMakeLists.txt:126;
   used by erhe_physics/jolt/jolt_world.cpp for debug drawing). A simulation
   library depending on the line renderer is the one true "low depends on
   high" edge in the graph; a debug-draw interface owned by physics and
   implemented by the editor would remove it.
8. erhe_rendergraph links erhe::ui (src/erhe/rendergraph/CMakeLists.txt) but
   no file under src/erhe/rendergraph includes erhe_ui/ (grep). Dead edge.
9. erhe_gltf makes erhe::scene PUBLIC (src/erhe/gltf/CMakeLists.txt:36-41)
   so the importer is a scene-level library; fine, but its doc position in
   src/erhe/CMakeLists.txt:35 (between gl and graph) hides that.
10. Vendored imgui (src/imgui/imgui/imconfig.h:184-196) declares
    erhe::graphics types and an Erhe_ImTextureID; the third-party copy is
    coupled to erhe's graphics library by configuration header. Documented as
    intentional in doc/erhe/imgui.md:52-56 (file-identical to the tksuoran/imgui
    fork's erhe branch).

Libraries too small to justify a separate target (lines are header+source
incl. tests): defer 47, message_bus 136, pch 17, hash 231 (xxhash.hpp
vendored), task 108, geometry_renderer 135, texgen header 648 is fine. Each
costs a CMakeLists, a doc, a CHANGELOG namespace and a link line in 20+
consumers. Candidates to fold: defer + hash + message_bus + utility -> one
erhe::core; geometry_renderer -> renderer.

Libraries that should be split: erhe_graphics (405 files, ~63k own lines,
three backends under erhe_graphics/{vulkan,gl,metal,null}) is the only one
where a split (backend targets behind a stable interface target) would cut
rebuild scope; today one header change in device.hpp rebuilds all backends
and all 15 consumers.

Duplicate source listing: src/erhe/xr/CMakeLists.txt lists xr_action.cpp/.hpp
twice (lines 6-7 and 14-15) when openxr is on; harmless (CMake dedups) but
untidy.

## 2. Build system quality

Options. 22 configure options are declared through set_option() with a
closed STRINGS list (CMakeLists.txt:44-72); backend selection is
one-of (ERHE_GRAPHICS_API opengl|vulkan|metal|none, ERHE_WINDOW_LIBRARY
sdl|glfw|none, ...). Android forces a known-good subset
(CMakeLists.txt:133-148). This is clean and discoverable.

Compiler flags. Per-target warnings via erhe_target_settings_toolchain:
MSVC /W4 /WX /external:W3 (cmake/msvc.cmake:19-31), Clang -Wall -Wextra ...
without -Werror (cmake/Clang.cmake:18-31), GCC only -Woverloaded-virtual and
-Wno-empty-body with the full warning set commented out
(cmake/GNU.cmake:1-6), AppleClang the warning set globally with no -Werror
(cmake/AppleClang.cmake:3-8). Sanitizer flags remain commented out in
Clang.cmake:42-55 and AppleClang.cmake:12-25; only MSVC honours
ERHE_USE_ASAN (cmake/msvc.cmake:60-63). Consequence: warnings are errors on
one of four compilers, so a GCC/Clang-only warning never fails CI.

Code generation. Two generators run at configure time and again as build
custom commands: src/erhe/gl/generate_sources.py (gl.xml -> gl wrapper,
src/erhe/gl/CMakeLists.txt:60-81) and erhe_codegen (Python struct/JSON
codegen, src/erhe/codegen/erhe_codegen_generate.cmake:1-45, with OUTPUTS
declared so ninja recompiles dependents in the same build). Generated
serialization TUs are compiled unoptimized and without PCH on MSVC to avoid
4-minute /O2 compiles (erhe_codegen_generate.cmake, erhe_codegen_source_settings).
Good engineering; the doc/cmake_conventions.md:73-83 note "run the build
twice" is stale for ninja after the OUTPUTS mechanism, still true for VS.

Per-platform trees. 32 configure wrappers (scripts/configure_*.bat|.sh); each
forwards extra arguments before its own -D options so the wrapper's own
values win (scripts/configure_ninja_win_vulkan.bat:13-17). CI relies on this
to pass -DERHE_BUILD_TESTS=ON (.github/workflows/build.yml:133-137). The
convention is documented in doc/testing.md:73-79 and matches the scripts.
scripts/configure_ninja.bat:4-5 hardcodes C:\Program Files\LLVM paths; not
a user path but still per-machine install state in a committed file.

Build-time concerns.
- Unity builds: no UNITY_BUILD anywhere (grep of root, cmake/, src CMake).
- PCH: ERHE_USE_PRECOMPILED_HEADERS defaults OFF (CMakeLists.txt:68) with a
  "TODO fix" at :90, yet the Windows wrappers turn it ON
  (scripts/configure_vs2026_vulkan.bat, configure_tests.bat). The shared PCH
  forces AVX2 flags globally on clang-cl (cmake/Clang.cmake:71-84) and
  -Wno-clang-cl-pch (:23-30): the single-PCH design is fighting per-target
  PUBLIC defines. doc/erhe/pch.md:22 says "all erhe libraries ... get the
  PCH automatically", which is only true when the option is ON.
- Warning-level hygiene for dependencies is handled once
  (src/CMakeLists.txt:7-19 clears the -external:W0 injection).
- ccache/sccache: not configured anywhere.
- googletest is fetched by 25 separate CPMAddPackage blocks, one per test
  CMakeLists (grep -l "NAME googletest" -> 25 files, e.g.
  src/erhe/graphics/test/CMakeLists.txt:1-9), each pinning 1.16.0 by hand.

Dependency pinning. 45 CPM packages; most pin VERSION or a commit. Unpinned
or floating: concurrentqueue GIT_TAG master (CMakeLists.txt:427-432, the
commit is still commented out at :430), cpp-terminal with no tag
(CMakeLists.txt:940). Content-hash pinning is used where a repo has no tags
(flip at :718-725 via URL_HASH; erhe_download_pinned_file in
cmake/functions.cmake:85-125). Geogram is pinned to a fork tag and its
commit is embedded as a define (CMakeLists.txt:391-401); SDL has a
sync-pin guard that fails configure when the Android copy drifts
(CMakeLists.txt:641-652).

Forks. Eight tksuoran/* forks are consumed: geogram (:351), glslang (:488),
SDL (:627), LightUSD (:835), fastgltf (:988), mimalloc (:1055,1071),
OpenXR-SDK-Source (:1282), plus the in-tree imgui copy mirroring
tksuoran/imgui (doc/erhe/imgui.md:52-56). The policy "forks, never patch
files" is stated with its rationale in doc/cmake_conventions.md:38-52. The
maintenance burden is real: every upstream bump is a fork rebase, and the
SDL fork has its own sync script and pin guard. Nothing in the repo tracks
which fork commits have been upstreamed; the 2026-06 audit recommendation 9
("upstream the fork patches") has no visible follow-through.

Toolchain files. cmake/toolchain/ holds 11 files (linux-clang-10..14,
linux-gnu-gcc-8..11, x86_64-windows-msvc-17) that nothing references
(grep of scripts/, CMakeLists.txt, doc/). Dead weight.

## 3. Platform abstraction quality (window / xr / imgui)

window. The abstraction is a compile-time selection of one Context_window
class (src/erhe/window/erhe_window/window.hpp:3-11 includes glfw_window.hpp,
sdl_window.hpp or null_window.hpp), not a runtime interface. The three
classes duplicate the same ~50-method surface (sdl_window.hpp:57-213,
glfw_window.hpp, null_window.hpp) and each is riddled with graphics-API
switches: 71 ERHE_ conditionals in window sources (statistics table), e.g.
sdl_window.hpp:62-64,78-80,125-129,160-162,205-210 expose OpenGL-only
constructors and Vulkan-only surface methods, and Window_configuration
carries OpenGL fields under #if (window_configuration.hpp:36-41). Public
headers pull <windows.h> (sdl_window.hpp:14-29) and forward-declare
wl_display. Backend-neutral parts are good: Input_event and the keycode
table (window_event_handler.hpp), mouse-cursor enum mirrored to ImGui with a
comment on why (sdl_window.hpp:37-53), the SDL type kept out of the public
header via void* (sdl_window.hpp:164-166). The glfw and null backends were
last touched 2026-07-30 while sdl_window.cpp is ~1,700 lines and active;
glfw is not built in any CI matrix entry (.github/workflows/build.yml:53-113),
so it is unverified code. RenderDoc capture lives in window
(renderdoc_capture.cpp) with a per-OS ladder (:12-298), reasonable.

xr. Well-contained OpenXR wrapper (16 files, 6,956 lines). Leaks: public
headers include openxr_platform.h and, on Vulkan, erhe_graphics/vulkan_external_creators.hpp
(xr_instance.hpp:10-12,20-21), and expose PFN_xr* members per API
(xr_instance.hpp:163-171); CMake has three TODOs about making volk/graphics
PRIVATE via pimpl (src/erhe/xr/CMakeLists.txt:68,94,102). Error handling is
consistent: ERHE_XR_CHECK returns false on failure (xr.hpp:95) and 93
log_xr->error/warn calls vs 13 ERHE_VERIFY. Two XR_SESSION_LOSS_PENDING
paths are logged "TODO" (xr_session.cpp:1626,1680) and depth layer near/far
are hardcoded with TODO (xr_session.cpp:1905-1906,2118-2119).

imgui. Imgui_host is a proper abstract base (imgui_host.hpp:45-62: pure
virtuals for begin/end frame, events, text input) with Window_imgui_host and
the editor's Rendertarget_imgui_host as implementations. Imgui_renderer is
API-neutral through erhe::graphics; the only backend switches are texture-heap
strategy defines (imgui_renderer.cpp:60-138) and two OpenGL blocks
(:1223,:1524). The one leaking bool constructor argument is `bool imgui_ini`
(imgui_host.hpp:52). Third-party code (imgui_node_editor, crude_json,
imgui_canvas, ~12.5k lines) is compiled inside erhe_imgui rather than as a
vendored target next to src/imgui; it inflates the library's struct count
(81) and TODO count and receives erhe's /W4 /WX.

ui. Font rasterization/layout switches on FREETYPE/HARFBUZZ defines
(font.cpp:16-58) and the library links erhe::primitive PUBLIC for one include
in color_picker.cpp:5. A font library depending on the mesh library is
accidental coupling.

## 4. Error handling, logging and assertions

- ERHE_VERIFY / ERHE_FATAL (src/erhe/verify/erhe_verify/verify.hpp:27-50) are
  active in every configuration (no NDEBUG gate; confirmed by
  task.hpp:30-31 "Active in Release"). They print with printf/__android_log_print,
  dump a cpptrace callstack to stderr (verify.cpp:24-50) and abort. The fatal
  path therefore bypasses erhe logging: a crash reason never reaches
  logs/log.txt, which is the file AGENTS.md tells agents to read. MSVC's
  variant also includes <windows.h> from a header used by ~1,400 call sites
  (verify.hpp:3-20).
- 1,438 ERHE_VERIFY and 226 ERHE_FATAL across src/erhe vs 148 raw assert()
  (90 of those in codegen/test code). Consistency is good: assert is
  effectively unused in library code.
- Exceptions: 59 throw sites, 22 try blocks. Almost all throws are fmt
  formatter boilerplate (log_glm.hpp / log_geogram.hpp: 21 identical
  `throw format_error("invalid format")`), allocator new/delete
  (profile.hpp:173,181; jemalloc_new_delete.hpp:36,52) and one
  std::runtime_error in vulkan_bind_group_layout.cpp:132 that no caller
  catches. The project has no typed recoverable-error type
  (std::expected appears once, in a review note src/erhe/primitive/claude_review.md).
  Recoverable failures are reported by `-> bool` (window: 53 such returns,
  xr: 54, commands: 54) plus a log line; the audit found no library that
  mixes the two styles within one call path.
- Logging: erhe::log wraps spdlog with per-library make_logger categories,
  a Store_log_sink and thread breadcrumbs for watchdogs (log.hpp:37-105).
  Violations of the "never printf" rule in library code are few and
  deliberate-looking: file.cpp:498-528 (cwd search before logging exists),
  geometry.cpp:760-806 and remesh.cpp:83-262 (repro dumps written with %a,
  matching the hexfloat rule), verify.cpp. erhe_smoke's usage text uses
  printf (smoke/main.cpp:22-27), acceptable for a CLI.
- make_logger(const std::string&, bool tail = true) (log.hpp:37) is a
  boolean parameter in the most-used API of the base layer; it predates the
  enum rule.
- Profiling coverage is uneven: ERHE_PROFILE_FUNCTION/SCOPE counts per
  library are graphics 87, scene_renderer 51, xr 37, gltf 30, window 27,
  ... but item 1, property 2, rendergraph 2, commands 1 (grep).

## 5. Test strategy and coverage map

CI (build.yml:136-167) configures every matrix entry with tests ON, builds
`editor` and `erhe_tests`, then runs `ctest --label-exclude "gpu|editor"
--timeout 120` through scripts/ci_run_tests.py:29-37 with a 30-minute step
bound (build.yml:163-167, commit c982af6d6). The step never fails the build
job; tests.yml turns the uploaded JUnit into the tests badge. GPU and
editor-driving tests never run in CI (no GPU runner).

| Library / area | Tests | Kind | ctest label | Runs in CI |
|---|---|---|---|---|
| circular_ring_buffer | yes | gtest | - | yes |
| codegen | yes | self-checking exe (add_test, codegen/test/CMakeLists.txt:44) | - | yes |
| commands | yes | gtest | - | yes |
| dataformat | yes | gtest | - | yes |
| frame_pacing | yes | gtest (virtual clock, 1,735 lines) | - | yes |
| geometry | yes | gtest (5,128 lines) | - | yes |
| gltf | data only (src/erhe/gltf/test/data, no CMake) | - | - | no |
| graph | yes | gtest | - | yes |
| graphics | yes | erhe_graphics_tests (deviceless) + erhe_graphics_gpu_tests (176 cases, FLIP goldens) | -, gpu | deviceless only |
| item | yes | gtest + erhe_smoke stress exe (not registered with ctest) | - | gtest yes |
| math | yes | gtest | - | yes |
| physics | yes | gtest (needs jolt/box3d) | - | yes |
| primitive | yes | gtest | - | yes |
| property | yes | gtest (4,233 lines) | - | yes |
| raytrace | yes | gtest | - | yes |
| renderer | yes | erhe_renderer_gpu_tests | gpu | no |
| scene | yes | gtest | - | yes |
| scene_renderer | yes | gtest + gpu tests | -, gpu | deviceless only |
| texgen | yes | gtest | - | yes |
| usd | yes | gtest (13,761 lines; lightusd only) | - | only where lightusd is configured |
| voxel | yes | gtest (openvdb only) | - | only where openvdb is configured |
| editor assets/brushes/renderers/rig/transform | yes | gtest | - | yes |
| editor mcp (mcp_server_tests) | yes | gtest + ctest fixtures that launch the editor (mcp/test/CMakeLists.txt:91-144) | editor | no |
| window, xr, imgui, ui, net, log, file, time, utility, task, profile, hash, defer, message_bus, buffer, gl, rendergraph, geometry_renderer, pch | no | - | - | - |

Observations.
- Nine platform/infra libraries in this slice have no tests; window/xr are
  hard to test without hardware, but net (a socket layer with framing) and
  file, time, log, rendergraph are pure logic and testable.
- The gpu label is the right cut; a self-hosted GPU runner or a software
  Vulkan (lavapipe/SwiftShader) job is the missing piece. graphics_test_coverage.md:5-11
  records 176/175/165/170 passes per backend from manual runs only.
- doc/testing.md:10-14 lists 12 library suites; the tree has 21 library
  test directories plus 6 editor ones. The list is stale.
- erhe_smoke (item stress) is built only with ERHE_BUILD_TESTS
  (src/erhe/CMakeLists.txt:61-63) but has no add_test, so it is never run.
- No ASan/UBSan CI job; ERHE_USE_ASAN is MSVC-only (section 2).
- No fuzzing, no benchmark gating; configure_tests.bat exists for the
  geometry timing harness (scripts/configure_tests.bat:2-11).
- The task-spawn grep guard (build.yml:184-192, scripts/check_task_spawns.py)
  is a good example of a cheap structural test.

## 6. Documentation and memory-bank health

Documentation system. doc/README.md states layout, header-line and writing
rules (doc/README.md:11-80); scripts/check_doc_links.py enforces path
resolution, relative links, the header line and the no-notes.md rule
(check_doc_links.py:2-21) and passes cleanly today. Every src/erhe library
except task has doc/erhe/<name>.md (doccheck). Sampled docs vs headers:
log.md, verify.md, time.md, xr.md, utility.md name only identifiers that
exist in the sources (0 misses each); frame_pacing.md misses one target
name, window.md 5 (SDL hint names and an editor tool), imgui.md 27 (they are
fork-internal ImGui symbols in src/imgui, not erhe_imgui), net.md 3
(editor window names). Stability lines are present in all sampled docs.
Weak spots: doc/erhe/net.md is titled "erhe::net Review" (:1) and reads as a
review rather than a present-tense description; doc/erhe/pch.md:22 overstates
PCH use; doc/testing.md suite list is stale; the erhe::task library has no document under doc/erhe/
although erhe_task is a library with a CI guard around it.

CHANGELOG.md follows Keep a Changelog with an [Unreleased] section
(CHANGELOG.md:1-25) and is fed by the AGENTS.md rule; it has never had a
release heading, so "version" is meaningless to consumers (see section 9).

Plans: doc/plans holds 62 documents, 18 "Status: in progress" and 44
"proposed". Eighteen concurrent in-progress plans is a lot for a one-owner
project and worth a triage pass.

Memory bank. 1,394 lines total, core files small (activeContext 31,
progress 89, systemPatterns 31, techContext 13, productContext 25), 14
topic files, memory-bank/local gitignored (.gitignore:72-73). Health issues:
- progress.md violates its own rule "active-tasks-only; DONE-task-sections
  move to topics" (progress.md:3): it carries four completed tasks
  (radiance-cascades, shadow-robustness, debug-line-anti-aliasing all marked
  DONE) as commit-hash timelines, i.e. exactly the history the doc rules ban.
- history.md (238 lines) says entries archive daily to archive/YYYY-MM-DD.md
  (history.md:2) but memory-bank/archive does not exist.
- techContext.md says Deps#27; the root CMakeLists has 45 CPM packages.
- productContext.md last changed 2026-06-29; everything else is current
  (2026-09-24..30). The bank is actively maintained but is drifting toward a
  second changelog.

## 7. Statistics tables

Per-library (src/erhe/<lib>, all files incl. tests and vendored; header and
source lines; struct vs class declarations; auto occurrences; TODO/FIXME/HACK/XXX;
printf-family calls; bool parameters in headers (regex approximation);
#if...ERHE_ conditionals; throw; std::shared_ptr / std::unique_ptr mentions;
ERHE_VERIFY / ERHE_FATAL; test lines).

| lib | hdr | src | struct | class | auto | TODO | printf | bool-param | ifdef ERHE_ | throw | shared | unique | VERIFY | FATAL | test lines |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| graphics | 14934 | 148850 (79019 wuffs) | 56 | 757 | 3027 | 163 | 5 | 47 | 180 | 28 | 256 | 183 | 670 | 99 | 20469 |
| usd | 1728 | 32573 | 1 | 178 | 694 | 1 | 0 | 0 | 0 | 1 | 709 | 14 | 0 | 0 | 13761 |
| geometry | 3557 | 20181 | 20 | 99 | 420 | 13 | 28 | 9 | 1 | 2 | 35 | 230 | 73 | 1 | 5128 |
| imgui | 5434 | 14542 | 81 | 92 | 1050 | 27 | 0 | 41 | 11 | 0 | 32 | 11 | 23 | 1 | 0 |
| scene | 3619 | 13423 | 2 | 153 | 728 | 16 | 0 | 8 | 0 | 0 | 641 | 6 | 33 | 17 | 4993 |
| scene_renderer | 5662 | 13262 | 2 | 288 | 483 | 13 | 0 | 20 | 1 | 0 | 244 | 60 | 137 | 8 | 2744 |
| physics | 3988 | 12214 | 1 | 141 | 1085 | 6 | 0 | 11 | 0 | 0 | 296 | 35 | 25 | 0 | 2779 |
| primitive | 2432 | 9408 | 1 | 82 | 415 | 23 | 0 | 6 | 0 | 0 | 134 | 11 | 122 | 43 | 1699 |
| gltf | 1114 | 8185 | 3 | 92 | 297 | 13 | 0 | 5 | 2 | 0 | 111 | 1 | 32 | 3 | 0 |
| property | 2221 | 7632 | 17 | 55 | 390 | 0 | 0 | 2 | 0 | 0 | 115 | 16 | 18 | 2 | 4233 |
| xr | 1250 | 5706 | 2 | 35 | 306 | 13 | 0 | 12 | 30 | 0 | 5 | 6 | 13 | 5 | 0 |
| item | 1764 | 5406 | 3 | 44 | 522 | 2 | 0 | 7 | 0 | 1 | 206 | 3 | 6 | 0 | 3670 |
| window | 1892 | 3991 | 8 | 30 | 296 | 24 | 0 | 17 | 71 | 0 | 8 | 0 | 11 | 2 | 0 |
| renderer | 1157 | 4199 | 1 | 62 | 98 | 5 | 0 | 6 | 1 | 0 | 15 | 17 | 27 | 1 | 846 |
| raytrace | 1328 | 4572 | 1 | 75 | 363 | 7 | 0 | 0 | 1 | 1 | 31 | 26 | 10 | 0 | 1609 |
| commands | 1160 | 3594 | 14 | 66 | 323 | 13 | 0 | 5 | 5 | 1 | 14 | 10 | 30 | 3 | 513 |
| math | 1835 | 3481 | 0 | 18 | 339 | 13 | 0 | 13 | 0 | 0 | 4 | 0 | 0 | 1 | 947 |
| texgen | 648 | 3319 | 0 | 22 | 107 | 0 | 0 | 1 | 0 | 0 | 0 | 0 | 17 | 4 | 1941 |
| frame_pacing | 992 | 2082 | 0 | 17 | 88 | 0 | 5 | 2 | 0 | 0 | 0 | 1 | 0 | 0 | 1735 |
| dataformat | 460 | 2898 | 1 | 4 | 133 | 1 | 0 | 0 | 0 | 0 | 2 | 0 | 144 | 13 | 382 |
| ui | 1017 | 1557 | 7 | 10 | 114 | 6 | 0 | 2 | 13 | 0 | 5 | 3 | 2 | 7 | 0 |
| net | 312 | 1762 | 0 | 8 | 141 | 5 | 0 | 0 | 2 | 0 | 8 | 2 | 12 | 0 | 0 |
| gl | 304 | 1528 | 1 | 0 | 78 | 0 | 0 | 1 | 4 | 0 | 4 | 0 | 0 | 6 | 0 |
| graph | 346 | 1191 | 0 | 18 | 78 | 0 | 0 | 2 | 0 | 0 | 8 | 5 | 5 | 0 | 763 |
| circular_ring_buffer | 84 | 1216 | 0 | 4 | 34 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 8 | 3 | 938 |
| codegen | 615 | 669 | 6 | 0 | 35 | 0 | 25 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 508 |
| rendergraph | 312 | 813 | 0 | 26 | 63 | 0 | 0 | 1 | 0 | 0 | 20 | 4 | 6 | 0 | 0 |
| log | 536 | 542 | 4 | 5 | 111 | 0 | 0 | 1 | 3 | 21 | 17 | 0 | 3 | 0 | 0 |
| file | 68 | 712 | 0 | 0 | 26 | 4 | 4 | 1 | 9 | 0 | 8 | 2 | 0 | 0 | 0 |
| voxel | 94 | 677 | 0 | 4 | 31 | 0 | 0 | 0 | 0 | 0 | 0 | 3 | 4 | 0 | 328 |
| buffer | 160 | 320 | 0 | 6 | 37 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 1 | 0 | 0 |
| time | 110 | 379 | 5 | 3 | 26 | 0 | 0 | 0 | 0 | 0 | 2 | 0 | 0 | 0 | 0 |
| profile | 326 | 26 | 0 | 1 | 7 | 0 | 0 | 0 | 12 | 4 | 0 | 0 | 0 | 0 | 0 |
| utility | 240 | 65 | 0 | 2 | 11 | 0 | 0 | 0 | 2 | 0 | 0 | 0 | 2 | 0 | 0 |
| hash | 230 | 1 | 0 | 0 | 9 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| message_bus | 135 | 1 | 1 | 3 | 7 | 1 | 0 | 0 | 0 | 0 | 2 | 0 | 0 | 0 | 0 |
| geometry_renderer | 35 | 100 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| smoke | 0 | 108 | 0 | 0 | 5 | 0 | 23 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| task | 84 | 24 | 0 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 1 | 0 |
| verify | 55 | 56 | 0 | 0 | 3 | 0 | 4 | 0 | 0 | 0 | 0 | 0 | 3 | 6 | 0 |
| defer | 46 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| pch | 16 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| TOTAL | 62300 | 331267 | 238 | 2403 | 11983 | 369 | 97 | 222 | 349 | 59 | 2926 | 647 | 1438 | 226 | 69986 |

Reading the table:
- struct vs class: 238 struct vs 2,403 class declarations overall; 81 of the
  imgui structs and most of the codegen/commands ones are in vendored or
  generated code. Own code follows the class-only rule closely.
- auto density: 11,983 occurrences over ~394k lines (3.0 per 100 lines);
  the highest own-code densities are physics (6.7/100) and imgui (5.3/100).
  Much of it is `auto f() -> T` trailing-return style, which the rule allows.
- shared_ptr outnumbers unique_ptr 4.5:1 (2,926 vs 647); scene (641), usd
  (709) and physics (296) are shared_ptr-heavy graphs.
- Backend switch density: graphics 180, window 71, xr 30, ui 13, profile 12.
  Everything else is under 12.
- Global/static mutable state: 94 non-const static/thread_local definitions
  in own non-test code (grep, section 1 evidence list): notable ones are
  graphics state presets (color_blend_state.hpp:39-43, depth_stencil_state.hpp:43-56,
  rasterization_state.hpp:58-64, input_assembly_state.hpp:13-16 -- mutable
  static instances of state objects), pipeline registries
  (render_pipeline_state.hpp:55, compute_pipeline_state.hpp:63), per-backend
  s_active_render_pass (gl_device.hpp:102, metal_device.hpp:105,
  null_device.hpp:53), gl error checking toggles (gl.cpp:22,
  gl_helpers.cpp:24-25), geometry repro counters under mutexes
  (geometry.cpp:744-745, remesh.cpp:64-65), and Context_window::s_window_count
  (sdl_window.hpp:212). The thread_local guards (scoped_worker_context.cpp:18-22,
  gl_context_index.cpp:7) are by design.

Repo-wide: 57 Python, 28 .bat and 18 .sh files under scripts/ (the
Python-not-PowerShell rule holds: zero .ps1). AGENTS.md is 173 lines; 17
agent documents under doc/agents; 9 skills under .claude/skills.

## 8. Prior audit (doc/reference/audit_erhe_2026_06_21.md section 8) follow-through

| # | Recommendation | Status | Evidence |
|---|---|---|---|
| 1 | Run tests in CI | Done, exceeded | build.yml:136-167: all 7 matrix entries, JUnit upload, tests badge; bounded 2026-09-30 (c982af6d6) |
| 2 | Pin concurrentqueue | Not done | CMakeLists.txt:429 `GIT_TAG master`, commit still commented at :430 |
| 3 | Rotate CLAUDE_CHAT_API_KEY, keep .mcp.json untracked | Done (as far as the repo shows) | .mcp.json untracked and in .gitignore:13; the key string appears only in the audit itself |
| 4 | Centralize the GoogleTest pin | Not done, worse | 25 test CMakeLists each CPMAddPackage googletest 1.16.0 (was 8) |
| 5 | Embed erhe version + git hash | Not done | rev-parse is run only for dependencies (CMakeLists.txt:393,657,1171,1182); no erhe version header; project(VERSION 1.0) at :20 |
| 6 | -Werror on Clang/GCC, .clang-format/.clang-tidy | Not done | cmake/Clang.cmake:18 no -Werror; GNU.cmake:2 warnings commented; no .clang-format/.clang-tidy/.editorconfig in root |
| 7 | fast/slow label split + ASan/UBSan CI job | Partly | labels gpu/editor exist (doc/testing.md:96-104); no sanitizer CI job; sanitizers MSVC-only |
| 8 | Refactor Mcp_server | Done (split), still large | src/editor/mcp: 21 files, 23,515 lines; mcp_server.cpp 2,116, mcp_server_scene_action.cpp 4,075 |
| 9 | Upstream fork patches | No evidence | 8 forks consumed (section 2); no tracking of upstreamed commits |
| 10 | Typed error type | Not done | std::expected appears only in src/erhe/primitive/claude_review.md |

Four of ten done or partly done; the two one-line fixes (2 and 4) are still
open.

## 9. Strengths (specific)

- Option design: closed-list set_option() (CMakeLists.txt:44-72) with a
  forced Android subset (:133-148) and a fatal on unknown OS (:127-129).
- Dependency hygiene beyond the norm: SHA256-pinned URL downloads
  (functions.cmake:85-125), the SDL sync-pin guard (CMakeLists.txt:641-652),
  dependency commits embedded as defines, a written fork policy with
  rationale (doc/cmake_conventions.md:38-52), and pins that name the
  upstream version which would retire each fork (e.g. CMakeLists.txt:352-355).
- Codegen that understands ninja's restat semantics and MSVC's optimizer
  cost cliff (erhe_codegen_generate.cmake:17-30 and the source-settings block).
- CI that separates the build verdict from the test verdict (build.yml:9-15,
  tests.yml:3-9), caches CPM by hash of CMake files, bounds hangs per test
  and per step, and keeps a line-flushed log so a killed step still names the
  running test (ci_run_tests.py:2-8). The Quest APK build in CI is rare for
  a project of this size.
- A structural CI guard (check_task_spawns.py) that enforces an invariant
  the type system cannot (build.yml:179-192).
- A GPU test suite with FLIP golden images and a written per-backend
  coverage matrix (graphics_test_coverage.md), plus an editor-level MCP
  suite with ctest fixtures that launch the editor (mcp/test/CMakeLists.txt:91-144).
- Unit-testable frame pacer with a "never calls the OS or a clock" contract
  (frame_pacer.hpp:11-16) mirrored by a Python reference model.
- Documentation rules that are mechanically checked (check_doc_links.py) and
  actually clean; every library has a doc (one exception).
- Consistent assertion vocabulary (ERHE_VERIFY/ERHE_FATAL, live in Release)
  and consistent logging categories; assert() is effectively absent from
  library code.
- Windows-specific compiler pain (clang-cl PCH, STL vector algorithm bug,
  /external:W0 override) is documented at the point of the workaround
  (cmake/Clang.cmake:87-109, src/CMakeLists.txt:7-19).

## 10. Prioritized infrastructure proposals

Ordered by (benefit / cost). Costs are rough engineer-days.

1. Two one-line pins (0.1 d): concurrentqueue GIT_TAG -> the commented
   commit (CMakeLists.txt:429-430); cpp-terminal a tag (:940). Removes the
   last floating dependencies.
2. Centralize googletest (0.5 d): one CPMAddPackage under ERHE_BUILD_TESTS in
   the root (next to flip, CMakeLists.txt:718-725), delete 25 copies. Also
   register erhe_smoke with add_test (short duration) so it runs at all.
3. Fix the four layering leaks in the base layer (1-2 d, mechanical):
   move log_geogram.hpp into erhe::geometry and drop geogram from erhe_log;
   move clipboard.cpp into erhe::window; drop the dead erhe::ui link from
   rendergraph and erhe::primitive from erhe::ui (one include in
   color_picker.cpp); make erhe_commands link erhe::xr only when
   ERHE_XR_LIBRARY is openxr by moving the Xr_*_binding sources under that
   condition. Each is a link-graph change with no behavior change and
   measurable rebuild-scope wins (everything links erhe::log).
4. Route ERHE_FATAL through erhe logging before abort (0.5 d): call a
   registered sink (spdlog flush) from erhe_dump_callstack so crash reasons
   reach logs/log.txt, which is where AGENTS.md sends agents. Keep printf as
   the fallback before log init.
5. Sanitizer CI job (1 d): one Linux Clang matrix entry with
   -fsanitize=address,undefined on the deviceless test set; the option
   plumbing exists (ERHE_USE_ASAN) but only for MSVC (cmake/msvc.cmake:60-63);
   wire it into Clang.cmake/GNU.cmake/AppleClang.cmake where the flags are
   currently commented out. Highest defect-finding value per CI minute for a
   codebase with 2,926 shared_ptr sites and hand-rolled ring buffers.
6. -Werror on Clang/GCC (2-3 d, incremental): the warning set already exists
   in cmake/Clang.cmake:18; add -Werror per target via
   erhe_target_settings_toolchain for leaf libraries first. GNU.cmake:2
   shows the set was disabled for Jolt; Jolt now builds with its own flags,
   so re-test. Also add .clang-format so the style rules in AGENTS.md are
   enforceable, not just readable.
7. Software-Vulkan GPU tests in CI (2-4 d): run erhe_graphics_gpu_tests on
   Linux with lavapipe (mesa-vulkan-drivers) under the headless Vulkan
   build; goldens may need a tolerance profile per driver (FLIP already
   supports thresholds). This is the only way the 176-case suite becomes a
   gate rather than a manual report.
8. Version embedding + first CHANGELOG release (0.5 d): generate
   erhe_version.hpp from project(VERSION) plus `git describe`, log it at
   startup next to the dependency commits already logged; cut a
   0.x heading in CHANGELOG.md so [Unreleased] has meaning. Prerequisite
   for any install/package story.
9. Reduce library count (2 d): fold defer, hash, message_bus and utility
   into one erhe::core (they have no dependencies except glm/SDL and total
   ~450 lines), fold geometry_renderer into renderer, and make task a
   header-only part of the same core once the graphics hook is inverted.
   Cuts ~6 CMakeLists, 6 docs, 6 CHANGELOG namespaces and dozens of link
   lines, with no runtime effect.
10. Split erhe_graphics into interface + backend targets (5-8 d): a
    graphics_api target (public headers, enums, state, shader_resource) and
    erhe_graphics_vulkan|gl|metal|null implementation targets selected by
    ERHE_GRAPHICS_API. Benefit: consumers no longer rebuild when a backend
    .cpp changes; the pimpl split already present (Device_impl,
    Render_pass_impl) makes this mostly CMake work. Also move wuffs-v0.4.c
    and the imgui_node_editor sources into vendored targets next to src/imgui
    so /W4 /WX and the statistics stop counting them as erhe code.
11. Unity builds and PCH decision (1 d to measure, then act): measure a
    clean build with CMAKE_UNITY_BUILD on the large libraries (graphics,
    scene_renderer, usd) and with ERHE_USE_PRECOMPILED_HEADERS ON vs OFF on
    ninja/clang-cl. Either retire the shared PCH (it forces global AVX2 on
    clang-cl and needs -Wno-clang-cl-pch, Clang.cmake:23-30,71-84) or make
    it default ON everywhere; today the default and the wrappers disagree.
12. Test the untested infrastructure (2-3 d): erhe::net framing and ring
    buffer, erhe::file path search, erhe::log Store_log_sink/breadcrumbs,
    rendergraph ordering, Input_event keycode round-trips via to_erhe_keycode
    (imgui_host.hpp:34-35). All are pure logic and fit the existing gtest
    pattern. Add a fuzz target for net's packet framing later (libFuzzer on
    the sanitizer job), since it parses untrusted bytes.
13. Public API boundary / install target (3-5 d, optional): there is no
    install() anywhere and headers are exposed as whole source directories
    (target_include_directories PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} in every
    library). An erhe::erhe umbrella with install(EXPORT) is feasible only
    after item 3 and 9 remove the accidental transitive dependencies; until
    then a consumer of erhe::log would receive geogram and SDL.
14. C++20 modules: not recommended now. The build is CMake 3.16.3 minimum
    (CMakeLists.txt:15), targets four generators including Xcode and the
    Android NDK's CMake 3.22.1 (build.yml:225-230); modules need CMake 3.28+
    and Ninja/VS only. Revisit after the minimum rises.
15. WebGPU / wasm feasibility from the build side: low today. Blockers are
    structural, not per-file: SDL3 and glm port, but geogram (linked from
    erhe::log), Jolt with AVX2 forced globally (Clang.cmake:71-84), cpptrace
    in erhe::verify, tracy, spdlog threads, and the OpenGL 4.6 / Vulkan /
    Metal-only graphics backends have no browser path. The ERHE_GRAPHICS_API
    "none" + ERHE_WINDOW_LIBRARY "none" build already exists (CI headless
    entry), so a webgpu backend would slot into the option system, but it
    is a 4th backend (~10k lines by analogy with metal at 8,653) plus a
    dependency diet. Treat as a 3-6 month project gated on items 3, 9 and 10.

## 11. Smaller findings worth a ticket

- doc/testing.md:10-14 suite list stale (12 listed, 21 present); add
  a document for erhe::task under doc/erhe/; retitle doc/erhe/net.md.
- doc/cmake_conventions.md:80-83 "run the build twice" no longer applies to
  ninja after the OUTPUTS mechanism (erhe_codegen_generate.cmake:17-30).
- cmake/toolchain/*.cmake (11 files) referenced by nothing.
- src/erhe/xr/CMakeLists.txt:6-7,14-15 duplicate source entries.
- scripts/configure_ninja.bat:4-5 hardcodes an LLVM install path.
- memory-bank/progress.md carries DONE sections against its own rule; no
  memory-bank/archive despite history.md:2.
- glfw window backend (glfw_window.cpp, last touched 2026-07-30) is built
  by no CI entry; either add a matrix entry or mark it experimental in
  doc/erhe/window.md.
- make_logger(name, bool tail) (log.hpp:37) and Imgui_host(..., bool imgui_ini, ...)
  (imgui_host.hpp:52) are boolean parameters in base-layer APIs.
