# Shader analytics in the editor

Stability: experimental

On desktop the editor analyzes every shader program it builds with the Arm
Mali Offline Compiler (`malioc`) in background tasks, so the shader cost overdraw modes (the
"Shader cost: <metric>" entries of the viewport's shader debug combo,
`doc/plans/shader_cost.md` B) are available without a shader variant
browser recording or `scripts/shader_analytics.py`. The script remains the
tool for a browser directory's full before / after analytics
(`doc/erhe/graphics.md` "Mali analytics").

## Enabling

`Shader_analytics_service` (`src/editor/renderers/shader_analytics_service.*`)
runs when all of these hold:

- not Android (the editor's default there is `Shader_analytics_default::none`);
- no `--no-shader-analytics` on the command line;
- `shader_analytics.enabled` in the editor settings (default true; read at startup);
- the compiler is found.

The compiler is searched in three sources, all of them, in this order:

1. the editor setting `shader_analytics.malioc_search_paths` (empty by
   default; the install locations are per machine, so they go in the local
   editor settings);
2. the environment variable `ERHE_MALIOC`, a path list separated like
   `PATH`;
3. `PATH`.

An entry is the executable or a directory holding it; `$NAME`, `${NAME}`
and a leading `~` expand from the environment, and an entry naming an unset
variable is skipped. The same executable reached through two entries counts
once. The first analysis task queries every candidate's version
(`malioc --version`) and uses the newest. The log names every candidate
found with its version and the one used, or, when the compiler is found
nowhere, every location searched.

`--shader-analytics <directory>` still loads a browser directory's
`analytics.js` at startup, read before the device exists so a session that
records into the same directory gets the previous recording's results. The
service's results merge into the same `Shader_analytics`, by program.

## What runs

`erhe::graphics::Device::set_shader_program_observer()` reports every program
the device builds from SPIR-V, compiled or served from the `Spirv_cache`,
with the modules the device uses (optimized when
`Graphics_config::spirv_optimization` is on). The service keeps a program
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
- the compiler runs as a child process in its own scratch directory under
  the system temporary directory, stdin empty and stdout / stderr captured to
  files.

Each raw report, plus an `erhe` block (core, compiler version, command line,
exit code), is kept in `cache/shader_analytics/<hash>.<core>.malioc.json`,
the hash being FNV-1a 64 of the compiler, its version, the core and the
module bytes. A module is compiled once per
compiler version; a later session reads its reports. A failed compile is a
cached result too.

From the reports the metrics of `Shader_cost_metric` are taken exactly as
`Shader_analytics::read()` takes them from `analytics.js`: Mali fragment
`Main` total cycles (the largest pipeline's, and the `arith_total`,
`texture` and `load_store` pipelines'). A
report whose shape the parser does not expect counts as a failed analysis
of that program (logged with the program name).

## Results

Finished jobs land on the main thread in one `Scene_commit_queue` commit per
batch: `Shader_analytics::add_programs()` merges them (a compiler whose core
differs from analytics already loaded contributes nothing) and reinstalls
the overdraw weights, so cost variants that weighed 0 for lack of analytics
resolve again. Until a program's analysis lands it weighs 0; the viewport
shows the count of such variants in cost mode.

The MCP tool `load_shader_analytics` reports the service as `service`:
`mali_core` (empty when the compiler is not found), `queued`,
`running`, `analyzed`, `failed`; null when the service is off.

Measured on the Linux desktop (default scene, headless Release, Intel):
113 fragment programs analyzed about 8.5 s after the first frames with an
empty report cache, about 0.1 s with the cache filled; the first frames are
not delayed.
