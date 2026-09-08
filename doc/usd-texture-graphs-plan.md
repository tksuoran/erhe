# Texture graphs in a USD file (USD plan E4c)

The plan step `doc/usd-compatibility-plan.md` E4c: an erhe texture graph
(`doc/texture-graph-plan.md`) rides a USD file as the `UsdShade` network
it is, so a USD-backed scene keeps its graphs across a save and a
`UsdShade`-aware tool reads the same prims. This document owns the
design; the plan step refers here.

## 1. Requirements

- R1 A `Graph_texture` asset is a `NodeGraph` prim where the asset sits
  in the tree (U4: a resource is a prim where it sits); every node of the
  graph is a `Shader` child of it; every link is an attribute connection.
  Nothing rides a JSON string.
- R2 A material slot that samples the graph's result is the material's
  own `UsdPreviewSurface` input connected to the `NodeGraph`'s interface
  output, in place of a `UsdUVTexture`: the file's native way of feeding
  a computed value into a surface.
- R3 The baked image is not written (as in glTF: a graph loads born
  dirty and the first evaluation re-bakes). A tool without erhe's nodes
  sees a material whose input connects to a network it cannot evaluate,
  which is the same outcome MaterialX gives a tool without MaterialX.
- R4 A save and a reload keep the graph node for node, parameter for
  parameter, link for link, node position included; a second save is
  byte-identical (E3).
- R5 A `NodeGraph` a foreign file authors without erhe's marker (section
  2.1) is not an erhe graph and is left to Tydra's material conversion
  exactly as today.
- R6 The writer in `erhe::usd` names no editor type (the X3 and E4a
  rule): the editor hands the graph over in a neutral record and
  rebuilds it from one.

## 2. Design

### 2.1 Prims

```
def Scope "Graph_Textures" {
    def NodeGraph "Rust" (
        # the marker: an erhe texture graph, not a foreign network
    ) {
        custom token erhe:graph:format = "erhe_texture_graph"
        color3f outputs:rgb.connect = </World/Graph_Textures/Rust/Output.inputs:rgb>

        def Shader "Noise" {
            uniform token info:id = "erhe:texture:noise"
            custom float2 erhe:ui:position = (120, 40)
            float inputs:scale = 4
            int inputs:octaves = 3
            float outputs:grayscale
        }
        def Shader "Colorize" {
            uniform token info:id = "erhe:texture:colorize"
            custom float2 erhe:ui:position = (320, 40)
            float inputs:value.connect = </World/Graph_Textures/Rust/Noise.outputs:grayscale>
            string inputs:gradient = "..."
            color3f outputs:rgb
        }
        def Shader "Output" {
            uniform token info:id = "erhe:texture:output"
            custom float2 erhe:ui:position = (520, 40)
            color3f inputs:rgb.connect = </World/Graph_Textures/Rust/Colorize.outputs:rgb>
        }
    }
}

def Material "Iron" {
    def Shader "PreviewSurface" {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor.connect = </World/Graph_Textures/Rust.outputs:rgb>
    }
}
```

- The `NodeGraph` prim is the `Graph_texture` asset: its name is the
  asset's name, its place is the asset's place (the `Graph Textures`
  kind scope when nothing else places it, spelled `Graph_Textures` by the
  identifier rule of E1). `erhe:graph:format` is the marker
  R5 needs and takes the value the glTF `ERHE_node_graphs` `format`
  field takes, so both formats spell the graph kind the same way.
- A `Shader` prim is one node: its name the node's name (sibling-unique,
  M2), `info:id` the factory type name under the `erhe:texture:`
  prefix (`make_texture_graph_node` takes the same string back), and
  `erhe:ui:position` the node's editor position.
- A node parameter is an `inputs:<name>` attribute typed from the
  parameter's registered property type by the mapping's value rows
  (`doc/usd_compatibility.md` "Property system": float, int, bool, token for
  an enumeration, float2 for a size, color3f / color4f for a color); a
  type with no USD form (gradient, curve) travels as its D16 text in a
  `string` attribute, one rule for both.
- A pin is an `inputs:<pin>` / `outputs:<pin>` attribute typed from the
  pin's value type: `grayscale` is `float`, `rgb` is `color3f`, `rgba`
  is `color4f`. An input pin with a link is the attribute with a
  `.connect` to the source node's output; an input pin without one is
  the attribute with no value, so the pin exists in the file.
- The graph's result: the `output` sink node's input source is also the
  `NodeGraph`'s interface `outputs:<pin>` connection, so the graph has a
  value a material can name (R2). The `material_output` and `buffer`
  sinks are nodes like any other (their meaning is erhe's).
- Recognition on read: a `NodeGraph` prim carrying the marker is a
  texture graph; a `Shader` child whose `info:id` lacks the
  `erhe:texture:` prefix, or names a kind the factory does not make, is
  one warning and no node, and the links into it are dropped with it.
  A `NodeGraph` without the marker is R5.

### 2.2 The record between erhe::usd and the editor

`Usd_data::node_graphs` (read) and `Usd_save_arguments::node_graphs`
(write) carry a `Usd_node_graph`: stage path, name, format token, the
interface outputs (pin name, type, source node and pin), and the nodes,
each with name, type name, position, parameters as (name, D16 text),
input pins as (name, value type, optional source node and pin) and
output pins as (name, value type). Values are text on both sides, as
the X2 override values are, so `erhe::usd` needs no node vocabulary.
The editor's `graph_texture_serialization.cpp` already turns a
`Graph_texture` into nodes with type names, parameters and links for
glTF; the same walk fills the record, and the same rebuild consumes it.

### 2.3 Reading

The reader walks the composed prim tree (composition is a no-op, X1),
not Tydra's render scene, which never reports shading prims: for each
`NodeGraph` with the marker it reads the children's `info:id`, the
`inputs:` / `outputs:` properties with their connections
(`Attribute::connections()`), and the position attribute, into the
record; the scene conversion stops there, as it does at a `Brush` prim,
so no node becomes a scene prim. A material input whose connection
targets a marked `NodeGraph`'s output is recorded on the material
record as (slot, graph path); Tydra leaves that slot unset, since the
target is no `UsdUVTexture`, and the editor binds the slot to the
rebuilt `Graph_texture` (a `Texture_reference`) the way the glTF
`material_bindings` binder does.

### 2.4 Writing

The writer receives the records, plans a `NodeGraph` prim at each
asset's path (the two-pass plan of U4 2g), writes each node as a generic
`lightusd::Shader` (`info_id` plus a property map), sets a connection
with `Attribute::set_connection(Path{node path, "outputs:<pin>"})`, and
writes the interface output on the `NodeGraph`. The material writer
connects a slot bound to a graph to that output instead of writing a
`UsdUVTexture` for it. Node order in the file is the graph's node order,
so the output is stable across saves (R4).

### 2.5 Editor

`collect_usd_node_graphs` (save) and `resolve_usd_node_graphs` (open and
import, after materials exist) in `src/editor/parsers/usd.cpp`, the E4a
shape: create the `Graph_texture` items at the recorded paths through
the same operation glTF import uses, rebuild the nodes through the
factory, set parameters by D16 text and positions, link pins, bind the
material slots, and leave the graph dirty for the next frame's
`evaluate_if_dirty`. A save no longer logs texture graphs as not carried.

## 3. Phases

Phase 1 holds (`src/erhe/usd/notes.md`, "Texture node graphs"); phase 2
is next. Two facts phase 1 settled that section 2 did not foresee: the
parameter travels as a (USD type, USD literal text) pair chosen by the
editor, since the editor's nodes serialize parameters as JSON rather than
through the property system, and `load_stage` hands Tydra a stage built
from a copy of the composed layer with the graph wiring stripped, because
Tydra fails a whole material over a `UsdPreviewSurface` input whose
connection is no `UsdUVTexture` (the kept layer keeps the wiring, and the
strip goes once the LightUSD fork's Tydra tolerates such an input).

1. `erhe::usd`: the record types, the reader (marked `NodeGraph` prims,
   material slot connections to them), the writer (generic `Shader`
   prims with connections, interface outputs, material slot connection),
   a fixture `texture_graph.usda` with three nodes, a gradient parameter
   and a material bound to the graph, tests for read, write and
   byte-identical double save.
2. Editor: the record to and from `Graph_texture`, material binding,
   save and open paths, MCP `get_scene_node_graphs` reporting nodes and
   links for the round-trip script, the `usd_snapshot` `node_graphs`
   block and a `usd_round_trip_leg` over the fixture, docs
   (`src/erhe/usd/notes.md`, `doc/usd_compatibility.md` rows,
   `doc/scene_serialization.md`).

Verification of the step: the round-trip leg green, and a headless
session that opens the fixture, screenshots the material rendering the
generated texture (a non-uniform surface), saves, reopens and matches
`get_scene_node_graphs` and the material's slot binding, with a
byte-identical second save and a clean scene close.

## 4. Geometry graphs (E4b) reuse this form

A `Graph_mesh` is the same prim form with `info:id` under
`erhe:geometry:` and the evaluated geometry as a child `Mesh "result"`
prim, written the way a brush writes its geometry (E4a), so a viewer
without erhe sees the result; reload rebuilds the graph from the nodes
and re-evaluates, reading the child mesh only when no nodes are present.
Pin value types map to the geometry payload types the mapping gives
them. E4c lands first and E4b adds the vocabulary and the result child.

## 5. Out of scope

- Standard `UsdShade` nodes (`UsdUVTexture`, `UsdPrimvarReader_*`) inside
  an erhe graph, and a MaterialX node vocabulary for erhe's nodes: an
  erhe node has its own semantics, and a mapping to MaterialX definitions
  is the material-fidelity step E2's territory.
- Writing the baked image (R3).
- Evaluating a foreign `NodeGraph` (R5).
