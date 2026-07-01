§MBEL:5.0

[FOCUS]
@task::#240-selectable-scene+scene-properties✓{builds-on-#239}
©Timo>goal::selectable-Scene-row-in-Hierarchy+Scene-properties-panel{ambient#237+per-scene-overrides}
>done::2026-07-01{2-commits-main:1fd65017-hierarchy+MCP,faacc975-scene-props+ambient}
>verified::headless-MCP{Scene-row+nested-content-library-screenshot;scene-file-v5+ambient-serialized+load-roundtrip;Scene-selected→Properties-shows-Ambient+Scene-Overrides}

[PRIOR]
@task::#239-per-scene-settings{editor-global→override-per-scene}✓{data-layer+consumers+UI}
©Timo>goal::subset-of-editor-settings-overridable-per-scene,stored-in-scene,codegen-based
>plan::APPROVED{2026-07-01}{plans-dir:https-github-com-tksuoran-erhe-issues-23-robust-wolf.md}
>alsoDone::reference-data-captured✓{doc/editor_settings_codegen_scene_reference.md+auto-memory-ptr}

[MECHANISM]
Scene_settings::codegen-struct{scene-unit,src/editor/scene/definitions/scene_settings.py}
override::Optional(StructRef(config-struct))/setting{disengaged=default=use-editor-global}
!enabler::codegen-already-supports-Optional(StructRef)+emits-is_default(){optional-at-default==!has_value()}→¬new-primitive
effective::scene-override-if-engaged||editor_settings->field{resolver-helpers}
storage::Scene_root{¬erhe::scene::Scene-graphics-agnostic}→serialize-new-field-in-Scene_file{v3→v4,old-scenes=all-null=defaults}
cross-unit-ref::add-EXTRA_DEFINITIONS_DIRS"${_config_defs}:config/generated/"→scene-erhe_codegen_generate(~L473)

[PER_SCENE_SET#8]
sky+clear_color+grid+physics+shadow_frustum_fit+post_processing+viewport+camera_controls
Global::render_style_appearance+content_edge_lines+graphics_preset_name+everything-else

[DECISIONS]
©Timo>ref-home::doc/+auto-memory-ptr{¬memory-bank-MBEL-for-big-static-ref}
©Timo>granularity::whole-config-group{¬per-leaf-field}
©Timo>per-scene-set::8{+viewport+camera_controls;¬render-style-appearance;¬content-edge-lines}

[NEXT]
✓PhaseA::reference-data-doc+memory
?PhaseB1::Scene_settings-struct+CMake-wire+Scene_root-member+scene_file-v4+serialize+resolvers+rewire-consumers+minimal-UI
?verify::build-twice→headless-MCP{save/load-roundtrip+effective-value+v3-back-compat+screenshot}
