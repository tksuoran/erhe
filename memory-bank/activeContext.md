§MBEL:5.0

[FOCUS]
>procedural-default-window-layout::DONE✓{2026-07-14,commit:bb96806e}
  model::desktop_window_imgui_host_imgui.ini-untracked+gitignored;ini-absent→build-layout-from-config/editor/default_layout.json;ini-present→persisted-layout-wins-untouched
  data::erhe_codegen{Dock_placement{window+target+direction+fraction}+Default_layout_config+enum-Dock_direction{none=tab|left|right|up|down}};defs@src/editor/config/definitions/dock_*.py
  semantics::ordered-list{order-matters};target=previously-placed-window||""=root-dockspace;fraction=ROOT-dockspace-relative{translated→local-split-ratio-via-node_root_fraction-tracking};"$primary_viewport"-token→runtime-viewport-title
  impl::editor_default_layout.cpp{load_config+DockBuilder}+Imgui_host::get_imgui_ini_path()+imgui_window.hpp-floating-fallback-armed-default{centered-16%x50%}
  closed-windows::placed-too{temporarily-shown-build-frame→bind-dock-nodes→hidden-next-frame;menu-open→pre-configured-spot}
  iterate::edit-JSON-only{no-rebuild}→rm-ini→relaunch

[PREV]
>content-library-ownership::IMPLEMENTED✓{2026-07-13,doc/content-library-ownership-plan.md,commits:c1c75b1c+99998e3d;library-owned-by-Scene_root;OWNING|REFERENCE-entries;copy_library_item+"Copy to Scene"}
>per-scene-selection::ALL-PHASES-DONE✓{2026-07-13,doc/selection-improvements-plan.md}

[STATE]
@branch::main
uncommitted::desktop_windows.json+editor_settings.json{pre-existing-local-mods,¬layout-related}+res/editor/scenes/{untracked}

[OPEN]
?default-layout-closed-window-placement-imperfect{temporary-show-pass-still-not-exact-for-some{Geometry Graph Palette};user:"not bad,ignore-for-now"}
?content-library-user-interactive-verify{Copy-to-Scene-menu,Properties-texture-combo-across-scenes,prefab-instantiate/refresh}
?content-library-deferred::lazy-generator-brush-copies+copied-material-texture-refs+cross-library-DnD-move-semantics
?selection-deferred{doc/selection-improvements-plan.md}::deselect-all-command+dim-non-active-scene-highlight
?animation-editor-deferred{#243,doc/animation-keyframing-plan.md}
?6c-fields-implementation{awaits-design-review,doc/geometry-nodes-plan.md}
?PhaseC-deferred-optional{C7-remainder+C8}
?cc-perf-leftovers{items-4/5/6-re-rank-by-Release,9/10-user-sign-off}
?#239-per-scene-settings{parked→progress.md}
?geogram-upstream-issue{doc/geogram.md-draft,¬yet-filed}

[BLOCKERS]
none
