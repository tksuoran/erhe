# Architecture, Foundations & Security Audit -- erhe

- **Date**: 2026-06-21
- **Auditor**: Claude Code (Architecture Audit mindset, C++)
- **Scope**: Full 9-section audit. Focus areas requested: Foundations & quality gates, Architecture (KISS/DRY/YAGNI), Security, Maintainability & cost.
- **Evidence tooling**: C++ symbol/usage introspection via LSAI (`mcp__lsai__*`, workspace `erhe-c-1`, clangd Tier3, Ready). CMake / JSON / Markdown / YAML read directly. Counts scoped to **own code** under `src/erhe/` and `src/editor/`; vendored trees (`.cpm_cache/`, `build_*/`, Windows SDK, `src/imgui`, `src/mango`, etc.) excluded.

---

## Executive Summary

erhe is a mature, single-author (tksuoran, with a vulcan/Ladislav fork) C++20 graphics
library + ImGui editor with genuinely strong engineering discipline in the areas the
author has invested in: **logging is exemplary**, **build determinism and cross-platform
CI are excellent**, **dependency pinning is centralized and documented**, and the newest
subsystem (the local **MCP control server**) shows careful security thinking
(loopback-only bind, timing-safe token compare, payload caps, file-mode/uid checks).

The weaknesses are concentrated and consistent with a fast-moving solo/research codebase:

1. **Warnings-as-errors is enforced on only one of three compilers.** MSVC gets
   `/W4 /WX`; Clang and GCC have **no `-Werror`** and actively suppress whole warning
   classes (`-Wno-narrowing`, `-Wno-sign-compare`, GNU warnings commented out entirely).
   Two-thirds of the compiler matrix can accumulate warnings silently.
2. **Tests exist but are not gated.** ~8 of 34 erhe libraries have unit tests
   (GoogleTest), `ERHE_BUILD_TESTS` defaults `OFF`, and **CI never runs `ctest`** -- it
   builds the `editor` target only. The safety net is present but disconnected.
3. **No self-versioning.** The binary embeds the git commit of vendored deps
   (geogram/bvh/sdl) but **not erhe's own** -- a running editor cannot report which erhe
   commit produced it.
4. **No typed value-error handling.** No `std::expected`/`tl::expected` anywhere in own
   code; internal failures are `VERIFY`/`FATAL` (abort) or exceptions. Acceptable for an
   editor, but it is the foundation the project is furthest from.

None of these are architectural emergencies. They are the classic "Day-1 foundations
that are cheap now, expensive later" gaps. The single highest-ROI fix is wiring the
existing test targets into CI with `ERHE_BUILD_TESTS=ON` + a `ctest` step.

**Overall score: 3.6 / 5** -- a solid, well-reasoned codebase with a few specific,
fixable foundation gaps.

---

## 1. KISS Analysis (Keep It Simple, Stupid)

| Component | Complexity (1-5) | Justification | Simpler Alternative | Notes / Savings |
|-----------|------------------|---------------|---------------------|-----------------|
| Many swappable backends (graphics vulkan/opengl/metal/none, raytrace bvh/tinybvh/embree/none, window sdl/glfw, physics jolt/none, xr openxr/none) via `#ifdef ERHE_<SUBSYS>_LIBRARY_<VALUE>` | 3 | **Justified.** Metal is Apple-only, Vulkan is default, OpenGL is legacy/desktop, BVH is the headless fallback. These are real platform requirements, not speculation. | None -- this is essential complexity. | Keep. The `#ifdef` selection at configure time is the right tool. |
| `editor::Mcp_server` -- single class, ~90 methods, `mcp_server.cpp` ~4,400 lines | 4 | A 90-method god-class is the one clear KISS smell. It mixes transport (HTTP routes, auth), JSON marshalling, and ~70 domain actions (scene/physics/material/mesh ops). | Split by concern: transport+auth, a tool-dispatch table, and per-domain action handlers in separate TUs. The free-function helpers already in the anonymous namespace show the seam. | Medium-term refactor; high cognitive cost for a new contributor reading one 4.4k-line file. |
| 6 maintained forks (geogram, glslang, fastgltf, OpenXR-SDK, SDL, mimalloc) | 4 | Each is documented with *why* and *when to drop*. The discipline is excellent, but 6 forks is real, ongoing rebase/sync surface (esp. SDL via `update_sdl.py` + drift guards). | Upstream the fixes (some are already filed: glslang #4186). | Keep but actively pursue upstreaming -- see Cost section. |
| `App_context` "all parts" pointer struct + post-construction wiring rule | 2 | Pragmatic composition root for a large editor; the constructor-injection rule is documented and enforced by convention. | None worth the churn. | Keep. |
| `erhe_codegen` (Python -> C++ struct + JSON serialization generator) | 3 | Reduces hand-written serialization boilerplate; the implementation is described as complete. | Could be reflection-cpp (commented out in root CMake), but homegrown codegen avoids a heavy dep. | Keep; ensure it stays documented. |

**KISS verdict**: Mostly disciplined. The single concrete simplification target is the
`Mcp_server` god-class. Backend `#ifdef` proliferation looks complex but is essential.

---

## 2. DRY Analysis (Don't Repeat Yourself)

| Duplication Found | Where | Impact | Recommended Action | Priority |
|-------------------|-------|--------|--------------------|----------|
| Per-library logging boilerplate (`<lib>_log.cpp` + `initialize_logging()` + category fields) replicated in ~20 libraries | `src/erhe/*/erhe_*/*_log.cpp` | Low -- this is *intentional, beneficial* duplication (each library owns its log categories; no shared coupling). | **Keep.** This is DRY-vs-coupling done right. | -- |
| Duplicated shaders + `cell_*.cpp` between editor and `rendering_test` | `res/rendering_test/shaders/`, `src/rendering_test/` | Known/accepted. CLAUDE.md declares `rendering_test` "rotten" and slated for rewrite. | **Do not fix** -- coherent partial fixes there are wasted work (per project policy). | -- |
| `_SILENCE_CXX20_*` / `NOMINMAX` / `_CRT_SECURE_NO_WARNINGS` definitions repeated per-compiler-file | `cmake/msvc.cmake`, `cmake/Clang.cmake`, `cmake/GNU.cmake` | Trivial. | Optional: hoist common defs to `common_options.cmake`. | Low |
| x86 SIMD baseline (`-msse4.1 -mpclmul`) block duplicated verbatim in `Clang.cmake` and `GNU.cmake` (identical comment + guard) | `cmake/Clang.cmake:38`, `cmake/GNU.cmake:26` | Trivial maintenance drift risk. | Extract to a shared cmake include. | Low |
| `googletest` `CPMAddPackage` repeated in every `*/test/CMakeLists.txt` | `src/erhe/*/test/CMakeLists.txt` (8 files) | Low -- but version pin (1.16.0) is now in 8 places; a bump touches 8 files, violating the project's own "one place pins versions" principle. | Move the GoogleTest `CPMAddPackage` to the top-level dependency block, guarded by `ERHE_BUILD_TESTS`. | Medium |

**DRY verdict**: Good. The only real finding is the GoogleTest pin scattered across 8
test CMakeLists -- it contradicts Foundation #2 (central dependency management).

---

## 3. YAGNI Analysis (You Ain't Gonna Need It)

| Item | Classification | Rationale |
|------|----------------|-----------|
| Swappable raytrace backends (4: bvh/tinybvh/embree/none) | **Keep** | All four are real, selectable, and serve different platform/perf trade-offs (embree x86-only, bvh headless fallback). |
| `ERHE_NAVIGATION_LIBRARY` (recastnavigation/none, default **none**) | **Keep (watch)** | Defaulted off; only cost is the CMake branch. Not wired into a shipping feature today -- confirm it is still on the roadmap. |
| `ERHE_AUDIO_LIBRARY` miniaudio (default none), `ERHE_TERMINAL_LIBRARY` cpp-terminal (default none) | **Simplify/watch** | Optional integrations defaulted off. Cheap to keep, but if unused for a long time they are dead configure branches. |
| `reflect-cpp` dependency | **Already removed** | Correctly commented out in root `CMakeLists.txt` -- good YAGNI hygiene (homegrown codegen used instead). |
| `ERHE_USE_MIMALLOC` (default OFF) with Tracy-tracking fork variant | **Keep** | Genuine perf/diagnostics tool; off by default, no runtime cost when disabled. |
| MCP server's ~70 domain actions (physics joints, collision filters, mesh component selection, ...) | **Keep** | This is the product surface (AI-driven scene authoring), not speculative generality. |

**YAGNI verdict**: Strong. No premature microservices, no speculative abstractions with
zero implementations, dead deps actively pruned. The only soft flags are the
defaulted-`none` optional integrations (navigation/audio/terminal) -- cheap, but verify
they are still intended.

---

## 4. Overengineering Analysis

- **Infrastructure vs requirements**: Not applicable in the cloud sense -- this is a
  desktop/XR application, no servers to right-size. The one network surface (MCP server)
  is appropriately minimal: a single `httplib::Server` on loopback.
- **Architecture vs team size**: This is the genuine risk. The codebase is large
  (34 erhe libraries + a large editor) and effectively single-author. The backend
  `#ifdef` matrix, 6 forks, and Quest/XR specifics demand specialized, hard-to-replace
  knowledge. The extensive CLAUDE.md + Memory Bank + per-library `notes.md` are a
  deliberate, effective mitigation -- onboarding documentation is well above average.
- **Process vs speed**: CI is well-tuned (9-config matrix, CPM caching, concurrency
  cancellation, doc-only skip). Build-only CI keeps it fast; the trade-off is no test
  gate (see Foundation #5/#13).
- **Stack audit**: ~27 third-party dependencies for a graphics engine is reasonable for
  the feature set (geometry, physics, glTF, XR, fonts, SVG, profiling). No technology is
  obviously used at 10% of capacity. The fork count (6) is the maintenance hotspot.

**Overengineering verdict**: Not overengineered. Complexity tracks real cross-platform
graphics/XR requirements. The dominant risk is **bus factor**, mitigated by unusually
good internal documentation.

---

## 5. Project Foundations Analysis

> Evidence discipline applied: each verdict below is backed by a specific config file,
> CMake module, or LSAI query result. Test/coverage verdicts are reported as ratios
> (have/total), not collapsed to binary.

| # | Principle | Status | Evidence | Severity |
|---|-----------|--------|----------|----------|
| 1 | Zero-tolerance warnings | **Partial** | MSVC: `/W4 /WX` per target (`cmake/msvc.cmake:19-20`), enforced in CI Windows jobs. Clang: `-Wall -Wextra` **but no `-Werror`**, plus `-Wno-unused -Wno-sign-compare -Wno-narrowing -Wno-deprecated-copy` (`cmake/Clang.cmake:3-7`). GNU: warning block **commented out entirely** ("TODO restore warnings", `cmake/GNU.cmake:1-2`). No project-level `.clang-tidy`/`.clang-format` in own code (only vendored copies under `.cpm_cache/` and third-party `src/imgui_gradient/`). | **High** |
| 2 | Central dependency management | **Present** (one exception) | All deps pinned once in root `CMakeLists.txt` via CPM, by tag or exact commit, with documented fork rationale. Exception: `concurrentqueue` uses `GIT_TAG master` (`CMakeLists.txt:254`) -- a moving branch, contradicting the project's own pin discipline. Secondary: GoogleTest 1.16.0 pinned in 8 separate test CMakeLists instead of centrally. | **High** |
| 3 | Versioning strategy | **Partial** | `project(erhe VERSION 1.0)` is static. Build embeds vendored commit hashes (`ERHE_GEOGRAM_GIT_COMMIT`, `ERHE_BVH_GIT_COMMIT`, `ERHE_SDL_*`) as defines, but **no erhe self git-hash**: no `version.hpp.in`, no `git rev-parse` for erhe in `src/**/CMakeLists.txt` (LSAI/grep: 0 matches). The running editor cannot report which erhe commit it is. | **Medium** |
| 4 | Structured error handling | **Partial** | LSAI search for `expected`: **zero** `std::expected`/`tl::expected` in own code (only `fastgltf::Expected`). Internal failures use `VERIFY`/`FATAL` (abort, `erhe::verify`) or exceptions. The fail-fast path is well-built: `ERHE_FATAL` (`erhe_verify/verify.hpp`) emits `std::source_location` + callstack dump + `DebugBreak`/`__builtin_trap` + `abort` -- a deliberate engine choice, not a defect. Positive: the MCP boundary returns typed JSON-RPC error codes (-32700/-32601/-32602/-32001). No typed value-error type for library APIs. | **High** |
| 5 | Test configuration (fast/slow) | **Missing** | Tests use `gtest_discover_tests` with **no `LABELS`/`set_tests_properties`** (grep over all `src/**/CMakeLists.txt`: 0 matches). No fast/slow split. Compounding: `ERHE_BUILD_TESTS` defaults `OFF` and **CI runs no `ctest` step** (`.github/workflows/build.yml` builds `--target editor` only). | **Medium** |
| 6 | Immutable DTOs | **Partial** | Domain is a mutable real-time scene graph (`erhe::scene::Node`/`Mesh` mutate by design), so the immutable-DTO pattern applies weakly. `erhe_codegen` produces serializable structs with accessors. No systematic `const`-member/value-equality DTO discipline, but largely N/A for this domain. | **Low** |
| 7 | DI with proper registration | **Present** | `editor::App_context` (`app_context.hpp:101`) is the shared-resource holder; constructor injection via explicit `Part&`/resource refs is documented and enforced (CLAUDE.md "Part construction" rule: ctor must not read `App_context`). `main()` is the composition root. No DI framework. | -- |
| 8 | Centralized config with validation | **Present** | All tunables in `config/<app>/*.json` (editor has 14 files: graphics, windows, renderer, logging, ...), loaded by `erhe_codegen`-generated loaders. Per-app isolation (`config/editor`, `config/hextiles`, ...). (Validation/fail-fast on missing keys assumed via codegen loaders -- not exhaustively verified.) | **High** (mostly satisfied) |
| 9 | Structured file logging | **Present (exemplary)** | `erhe::log::make_logger` / `Log_sinks` (`log.cpp:271,431`) wrap spdlog to a file sink (`logs/log.txt`). Each library owns categories via `<lib>_log.cpp`/`initialize_logging()`. `config/editor/logging.json` exposes ~140 per-category levels, runtime-configurable. No `std::cout`/`printf` logging. | -- |
| 10 | Zero-dependency core module | **Partial** | No single "core contracts" target. Foundational low-dep libs (`log`, `utility`, `item`) are built first (`src/erhe/CMakeLists.txt:13-15`), and contracts are distributed (per-subsystem `I*` interfaces + `erhe::item` base). Works, but there is no one dependency-free module holding all interfaces/DTOs. | **Medium** |
| 11 | Interface-first design | **Present** (for swappable subsystems) | Abstract interfaces at backend boundaries: `erhe::physics::ICollision_shape` (rich abstract base, virtual dtor, factory API -- `icollision_shape.hpp:59`), plus raytrace/window/xr backends behind `#ifdef`. Not every component is interface-fronted, but every *replaceable* one is. | **Medium** |
| 12 | Internal/private by default | **Present** | `class` everywhere (per style rule) with `m_`-prefixed private members; anonymous-namespace helpers (e.g. `mcp_server.cpp` has a large anon namespace of free helpers); PRIVATE link deps hide impl libs (`erhe::scene` links `erhe::log`/`fmt` PRIVATE). | **Low** |
| 13 | Test module per production module | **Partial** | ~**8 of 34** erhe libraries have a `test/` target (circular_ring_buffer, codegen, dataformat, geometry, graphics, item, math, raytrace) -- **~24%**. Plus `src/editor/mcp/test`. Not Missing (real tests exist), but well short of one-per-module. | **Medium** |
| 14 | Convention over configuration | **Present** | `erhe_target_sources_grouped()` + `erhe_target_settings()` helpers (`cmake/functions.cmake`, `common_options.cmake`); uniform `erhe_<name>/` dir layout; `erhe::<name>` alias targets; per-lib `<name>_log`/`initialize_logging` convention. Sources listed explicitly (no `file(GLOB)`) -- correct. | **Low** |
| 15 | Deterministic build output | **Present** | In-source builds hard-blocked (`CMakeLists.txt:4-7` FATAL_ERROR). Per-configuration build dirs (`build_vs2026_vulkan/`, `build_ninja_linux_vulkan/`, ...). Persistent CPM source cache (`.cpm_cache`). CI keys cache on `hashFiles(cmake/**, **/CMakeLists.txt)`. | **Low** |
| 16 | Black box composition | **Present** | Targets expose `erhe::<name>` aliases; `target_link_libraries` uses correct PUBLIC/PRIVATE scopes (impl deps hidden). Nuance: each lib's PUBLIC include dir is the whole library root (`erhe_<name>/` namespacing provides logical, not enforced, internal/public split -- no separate `include/` vs `src/`). | **Medium** |

**Foundations scorecard: ~7 Present, ~8 Partial, 1 Missing (of 16).**
The Missing one (#5 test gating) and the High-severity Partials (#1 warnings, #2 central
deps, #4 errors) are the actionable set.

---

## 5b. Code-Level Diagnostics (LSAI / clangd)

Beyond the build-config foundations, LSAI `diagnostics` was run over the workspace and
filtered to own code (`src/erhe/*`, `src/editor/*`; vendored/SDK/`rendering_test`
excluded). Result: **0 errors**, 2829 warnings -- but ~2790 are `-Wc++98-compat`-family
pedantry from the analyzer's near-`-Weverything` profile. The **~40 substantive warnings**
cluster in `erhe::net` and the MCP server. The actionable ones:

| File:line | Diagnostic | Why it matters |
|-----------|------------|----------------|
| `src/erhe/net/erhe_net/socket.cpp:39` | `-Wshadow-field-in-constructor`: ctor param `length` shadows field `length` of `Packet_header` | Silent-bug risk: assignments may target the param, not the field. |
| `socket.cpp:276,294,302,366` | `-Wsign-conversion` x4: `const int` -> `std::size_t` | The classic networking bug: a `recv()`/`-1` return implicitly becomes a huge `size_t`. |
| `socket.cpp:402` | `-Wunsafe-buffer-usage`: unsafe pointer arithmetic | Memory-safety smell in packet handling. |
| `socket.cpp:207` | `-Wcast-qual`: cast drops `const` (`const int*` -> `char*`) | Const-correctness violation on a socket buffer. |
| `socket.cpp:421` | `-Wswitch-enum`: `CONNECTED` not explicitly handled | Missing state handling. |
| `src/editor/mcp/mcp_server.cpp:2426,2440` | `-Wfloat-equal`: float compared with `==`/`!=` (material edit) | Correctness; also contradicts CLAUDE.md's own float-handling guidance. |
| `mcp_server.cpp:2120` | `-Wunsafe-buffer-usage`: unsafe buffer access | Memory-safety smell. |
| `mcp_server.cpp:267` | `-Wswitch-enum`: `e_invalid` not handled | Missing case. |
| `mcp_server.cpp:1663` | `-Wshadow-uncaptured-local`: declaration shadows a local | Silent-bug risk. |
| `mcp_server.cpp:5,51,70`; `server.cpp:5` | clangd unused-includes (`app_settings.hpp`, `gltf.hpp`, `cerrno`, `verify.hpp`) | Dead includes; trivial cleanup. |
| (many) | `-Wextra-semi`, `-Wnrvo`, `-Wexit-time-destructors`, `-Wpadded`, `-Wcovered-switch-default` | Stylistic / low-value. |

**Key cross-finding (concrete proof of Foundation #1):** these real defects -- signedness
conversions in `erhe::net`, constructor field-shadowing, float-equality, unsafe buffer
arithmetic -- are **invisible in the actual build**, because Clang/GCC suppress
`-Wsign-conversion`/narrowing and carry no `-Werror` (only MSVC has `/WX`, and MSVC `/W4`
does not flag several of these). The LSAI diagnostics thus *demonstrate* the cost of the
MSVC-only warnings gate: a whole class of correctness bugs in the network code passes
through CI green. Fixing the warnings gate (Recommendation #6) would surface exactly these.

**Recommended priority:** the `erhe::net::Socket` sign-conversion + shadow-field +
unsafe-buffer cluster first (it is the code path behind the loopback MCP server and any
other networking), then the MCP `-Wfloat-equal` sites.

---

## 6. Security Analysis

This is a desktop/XR application, not a web service. The only network attack surface is
the **local MCP control server**; the other inputs are file parsers (glTF, JSON config,
images, fonts).

### 6.1 OWASP Top 10

| Risk | Status | Evidence |
|------|--------|----------|
| A01 Broken Access Control | **Risk (low)** | MCP server (`editor::Mcp_server`) supports bearer-token auth but it is **optional**: when no `~/.claude/erhe_mcp_token` is present, "any request is accepted" (`setup_routes`, `mcp_server.cpp:670`). Mitigated by **loopback-only bind** (see A05) and a loud startup warning. |
| A02 Cryptographic Failures | **OK** | Token comparison uses `constant_time_equal` (timing-safe). No TLS, but traffic never leaves `127.0.0.1`. No secrets logged. |
| A03 Injection | **OK / Risk (low)** | JSON via nlohmann/simdjson; MCP request body parsed inside `try/catch(json::parse_error)` -> -32700, no crash. No SQL. Path inputs exist (MCP `import/export gltf`, `save_scene`) -- potential path traversal **iff** the server were exposed; loopback + optional auth bound the risk. |
| A04 Insecure Design | **Risk (low)** | Auth-optional-by-default is a deliberate, documented usability trade-off for a local dev tool. Reasonable, but defaulting to "deny without token" would be safer. |
| A05 Security Misconfiguration | **OK** | Server binds `127.0.0.1` explicitly (`server_thread_main`, `mcp_server.cpp:656`) -- **not** `0.0.0.0`. `set_payload_max_length(k_max_payload_bytes)` caps request size (DoS guard). `/health` endpoint leaks nothing. |
| A06 Vulnerable Components | **Risk** | ~27 deps + 6 forks, no automated vulnerability scanning (no Dependabot/CodeQL/`pip-audit` equivalent in `.github/`). `concurrentqueue` tracks moving `master`. Most pins are recent (fmt 12.1, spdlog 1.17, glm 1.0.3, Vulkan SDK 1.4.341), which is good. |
| A07 Auth Failures | **OK** | Token file requires POSIX mode `0600` and matching `uid` (rejects other-user/symlink-swapped tokens, `load_auth_token`, `mcp_server.cpp:478`). Trailing-whitespace trimmed. Returns 401 + `WWW-Authenticate` correctly. |
| A08 Data Integrity Failures | **Risk (low)** | glTF/asset import from untrusted files flows through fastgltf (a fork) + Geogram geometry ops; malformed input is a parser-hardening concern, not a verified vuln. No deserialization of executable content. |
| A09 Logging & Monitoring | **OK** | Excellent structured file logging (Foundation #9). MCP lifecycle, auth state, and errors are logged. |
| A10 SSRF | **OK** | `httplib` used as a server; no outbound request driven by user-controlled URLs. `erhe::net` client exists but is not fed untrusted URLs from the MCP surface. |

### 6.2 Secrets Management

- **`.mcp.json` contains a live `CLAUDE_CHAT_API_KEY`** (value redacted in this report).
  **Good**: it is `.gitignore`d and confirmed **not** tracked (`git ls-files`: no match).
  **Caution**: it sits in plaintext at the repo root, so it is one `git add -f` or one
  tool that ignores `.gitignore` away from leaking. The value was surfaced into this audit
  session -- consider **rotating it** and keeping such secrets outside the repo tree
  (e.g. `~/.claude/`).
- No `.env` files committed. No passwords/connection strings in tracked config. The
  `.vscode/settings.json` `"stop_token": "cpp"` hit is a false positive (clangd setting).
- MCP bearer token correctly stored outside the repo (`~/.claude/erhe_mcp_token`, 0600).

### 6.3 Input Validation
- MCP: JSON parse guarded; missing `params.name` -> -32602; unknown method -> -32601;
  payload size capped. Per-action argument parsing has dedicated helpers
  (`try_read_vec3/vec2/float/bool`, `parse_axis`, `parse_motion_mode`, ...) -- structured,
  not ad-hoc string slicing.
- File parsers (glTF/PNG/SVG/font) are the largest untrusted-input surface; hardening
  relies on the upstream libraries. Acceptable for the threat model (local files).

### 6.4 Authentication & Authorization
- Single mechanism (bearer token) on the only network endpoint. Per-endpoint check is
  applied before dispatch. Adequate for a loopback dev tool.

### 6.5 Dependency Vulnerabilities
- No automated scan in CI. **Recommendation**: add Dependabot (it understands neither CPM
  nor the forks directly, but can watch the Action versions) and/or a periodic manual
  review of the pinned tags. At minimum, replace `concurrentqueue` `master` with a pinned
  commit (the commented-out `00dd7ba0...` is right there).

### 6.6 Transport Security
- No HTTPS, by design (loopback only). No CORS exposure (not a browser-facing API).
  Acceptable.

### 6.7 Memory Safety (sanitizer coverage -- source-verified)
- **AddressSanitizer**: real and supported. `ERHE_USE_ASAN` option (`CMakeLists.txt:65`,
  default **OFF**), wired in `cmake/msvc.cmake:58` (`-fsanitize=address`), plus **6
  dedicated configure scripts**: `configure_tests_asan.bat`,
  `configure_vs2026_{opengl,vulkan}_asan.bat`, `configure_xcode_{metal,opengl,vulkan}_asan.sh`.
- **UBSan**: present only as a Jolt-specific build config type
  (`JoltPhysicsCompatibility.cmake:71`, `RELEASEUBSAN = -fsanitize=undefined,implicit-conversion`)
  and as **commented-out** lines in `cmake/Clang.cmake`/`AppleClang.cmake`. Not a
  first-class project-wide option.
- **TSan / MSan / leak / fuzzer**: **not present.** Only a commented-out `-fsanitize=thread`
  block exists in `Clang.cmake`/`AppleClang.cmake`; there is no MSan, no leak-sanitizer
  wiring, and **no libFuzzer target**.
- **Verdict**: ASan coverage (opt-in) is a genuine defensive strength for the untested
  libraries (Foundation #13). **Gap / top recommendation**: add a libFuzzer target for the
  asset loaders (glTF/PNG/font) under ASan/UBSan -- the highest-value untrusted-input
  surface, and currently unfuzzed.

> Correction note: an earlier draft of this section carried over two claims from a prior
> audit without verification -- a `new`/`delete` ratio and a broad
> `fsanitize=...,thread,memory,leak,fuzzer,...` sanitizer list. On source verification the
> sanitizer list was **inflated** (only ASan is a real option; TSan/MSan/fuzzer do not
> exist), and the `new`/`delete` ratio could not be verified within this audit's tooling
> rules. Both have been removed/corrected. Recorded per the audit's "report faithfully" rule.

**Security verdict**: For its threat model, **good**. The MCP server is the most
security-aware part of the codebase. Action items: (a) rotate the surfaced API key,
(b) pin `concurrentqueue`, (c) consider auth-required-by-default, (d) add lightweight
dependency monitoring.

---

## 7. Cost Analysis (Maintenance TCO -- adapted for a desktop/OSS project)

There is no cloud spend. The meaningful cost is **developer maintenance time and risk**.

| Component | Maintenance cost driver | Risk if neglected | Lower-cost alternative |
|-----------|------------------------|-------------------|------------------------|
| 6 maintained forks (geogram, glslang, fastgltf, OpenXR-SDK, SDL, mimalloc) | Periodic rebase onto upstream; SDL has a drift-guard + `update_sdl.py` ceremony | Forks drift, security fixes missed, upstreaming window closes | Upstream each fix (glslang #4186 already filed); drop fork when released |
| Tests not run in CI | Every green CI is build-only; regressions in tested libs ship silently | False confidence; the 8 test suites rot | **One-time CI change** (highest ROI) |
| `-Werror` only on MSVC | Warnings accumulate on Clang/GCC/Android unseen | "Hundreds of fixes later" when someone finally enables it | Enable `-Werror` incrementally per-library on Clang |
| `Mcp_server` 4.4k-line god-class | Hard to navigate/test; merge-conflict magnet | Slows MCP feature work; raises contributor bar | Split transport/dispatch/actions |
| ~27 deps, no vuln monitoring | Manual tracking of upstream CVEs | Vulnerable component ships | Dependabot for Actions + periodic pin review |
| Single-author bus factor | Specialized XR/Vulkan/forks knowledge | Project stalls if author unavailable | Already mitigated by strong docs (CLAUDE.md, notes.md, Memory Bank) |

---

## 8. Final Recommendations (ordered by ROI)

### Quick wins (< 1 day, high return)
1. **Run tests in CI.** Add `ERHE_BUILD_TESTS=ON` to the (Linux) configure step and a
   `ctest --test-dir <build_dir> --output-on-failure` step. Connects the existing
   GoogleTest suites to the gate. *(Foundation #5/#13; eliminates the single biggest
   "safety net exists but is unplugged" gap.)*
2. **Pin `concurrentqueue`.** Replace `GIT_TAG master` with the commented-out commit
   `00dd7ba09164...`. One-line change. *(Foundation #2; Security A06.)*
3. **Rotate the surfaced `CLAUDE_CHAT_API_KEY`** and confirm it stays out of any tracked
   file. *(Security 6.2.)*
4. **Centralize the GoogleTest pin** in the root dependency block (guarded by
   `ERHE_BUILD_TESTS`), removing it from 8 test CMakeLists. *(DRY / Foundation #2.)*

### Medium term (1-2 weeks)
5. **Embed erhe's own version + git hash** (`version.hpp.in` + `git rev-parse`), log it at
   startup alongside the dep commits already captured. *(Foundation #3.)*
6. **Enable `-Werror` on Clang/GCC incrementally.** Start with the leaf libs that already
   compile clean; expand library-by-library. Add a project `.clang-format` (and optionally
   `.clang-tidy`) so the `format-check`/`lint` commands in `cpp-project.md` have a config.
   *(Foundation #1.)*
7. **Add a fast/slow test label split** (`set_tests_properties(... LABELS fast)`), and an
   ASan/UBSan CI job (the `ERHE_USE_ASAN` option already exists). *(Foundation #5;
   sanitizer coverage.)*

### Strategic (1-3 months, optional)
8. **Refactor `Mcp_server`** into transport+auth / dispatch-table / per-domain action
   units. *(KISS / maintainability.)*
9. **Upstream the fork patches** and retire forks as releases land. *(Cost / Security.)*
10. **Introduce a typed error type** (`std::expected`-style) for library-level recoverable
    failures where `FATAL` is currently too blunt, starting at I/O and parse boundaries.
    *(Foundation #4 -- lowest urgency; the current model is acceptable for an editor.)*

---

## 9. Final Scorecard

| Principle | Score | Brief Motivation |
|-----------|-------|------------------|
| KISS | 4/5 | Disciplined; only the `Mcp_server` god-class and 6-fork surface stand out. |
| DRY | 4/5 | Clean; intentional logging duplication is correct. Only the scattered GoogleTest pin is a real miss. |
| YAGNI | 4/5 | Dead deps pruned (reflect-cpp), no speculative abstractions; a few defaulted-`none` optional integrations to watch. |
| Overengineering | 4/5 | Complexity tracks real cross-platform/XR needs; bus factor is the main risk, well-documented. |
| Project Foundations | 3/5 | ~7/16 Present, ~8 Partial, 1 Missing. Strong on logging/build/DI/config; weak on warnings-gate, test-gating, self-versioning, typed errors. |
| Security | 4/5 | Loopback-only MCP with timing-safe auth + payload caps is well done; gaps are optional-auth default and no dep scanning. |
| Cost/benefit ratio | 4/5 | Costs are developer-time (forks, missing test gate), not runtime/cloud; mostly cheap to address. |
| Maintainability | 3/5 | Excellent docs offset single-author risk; the unplugged test gate and Clang/GCC warning drift hurt long-term. |
| **Overall Score** | **3.6/5** | Solid, well-reasoned codebase. Highest-ROI fix: wire existing tests into CI. |

---

### Appendix: Evidence method note

All C++ symbol existence/usage/source claims were obtained via LSAI
(`mcp__lsai__lsai_search/outline/source/callers`, workspace `erhe-c-1`). Build, test,
dependency, config, and CI claims were read from CMake / JSON / YAML directly. No
`Missing` verdict was issued without the corresponding negative check actually run
(e.g. Foundation #5 "no LABELS" = grep over all `src/**/CMakeLists.txt` returning zero
matches; Foundation #4 "no std::expected" = LSAI `expected` search returning only
third-party hits). Vendored/generated trees were excluded from all counts.
