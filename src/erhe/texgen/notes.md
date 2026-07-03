# erhe_texgen

## Purpose
Procedural texture shader-code composition core (Phase 1 of
`doc/texture-graph-plan.md`, issue #199). Ports the semantics of Material
Maker's GLSL composition engine (https://github.com/RodZill4/material-maker,
MIT license): nodes contribute GLSL snippets described by immutable data
tables, and composing a node DAG produces one monolithic fragment-shader body
plus an ordered list of live-editable uniforms. Pure string logic - no GPU,
no graphics dependency; GPU validation lives in `src/erhe/graphics/test/`
(Phase 2) and the editor node graph bridges to this library later (Phase 3).

## Key Types
- `Value_type` -- MVP value types flowing between nodes: `grayscale` (GLSL
  float, Material Maker "f"), `rgb` (vec3), `rgba` (vec4). Conversion
  expression templates between all pairs (from Material Maker's
  io_types.mmt); `convert(expression, from, to)` wraps an expression.
- `Shader_code` -- accumulated composition result: globals (de-duplicated by
  exact content), uniforms (de-duplicated by name, registration order),
  defs (emitted input helper functions), inline code, and the output
  expression + type. Port of Material Maker's ShaderCode.
- `Node_descriptor` -- immutable data table mirroring Material Maker's
  shader_model: inputs (name, type, default expression, `function` flag),
  outputs (type + expression template), parameters (float/color/enum/bool/
  size), `global` GLSL library, inline `code` statements.
- `Compose_node` -- lightweight non-owning DAG node: descriptor pointer,
  stable integer id (feeds `o{id}` name generation), per-instance parameter
  values, per-input bindings (upstream node + output index, null =
  unconnected), per-node seed.
- `Composer` / `Compose_options` -- `compose(sink, output_index)` walks
  upstream into one `Shader_code`; `assemble_fragment(shader_code)` produces
  the final fragment-shader body string.
- `common_library_glsl()` -- rand/hash/param_rnd/hsv GLSL library ported from
  Material Maker's shader_functions.tres (MIT attribution in the source).

## Public API
- `Composer::compose(const Compose_node& sink, std::size_t output_index) -> Shader_code`
- `Composer::assemble_fragment(const Shader_code&) -> std::string`
- `Shader_code::get_uniforms() -> const std::vector<Uniform>&` -- ordered
  (sink first, then upstream in substitution order); name/kind/current value
  are sufficient to build a UBO and re-upload values without recompiling.
- `Compose_node::set_float/set_color/set_enum_index/set_bool/set_size_exponent/set_seed`,
  `set_input/clear_input`
- `convert(expression, from, to) -> std::string`

## Substitution grammar (in descriptor template strings)
- `$param` / `$(param)` -- float/color parameters become uniform references
  `p_o{id}_{param}` (prefixed with `Compose_options::uniform_accessor_prefix`,
  default empty); enum parameters substitute their code fragment; bool ->
  `true`/`false`; size -> the `pow(2, exponent)` float literal (`"{:.1f}"`);
  gradient/curve parameters substitute the NAME of a per-node helper function
  (`o{id}_{param}_gradient` returning `vec4`, `o{id}_{param}_curve` returning
  `float`) emitted once into globals, so `$gradient($input($uv))` becomes
  `o5_gradient_gradient(<input>)`.

### Gradient / curve parameter codegen (Phase 4)
`Parameter_kind::gradient_parameter` / `curve_parameter` carry control-point
data on `Parameter_descriptor` (defaults) and `Parameter_value` (live values:
`gradient_stops` + `gradient_interpolation`, `curve_points`).
`Compose_node::set_gradient` / `set_curve` sort and strictly-increasing-nudge
the control points (mirrors Material Maker's `MMGradient.sort`) so the emitted
ladders never divide by a zero span; an empty gradient degrades to one black
stop. The composer emits `vec4 <fn>(float x)` (gradient: CONSTANT / LINEAR /
SMOOTHSTEP / CUBIC if/else ladder) and `float <fn>(float x)` (curve: piecewise
Hermite) into globals, ported faithfully from Material Maker's
`MMGradient.get_shader` / `MMCurve.get_shader` (types/gradient.gd,
types/curve.gd, MIT).

**DECISION - control points are baked as GLSL constants, not uniforms.** Where
Material Maker uploads the stops/points into `p_<name>_pos[]` / `p_<name>_col[]`
/ `p_<name>_<i>_x` uniform arrays (so a value edit updates uniforms without a
recompile), erhe bakes the literals into the function body. Any value edit
therefore recomposes the source and recompiles - but the on-disk SPIR-V cache
de-duplicates unchanged sources, and this keeps the codegen pure string logic
with no std140 array-uniform layout or per-frame array upload. **Future
optimization:** emit the control points as std140 uniform-array members (like
the float/color uniforms already do) so gradient/curve value edits skip the
recompile; structural edits (add/remove stop) would still recompile. Not done
in this chunk - constant-baked is correct and much simpler.
- Built-ins: `$uv` (current coordinate expression), `$seed` (per-node float
  uniform `p_o{id}_seed`), `$name` -> `o{id}`, `$name_uv` -> variant-unique
  `o{id}_{variant}` (only defined inside nodes with a `code` stanza),
  `$time` -> `elapsed_time`, `$node_id` -> the raw id.
- `$input_name(expr)` -- samples the input at `expr`: inline form composes
  the upstream subtree with `$uv` rebound to `(expr)` (one redundant
  parenthesis level is collapsed); function form (input has `function` flag)
  emits `o{id}_input_{name}(vec2 uv)` into defs once and substitutes a call.
  Unconnected inputs use the default expression with `$uv` rebound, wrapped in
  parentheses (unless already a single parenthesized group) so its precedence
  matches the connected path's atomic local; defaults are authored in the
  input's own type (no conversion). Type mismatches on connected inputs insert
  the `Value_type` conversion expression.
  Cycles in the graph (self-binding or longer loops, inline or function-form)
  and pathologically deep chains are broken by an active-path guard / depth cap
  (256): the offending re-entry yields a `/* (error: cycle) */` (or
  `max depth`) zero-literal of the expected type so composition always
  terminates and still assembles textually.
- `$rnd(a, b)` -> `param_rnd(a, b, $seed + <offset>)` and
  `$rndi(a, b)` -> `param_rndi(a, b, $seed + <offset>)` (`$rndi(` is matched
  before `$rnd(`); the offset is `sin(position + base) * 0.5 + 0.5` of the
  occurrence's character position, distinct per occurrence (deviation from
  Material Maker, which collapses textually identical calls to one offset).
  `base` is a per-template-string offset (17 apart, like Material Maker's
  `rnd_offset`) so two template strings whose `$rnd` sits at the same
  character position still decorrelate. Rewriting is applied to output/code
  templates, to selected enum code fragments, and to unconnected-input default
  expressions; nested `$rnd` inside an argument is rewritten too. Any template
  referencing `$rnd(`/`$rndi(` (including inside an enum value's code) makes
  the node seeded.
- Parsing scans right-to-left like Material Maker's `replace_variables`, so
  the arguments of `$input(...)` are already substituted when the input is
  resolved. Identifier extraction takes the longest run of word characters
  (`[A-Za-z0-9_]`), so `$uv_scale` never partially matches `$uv`; the
  `$(name)` form glues against adjacent identifier text (`blend_$(mode)`).
  Unknown variables substitute an `(error: name not found)` marker instead
  of throwing.

## Composition model
- Every node output is evaluated into a local
  `o{id}_{output_index}_{variant}_{typekey}` declared in the inline code;
  downstream references the local. A variant context (port of Material
  Maker's MMGenContext) keys stanzas by (node, uv expression): repeated
  samplings at the same coordinates share one stanza and its locals, a
  different coordinate expression creates a new variant with distinct local
  names. Function-form input bodies use a child context (parent declaration
  state visible, independent local numbering).
- Per-node declarations (uniforms, globals, helper functions) are generated
  exactly once per composition.
- `assemble_fragment` output layout: common library (only when the assembled
  text references one of its functions - `Common_library_mode` can force
  always/never), de-duplicated globals, uniform declarations (plain
  `uniform float name = value;` by default; `Uniform_declaration_mode::none`
  when the caller declares a UBO block and sets the accessor prefix), defs,
  then `void main() { vec2 uv = <uv_source_expression>; <code>;
  <output_variable> = <vec4 expr>; }`. Grayscale sinks wrap as
  `vec4(vec3(v), 1.0)`, rgb as `vec4(v, 1.0)`. Function name, output
  variable and uv source are options (defaults: `main`, `out_color`,
  `v_texcoord`).

## Performance notes / Known limitations
- **O(d^2) Shader_code merges.** `compose_node` returns a `Shader_code` by
  value at each recursion level, and each level re-merges its children's
  globals/uniforms/defs/code into a fresh accumulator (deduplicating globals
  and uniforms by linear scan). For a chain of depth `d` this copies and
  re-dedups O(d^2) in the worst case. Fine for current small graphs; if real
  graphs grow large, thread a single accumulator through the recursion and
  keep hashed dedup indexes (name->bool for uniforms, content-hash set for
  globals). Measure first - the string volume is small in practice.
- **Comment-blind parenthesis / $rnd scanning.** `find_matching_parenthesis`
  and `replace_rnd` treat the template text as bare GLSL: a `)` (or a `$rnd(`)
  living inside a `//` or `/* */` comment in a coordinate/argument expression
  is not skipped, so it can break parenthesis matching or argument extraction.
  Material Maker has the same limitation. Node authors must keep comments out
  of expression templates that flow through input coordinates or $rnd
  arguments.
- **Right-to-left `replace_variables` shuffle.** Each resolved `$` prepends the
  processed suffix to a growing `tail` string (`tail = head.substr(...) +
  tail`), an O(n) copy per occurrence, so a template with `k` variables is
  O(n*k). The right-to-left order is load-bearing (an appended substitution -
  e.g. the `$seed` injected into an expanded enum `$rnd` fragment - is
  re-scanned in place), so a segment-and-join rewrite must preserve that exact
  re-scan semantics. Left as-is: templates are short and this is not a
  measured hot path.

## Dependencies
- **erhe libraries:** `erhe::verify` (private)
- **External:** `fmt` (private)
- Deliberately does NOT depend on `erhe::graph` or `erhe::graphics`: texgen
  has its own tiny compose-time DAG model; the editor bridges from its node
  graph, and GPU work stays in graphics tests / editor code.

## Notes
- Composition runs on edits, not per frame, so normal allocation is fine
  here (the no-runtime-alloc discipline does not apply).
- Tests: `src/erhe/texgen/test/` (`erhe_texgen_tests`, gated behind
  `ERHE_BUILD_TESTS`), pure string assertions, includes byte-exact golden
  assemblies. Test-local descriptors live in `test/test_descriptors.hpp`.
- GLSL ported from Material Maker carries an MIT attribution comment
  (`common_library.cpp`, conversion table in `value_type.cpp`).
