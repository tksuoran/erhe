# Bug report draft: glslang emits invalid `DebugGlobalVariable` for compiler-generated `OpConstantComposite` (`gl_WorkGroupSize`)

Target repository: `KhronosGroup/glslang` (and as a cross-reference,
`KhronosGroup/SPIRV-Tools` / `Vulkan-ValidationLayers`, since both
ends agree on the rejection -- the spec text is what `spirv-val`
enforces).

## Relationship to glslang issue #4186

This is a *distinct* bug in the same family as
<https://github.com/KhronosGroup/glslang/issues/4186> ("DebugTypeArray:
Component Count must be OpConstant ..."), which is currently open and
assigned. Both bugs are instances of glslang's
NonSemantic.Shader.DebugInfo.100 emitter putting a SPIR-V result-id
into a debug-info operand slot whose spec wording disallows that id's
opcode kind. The differences:

| | #4186 | this report |
|---|---|---|
| Failing extinst | `DebugTypeArray` | `DebugGlobalVariable` |
| Failing operand | `Component Count` | `Variable` |
| Operand result-id kind | `OpSpecConstantOp` (arithmetic on spec constants) | `OpConstantComposite` (synthesised `gl_WorkGroupSize`) |
| Source-level trigger | `shared T foo[expr-of-spec-constants];` | `layout(local_size_x = 32) in;` (no array, no spec constants) |
| Source-level workaround | sometimes possible (rephrase array size) | none for compute shaders |

Worth filing as a new issue (rather than appending to #4186) so that
the suggested fix (`createConstVariable` -> `DebugInfoNone`) sits next
to its specific call site, while still cross-linking #4186 so the
maintainer (dnovillo) can decide whether to consolidate.

## Summary

When compiling any GLSL compute shader with non-semantic shader
debug info enabled, glslang emits a `DebugGlobalVariable` extended
instruction (NonSemantic.Shader.DebugInfo.100, opcode 18) describing
the implicit `gl_WorkGroupSize` builtin. The Variable operand of that
`DebugGlobalVariable` is the result-id of the `OpConstantComposite`
glslang synthesises to materialise the workgroup-size vector
(`vec3(local_size_x, local_size_y, local_size_z)`). The
NonSemantic.Shader.DebugInfo.100 spec restricts the Variable operand
to one of:

- result-id of `OpVariable`,
- result-id of `OpConstant` (i.e. a *scalar* constant), or
- result-id of `DebugInfoNone`.

`OpConstantComposite` is *none* of those, so `spirv-val` correctly
fails the module with
`VUID-VkShaderModuleCreateInfo-pCode-08737`:

```
NonSemantic.Shader.DebugInfo.100 DebugGlobalVariable: expected operand
Variable must be a result id of OpVariable or OpConstant or DebugInfoNone
  %1262 = OpExtInst %void %1 DebugGlobalVariable %1263 %1219 %35 %uint_220 \
                                         %uint_0 %37 %1263 %1261 %uint_8
```

where `%1261 = OpConstantComposite %v3uint %uint_32 %uint_1 %uint_1`
(in our case, from `layout(local_size_x = 32) in;`).

## Versions

- glslang `16.3.0` (also reproduces on `16.2.0`; both are pinned via CPM
  in our project).
- Vulkan SDK `1.4.328.1` (validation layer `VK_LAYER_KHRONOS_validation`,
  spec_version `0x00404148` = `1.4.328`).
- Adreno 740 driver on Meta Quest 3 (Horizon OS), but the issue is
  CPU-side: `vkCreateShaderModule` fails inside the validation layer
  before the driver ever sees the module.
- Reproduces with the validation layer's standard core checks; no
  best-practices or sync-validation features needed.

## Minimal reproduction

```glsl
#version 460

layout(local_size_x = 32) in;

layout(set = 0, binding = 0) buffer SSBO { uint v[]; } ssbo;

void main()
{
    ssbo.v[gl_GlobalInvocationID.x] = 1u;
}
```

Compile with `glslangValidator` using both:

```
glslangValidator -V \
    --target-env vulkan1.3 \
    -gV \                            # generateDebugInfo
    --enable-non-semantic-debug-info \
    repro.comp -o repro.spv
spirv-val --target-env vulkan1.3 repro.spv
```

`spirv-val` emits the VUID-08737 error above. The shader does not
reference `gl_WorkGroupSize` -- glslang generates the constant and
the matching `DebugGlobalVariable` purely from the `local_size_x`
layout qualifier.

## Expected behaviour

For the implicit `gl_WorkGroupSize` builtin (which is materialised as
`OpConstantComposite`, not as an `OpVariable`), glslang should either:

1. Emit `DebugGlobalVariable` with the Variable operand set to a
   `DebugInfoNone` result-id (which the spec explicitly allows for
   exactly this kind of "no actual variable" case), OR
2. Skip emitting `DebugGlobalVariable` entirely for this synthesised
   constant (it carries no source-level GLSL declaration and so has
   no useful debug-info to attach).

Either option produces SPIR-V that passes `spirv-val` with non-semantic
debug info enabled.

## Actual behaviour

The Variable operand references the `OpConstantComposite` result-id
directly, which violates the spec. Modules fail
`vkCreateShaderModule` whenever validation layers are loaded; without
validation, drivers (Adreno 740 in our case) accept the module silently
because the offending instruction is non-semantic.

## Suspect code site

The emission happens in `SPIRV/SpvBuilder.cpp::Builder::createConstVariable`,
called from `SPIRV/GlslangToSpv.cpp::TGlslangToSpvTraverser::createSpvVariable`
when the symbol is a constant (`node->getQualifier().isConstant()`):

```cpp
// SPIRV/SpvBuilder.cpp, near line 2872 in 16.3.0
void Builder::createConstVariable(Id type, const char* name, Id constant, bool isGlobal)
{
    if (emitNonSemanticShaderDebugInfo) {
        Id debugType = getDebugType(type);
        if (isGlobal) {
            createDebugGlobalVariable(debugType, name, constant);   // <-- 'constant'
                                                                    // is the OpConstantComposite,
                                                                    // not an OpVariable
        }
        else {
            auto debugLocal = createDebugLocalVariable(debugType, name);
            makeDebugValue(debugLocal, constant);
        }
    }
}
```

`createConstVariable` is also reached for other compiler-synthesised
file-scope `const`-qualified symbols (we observed it on user-written
file-scope `const vec3 foo[] = vec3[](...)` arrays as well as on the
implicit `gl_WorkGroupSize` builtin). The user-scope cases are
workable (the user can move the array into a function), but
`gl_WorkGroupSize` is unavoidable for compute shaders.

## Suggested fix

In the `isGlobal` branch of `Builder::createConstVariable`, pass
`makeDebugInfoNone()` as the Variable operand instead of `constant`.
That preserves the `DebugGlobalVariable` entry (so consumers still see
the symbol's name, type, and source location), satisfies the spec,
and matches the wording in the NonSemantic.Shader.DebugInfo.100
spec that lists `DebugInfoNone` as the canonical "no live variable"
sentinel.

A more conservative variant: only substitute `DebugInfoNone` when the
Variable id is in fact an `OpConstantComposite` / `OpSpecConstantComposite`
(leaving genuine scalar `OpConstant` cases alone). The current code is
already structurally aware of the constant kind at the call site, so the
check is local.

## Workarounds we are NOT taking

- Disabling `emitNonSemanticShaderDebugInfo` in `SpvOptions`: hides the
  bug for this case but loses source-line debug info for every shader
  the project ships.
- Stripping the offending `DebugGlobalVariable` extinst calls in a
  post-process step: same end-state as the disable, but with extra
  build-pipeline complexity.

We've moved file-scope `const` arrays into function scope where that is
possible (since glslang then emits `DebugLocalVariable` instead of
`DebugGlobalVariable` and the spec is satisfied), but the
`gl_WorkGroupSize` case has no source-level workaround -- the layout
qualifier is mandatory for compute shaders.

## What we will file

The above as a glslang issue at
<https://github.com/KhronosGroup/glslang/issues>, with this repro and
the suggested fix. We will reference VUID-VkShaderModuleCreateInfo-pCode-08737
and link the NonSemantic.Shader.DebugInfo.100 spec for the Variable
operand constraint.
