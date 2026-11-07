# Shader analytics in the editor

Stability: experimental

On desktop the editor analyzes every shader program it builds with the Arm
Mali Offline Compiler (`malioc`) and the Imagination PowerVR profiling compiler
(`ProfilingCompilerCLIWrapper`) in background tasks, so the shader cost
overdraw modes (the "Shader cost: <metric>" entries of the viewport's
shader debug combo, `doc/plans/shader_cost.md` B) are available without a
shader variant browser recording or `scripts/shader_analytics.py`. The
Shader Variants window also runs the full before / after analysis of the
session's programs itself ("Session programs" below);
`scripts/shader_analytics.py` remains the tool for a recorded browser
directory (`doc/erhe/graphics.md` "Mali analytics", "PowerVR analytics").

## Enabling

`Shader_analytics_service` (`src/editor/renderers/shader_analytics_service.*`)
runs when all of these hold:

- not Android (the editor's default there is `Shader_analytics_default::none`);
- no `--no-shader-analytics` on the command line;
- `shader_analytics.enabled` in the editor settings (default true; read at startup);
- at least one compiler is found.

Each compiler is searched in three sources, all of them, in this order:

1. the editor setting `shader_analytics.malioc_search_paths` /
   `shader_analytics.powervr_search_paths` (empty by default; the install
   locations are per machine, so they go in the local editor settings);
2. the environment variable `ERHE_MALIOC` / `ERHE_POWERVR`, a
   path list separated like `PATH`;
3. `PATH`.

An entry is the executable or a directory holding it; `$NAME`, `${NAME}`
and a leading `~` expand from the environment, and an entry naming an unset
variable is skipped. The same executable reached through two entries counts
once. The first analysis task queries every candidate's version
(`malioc --version`; the PowerVR wrapper has no version option, its version is the DDK
build the SPIR-V compiler library of the configured core embeds, and a
candidate without that core's library is skipped) and uses the newest of
each compiler. The log names every candidate found with its version and the
one used, or, for a compiler found nowhere, every location searched.

`--shader-analytics <directory>` still loads a browser directory's
`analytics.js` at startup, read before the device exists so a session that
records into the same directory gets the previous recording's results. The
service's results merge into the same `Shader_analytics`, by program.

## Session programs

The session program list (`Shader_session_programs`), its background
analysis (`Shader_analytics_service`) and the Shader Variants window read the
programs' SPIR-V modules, so they are built only with `ERHE_SPIRV` (the
Vulkan and Metal backends); an OpenGL or `none` build has neither, and MCP
`analyze_shader_programs` refuses there.

`Shader_session_programs` (`src/editor/renderers/shader_session_programs.*`)
owns `erhe::graphics::Device::set_shader_program_observer()`, which reports
every program the device builds from SPIR-V, compiled or served from the
`Spirv_cache`, with the modules the device uses (optimized when
`Graphics_config::spirv_optimization` is on). It keeps the session's
program list - per program the key (JSON `[name, variant defines]`), the
shader variant browser's id for it, the define value names, and per stage
the type, the `Spirv_cache` entry key and the module's size - and passes
each program to its listeners (the service below). It is created before
any program builds and outlives the service and the Shader Variants window.

Each stage's module sizes before and after the optimizer are
`Spirv_cache_module_stats` (`<key>.stats.json` beside the cache entry,
valid for the SPIRV-Tools version that computed them): read when the
program is reported, else computed from the record in background tasks
(at most `shader_analytics.max_jobs`, spawned from the main thread's commit
queue after `start()`) and stored, so a later session reads them at once.
Editor shutdown calls `stop()` before it drains the task executor: the
queued statistics and analysis jobs are dropped (the next session computes
them) and only the running ones complete, so the exit does not wait for
every program's optimizer run after a SPIRV-Tools version change.

With no browser directory open, the Shader Variants window
(`doc/editor/editor.md` "Shader variant browser") lists these programs,
with column groups for the cores the background analysis selected. The
table shows both modules' sizes once the stats are known (until then the
module the device uses); selecting a program reads its
stages' `Spirv_cache` records (`doc/erhe/graphics.md` "SPIR-V cache
records") for the Sources tab and both SPIR-V tabs, running the optimizer
for the module the cache does not hold, and keeps what it read by program
id. The list reloads when a program builds while it is shown.

The full analysis of a session program runs in the same background tasks
(statistics jobs first) once the background analysis has selected its
compilers (`Shader_analytics_service` passes its `Offline_compiler_tools`
to `set_offline_compiler_tools()`): selecting a program queues it first,
"Analyze all" queues every program not analyzed yet. A job takes every
stage's modules before and after the optimizer from the records and runs
`analyze_program_stages()` (`src/editor/renderers/offline_compiler.*`):
`malioc` and the PowerVR compiler per module, raw reports cached in `cache/shader_analytics/` under the keys of "What runs"
below. The summaries are those of `scripts/shader_analytics.py`, field for
field: per core the `analytics.js` core block and the program's stage
blocks, from which the window builds its column groups and core tabs as for
a recorded directory. Over a 148-program default session (Release, Intel)
the results equal the script's `analytics.js` for the same recording in
every Mali program block and in the PowerVR fragment and compute
blocks (the PowerVR compiler varies run to run on vertex modules,
`doc/erhe/graphics.md` "PowerVR analytics"). Results stay for the session.

The MCP tool `analyze_shader_programs` queues the analysis (`analyze`:
`all`, or `listed` with `program_ids`) and reports the status counts, the
programs with their ids, and the analysis of the `program_ids`.

## What runs

The service keeps a program
when it has a fragment stage and is not an overdraw counting variant
(`ERHE_OVERDRAW` other than 0; the cost modes weigh those by their
`ERHE_OVERDRAW` off twin), once per program key - the JSON `[name, variant
defines]` the cost modes and `fragments.js` match by.

The observer only queues a job. A kick posted to the main thread's
`Scene_commit_queue` spawns up to `shader_analytics.max_jobs` (default 2)
Taskflow tasks; a task runs jobs until the queue is empty. Analysis therefore
starts with the main loop's first commit-queue flush, after startup, and
holds at most `max_jobs` worker threads (each waiting on one compiler
process) while scenes load. The first task selects the compilers
("Enabling" above).

Per job (`analyze_program()`, `src/editor/renderers/offline_compiler.*`):

- `malioc --vulkan --spirv -c <mali_core> --format json` on the fragment module;
- `ProfilingCompilerCLIWrapper -a spv -b <BVNC> -p <its directory> -i
  frag.spv` on the fragment module, the BVNC resolved from
  `shader_analytics.powervr_core` (default `PowerVR-BXM-8-256`) through the
  wrapper's `-h` list; the report is `frag.spv.json` in the working
  directory;
- each compiler runs as a child process in its own scratch directory under
  the system temporary directory, stdin empty and stdout / stderr captured to
  files (the PowerVR
  wrapper writes its report there).

Each raw report, plus an `erhe` block (core, compiler versions, command line,
exit code; for PowerVR the BVNC and, on failure, the compiler's text), is
kept in `cache/shader_analytics/<hash>.<core>.malioc.json` /
`.powervr.json`, the hash being FNV-1a 64 of the compiler, its versions, the
core (the BVNC for PowerVR) and the module bytes.
A module is compiled once per compiler version; a later session reads its
reports. A failed compile is a cached result too.

From the reports the metrics of `Shader_cost_metric` are taken exactly as
`Shader_analytics::read()` takes them from `analytics.js`: Mali fragment
`Main` total cycles (the largest pipeline's, and the `arith_total`,
`texture` and `load_store` pipelines'), PowerVR fragment `Main` counters (the naive cycle count, a loop body counted
once; instructions, samples, temporary registers). A report whose shape the
parser does not expect counts as a failed analysis of that program (logged
with the program name).

## Results

Finished jobs land on the main thread in one `Scene_commit_queue` commit per
batch: `Shader_analytics::add_programs()` merges them (a compiler whose core
differs from analytics already loaded contributes nothing) and reinstalls
the overdraw weights, so cost variants that weighed 0 for lack of analytics
resolve again. Until a program's analysis lands it weighs 0; the viewport
shows the count of such variants in cost mode.

The MCP tool `load_shader_analytics` reports the service as `service`:
`mali_core`, `powervr_core` (empty for a compiler not found), `queued`,
`running`, `analyzed`, `failed`; null when the service is off.

Measured on the Linux desktop (default scene, headless Release, Intel):
113 fragment programs analyzed about 8.5 s after the first frames with an
empty report cache, about 0.1 s with the cache filled; the first frames are
not delayed.

## Future work

- [plans/shader_analysis_followups.md](../plans/shader_analysis_followups.md)
