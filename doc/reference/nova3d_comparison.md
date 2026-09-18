# Nova3D vs. erhe AI creation tools

Comparison of [Nova3D](https://github.com/RareSense/Nova3D) against erhe's AI creation
stack (the in-editor MCP server, `scripts/creations/`, and the `erhe-creations` skill —
see `doc/ai_creations.md`). Reviewed from `C:/git/tksuoran/Nova3D` (v1.3.1 era,
2026-08). The goal is to identify features worth adding to erhe.

## What Nova3D is

Nova3D is a "code-native" AI 3D asset generator: an LLM writes Blender Python
construction code, headless Blender executes, validates, and repairs it, and the output
is a structured GLB with named parts, an assembly hierarchy, joint pivots, and PBR
materials. The asset *is* the program; the mesh is its compiled output. It is
positioned against diffusion-based generators (Meshy, Tripo, TRELLIS, Hunyuan3D),
which produce fused, unstructured meshes.

Important caveat: the generation backend ("GraphFlow", including the code-writing LLM
prompts, the Blender execution sandbox, the repair loop, and the screenshot-based
validation LLM) is **closed-source and hosted**. The public repo contains three
clients that all talk to `nova3d.xyz/api`:

- **Flutter web app** — chat UI with an embedded Three.js editor (vanilla ES modules,
  iframe + same-origin postMessage bridge).
- **`nova3d-mcp`** — a Python FastMCP server (9 tools) so any MCP host (Claude Code,
  Cursor, …) can generate and edit assets.
- **Blender add-on** — generate from inside Blender; results import as named meshes
  and the generated `code.py` opens in the Text Editor.

## Two opposite architectures

The most interesting contrast is structural, and both projects use MCP — in opposite
directions:

- **erhe is an MCP *server*.** The editor exposes ~193 tools (scene ops, geometry ops,
  node graphs, physics, animation, lightmaps, screenshots) and an external agent is
  the intelligence. The scene is built tool call by tool call inside a live engine;
  the "asset program" is the Python creation script the agent writes
  (`scripts/creations/`). All state, rendering, and verification are local.
- **Nova3D's clients are MCP/HTTP *clients* of a hosted generation service.** The
  intelligence and the 3D construction both happen server-side; the clients submit a
  prompt, poll a workflow, and receive a finished GLB plus the Blender code that
  produced it. The local editor is for human touch-up only — the AI never drives it.

Both converge on the same core thesis: **generated 3D should be a program with named,
separately editable parts**, not an opaque mesh. erhe's creation scripts and Nova3D's
Blender `code_artifact` play the same role.

## Comparison table

| Aspect | Nova3D | erhe AI creations |
|---|---|---|
| Core idea | Hosted LLM writes Blender Python → headless Blender builds structured GLB | External agent drives a live editor through MCP tools; Python script is the asset's source |
| AI integration direction | Clients call a hosted generation API; MCP server (9 tools) fronts it for agents | Editor **is** the MCP server (~193 static tools + all editor commands) |
| Openness | Clients MIT; generation backend closed, hosted, credit-billed | Fully local and open; no service dependency, no billing |
| LLM providers | Multi-model routing (Claude Opus 5 / Fable 5, GPT-5.5/5.6, Gemini, Kimi), hosted credits or BYOK keys | Agent-agnostic — whatever drives the MCP client (typically Claude Code) |
| Text prompt → asset | Yes — primary interface (≤40 words + up to 3 reference images) | Indirect — agent interprets the prompt and authors tool calls / a script |
| Image reference input | Yes (sketch/photo → 3D) | No |
| Generation model | Blender code gen → execute → **auto-repair loop** → **screenshot validation LLM** → correction | Agent plans, calls tools, screenshots via `capture_screenshot`, self-corrects in-session |
| Part-level editing | `regenerate_part`, `add_part` — rebuild one named component by description | Full granularity: any node/mesh/component addressable; geometry ops target specific nodes |
| Geometry operations (AI-drivable) | None client-side (all generation server-side) | remesh, decimate, smooth, chamfer, Catmull-Clark, CSG, lattice/FFD, sweep, hulls, edge sharpness, component selection |
| Node graphs | None | Geometry graph + texture graph, fully AI-drivable |
| Articulation | `articulate_model` — kinematic joint pivots/axes in GLB, per-joint sliders, demo animation | Full Jolt physics: six-dof joints with limits/drives/motors, ragdolls, manual sim clock |
| Physics simulation | None (articulation is kinematic metadata only) | Jolt rigid bodies, materials, filters, wind, deterministic settling |
| Animation | None | Keyframe create/edit/delete, playback control via MCP |
| Materials/texturing (AI) | "Magic Texture": UV atlas generation from source code + PBR texture painting/baking at 1K–4K | PBR parameter editing, procedural texture graph, lightmap baking; **no generative texture painting** |
| UV generation | Dual-mode auto unwrap (combined atlas + per-group), delivered as checker GLB + SVG atlases | `generate_texture_coordinates` / `project_texcoords`; lightmap chart/tile tooling |
| Long-running ops | Fully async: submit → `workflow_id` → poll every 3 s; runs up to ~45 min; resumable after client restart | 5 s hard request timeout; fire-and-poll via `get_async_status` for some ops; no durable job ids |
| Progress reporting | GraphFlow node ids mapped to human labels ("Writing the Blender scene…", "Repairing the Blender script…") | None beyond `get_async_status` |
| Session/state carrier | `code_artifact` with injected conversation id; MCP writes into web app's chat history; 24 h workflow store survives restarts | The scene + the creation script; `--reuse`/`--only`/`--reframe` incremental modes; node ids reshuffle per launch |
| Undo of AI actions | Local editor snapshot undo; coarse server-backed **asset versions** to step between AI generations | Real operation-stack undo; `batch` groups a burst into one undo entry (not uniform across all tools) |
| Human editing UI | Three.js editor: selection, gizmos, sculpt brushes (grab/inflate/smooth/flatten), mirror/merge/subdivide/decimate, material presets, explode view | Full native editor: gizmos, mesh component modes, node graph editors, properties, brushes |
| AI chat UI | Yes — chat-first web app with progress messages, retry, version switching | None — interaction via external agent / CLI / scripts |
| Error handling | Recoverable-vs-fatal classification, bounded retries, idempotent starts via client request ids, 11 error categories, verbatim user-facing failure messages | `isError` + stringly-typed message in a text block; per-request exception boundary |
| Auth/security | OAuth loopback login, 0600 session files, key scrubbing, BYOK keys browser-local, analytics scrubber | Bearer token file (auth **off** if absent), loopback-only bind, plain HTTP |
| Formats | GLB in/out only | glTF import/export with `ERHE_brushes`/`ERHE_physics`/`ERHE_scene` extensions, prefabs, asset manager |
| Headless operation | Server-side (closed) | First-class: headless build runs full pipeline + MCP + screenshots |
| DCC integration | Blender add-on (import as named meshes, code in Text Editor, crash-resumable jobs, safe self-update) | None |
| Agent-facing docs | FastMCP `instructions`, `llms.txt`, README decision tree keyed on `next_action` | `AGENTS.md`, `.agents/skills/erhe-creations/` with maintenance contract, `doc/mcp_api_guidelines.md` |
| Multi-object scenes | No — one asset per conversation | Yes — full scene composition is the native mode |
| Generative geometry (diffusion/learned) | No (code-native by design) | No (parametric/CSG/L-systems by design) |

### Summary judgment

erhe's AI-drivable *capability surface* is far larger (geometry ops, node graphs,
physics, animation, lightmaps, full scenes vs. Nova3D's seven hosted workflows). What
Nova3D has that erhe lacks is almost entirely **product/UX and workflow
infrastructure**: prompt-first entry, async job durability, progress narration,
part-level regeneration semantics, generative texturing, versioning, and polished
error handling. Those are the transferable ideas.

## Features worth adding to erhe AI creations

Ordered roughly by leverage-for-effort.

### Workflow / protocol

1. **Durable async jobs with workflow ids.** Nova3D's submit → `workflow_id` → poll
   pattern, with a client-side workflow store (24 h TTL) that survives restarts, is a
   much better long-running-op story than erhe's 5 s `k_request_timeout` +
   `get_async_status`. Give slow tools (geometry ops, lightmap prepare, asset loads,
   glTF import) a uniform `job_id` + `get_job_status`/`get_job_result`/`cancel_job`
   contract, persisted across editor restarts where feasible.
2. **Idempotent starts.** Nova3D lets the client supply the request id
   (`state-<timestamp>`), so a timed-out submit can be polled instead of re-run.
   Accept an optional client-supplied idempotency key on mutating tools (or at least
   on `batch`), so an agent that hits the 5 s timeout can find out whether the
   mutation landed instead of guessing.
3. **Progress narration for long operations.** Nova3D maps internal pipeline stages
   to human-readable labels ("Repairing the Blender script…"). erhe jobs could report
   a `stage` + `detail` string (e.g. "remeshing 3/7 nodes", "lightmap tile 12/40") in
   `get_async_status` — cheap, and it lets agents give users live feedback and detect
   stalls.
4. **Structured error contract.** Nova3D classifies failures into named categories
   (`invalid_api_key`, `blender_generation_failed`, …), separates recoverable from
   fatal, and prioritizes a `user_message`. erhe tool errors are free-text inside
   `isError`. Add `{error_code, recoverable, message, detail}` to tool failures and
   document the codes in `mcp_tools.json`. This directly improves agent retry logic.

### Creation semantics

5. **Part-level regeneration as a first-class idiom.** Nova3D's `regenerate_part` /
   `add_part` ("replace the roof with a dome, keep everything else") maps naturally
   onto erhe's `--only` creation-script flag — but only for script-built scenes.
   Consider a tool-level equivalent: tag a subtree with the *recipe* (script +
   function or a batch of tool calls) that produced it, so an agent can re-run just
   that recipe in place. The `ERHE_scene` extension could carry the provenance.
6. **Asset/scene versioning.** Nova3D keeps successive AI generations as switchable
   versions. erhe creations overwrite the saved `.glb`. Cheap version: save
   `creations/<name>.v<N>.glb` alongside a `latest` pointer, plus an MCP tool to list
   and load versions. This makes agent iteration safer than undo alone (undo dies
   with the session).
7. **Stable node addressing across sessions.** Nova3D's `code_artifact` carries
   conversation identity through every edit. erhe node ids reshuffle every launch
   (documented in `AGENTS.md`), forcing re-query before every `select_items`. A
   persistent per-item GUID (serialized via `ERHE_scene`) addressable by MCP tools
   would remove a whole class of agent boilerplate and stale-id bugs.
8. **Reference-image input for creations.** Nova3D accepts up to 3 reference images.
   erhe's equivalent would be purely conventional — a documented place to drop
   reference images plus a skill instruction to view them before building — but it is
   currently absent from the creation workflow entirely.

### Texturing / materials

9. **Automatic UV atlas modes.** Nova3D runs two unwraps in parallel — one combined
   atlas and one per-hierarchy-group — and delivers checker-textured previews plus
   SVG atlas layouts. erhe has the machinery (geogram atlas packing, lightmap charts,
   `generate_texture_coordinates`); packaging an `unwrap` tool with
   `atlas_mode: combined|per_group` and an atlas-preview export would close the gap.
10. **AI-paintable textures.** Nova3D's "Magic Texture" bakes PBR texture sets onto
    the generated UVs. erhe's texture graph is procedural-only. A bridge tool that
    imports an externally generated image set (base color / normal / roughness) onto
    a material's texture slots — combined with (9) — would let an agent use any image
    model for texturing without erhe hosting one.

### Product / UX

11. **In-editor MCP status panel.** Nova3D surfaces auth, credits, and `next_action`
    everywhere. erhe's server has *no* UI at all — not even a visibility toggle. A
    small window showing server state (port, auth on/off, recent tool calls, queue
    depth, per-call timing) would help both debugging and trust; a recent-calls log
    doubles as an audit trail for AI mutations.
12. **Auth on by default.** Nova3D refuses weak session-file permissions and scrubs
    keys aggressively; erhe silently disables auth when `~/.agents/erhe_mcp_token` is
    missing. Generate a token on first launch instead, and surface auth state in the
    panel from (11).
13. **`llms.txt`-style tool overview.** Nova3D ships a compact agent-facing summary
    (`llms.txt`, README decision tree keyed on `next_action`). erhe's 6249-line
    `mcp_tools.json` is exhaustive but heavy; a generated one-page tool index grouped
    by category (creatable from the JSON at build time) would cut agent onboarding
    tokens substantially.
14. **A minimal chat/companion surface (optional, larger).** Nova3D is chat-first;
    erhe deliberately is not, and the external-agent model is a strength. But a thin
    in-editor "agent console" that shows what the connected agent is doing (tool call
    stream, screenshots it took, pending jobs) — without hosting an LLM — would give
    the erhe workflow the observability Nova3D's chat provides.

### Hardening (small, from Nova3D's error-handling playbook)

15. **Uniform strict argument validation.** Nova3D's backend rejects unknown payload
    keys; erhe implements unknown-key rejection only for `create_shape` /
    `place_brush*` (added after a silently ignored `radius=` cost a build). Roll the
    allowlist pattern out to all mutating tools — the schemas in `mcp_tools.json`
    already exist to validate against.
16. **Make undo coverage uniform.** Nova3D snapshots before every AI edit; erhe's
    operation-stack coverage is good but uneven (`mcp_server_assets.cpp` has none;
    physics *edit* tools apply immediately). Audit and close the gaps, and consider
    gating `clear_undo_history` behind a confirmation argument so an agent cannot
    casually destroy recovery state.

## What erhe should *not* copy

- **Hosted/closed generation service and credit billing** — antithetical to erhe's
  local, open model, and the local live-engine loop (build → screenshot → adjust) is
  erhe's core advantage over Nova3D's ~45-minute opaque server runs.
- **Diffusion-style mesh generation** — both projects independently rejected it for
  the same reason (unstructured fused output); Nova3D validates erhe's code/tool-native
  bet.
- **In-process LLM client** — the agent-as-MCP-client architecture keeps erhe
  model-agnostic for free; Nova3D needs a model catalog, key management, and a
  billing wallet to achieve the same.
