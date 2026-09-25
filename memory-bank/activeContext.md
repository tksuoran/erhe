§MBEL:5.0

[FOCUS]
@rigging-phase-2::IN-PROGRESS-2026-09-18{doc/plans/rigging/{rigging_tools,fabrik_ik,ik_settings,pole_target}.md;rigging-branch-rebased+ff-into-main{doc-restructure-conflicts-only};via-harness;pole-slice-DONE{484bac408-requirements+67aafe503-solver-ik_apply_pole{rigid-swivel-about-root-effector-axis;unconstrained=once-after-fabrik,constrained=between-forward+backward-pass->limits/locks-win}+c59ba15b3-Ik_settings.pole_target{bridged-weak-Node-ref}+pole_angle+Ik_drag::discover_pole{effector->root-scan-continues-past-non-admissible;pole-world-pos-captured-at-begin}+MCP-ik_drag{one-gesture-one-undo;Ik_drag::make_transform_operation}+08a7a73e0-ERHE_rig.ik.pole_target=glTF-NODE-INDEX{path-form-failed-under-import-root;Gltf_export_arguments::node_extensions_builder}+USD-save-warns-IK-settings-not-written};verify=scripts/ik_pole_verify.py{criteria-1-9+12;each-measured-drag-undone-first:drag-solves-from-CURRENT-pose}+roundtrip-418/421-baseline-3};+ebf606ff2-effector-orientation{Ik_effector_orientation:keep_world|follow_last_segment;captured-at-Ik_drag::begin;Transform_tool_settings-session-state;MCP-ik_drag.effector_orientation;scripts/ik_effector_orientation_verify.py;doc/plans/rigging/ik_drag_options.md}+edaf10d98-chain-visualization{pure-build_ik_drag_lines;Transform_tool::render_ik_drag-x-ray-bucket;Debug_visualizations_style-v2-ik_*};PHASE-2-LEFT=stiffness{after-F7};feel-questions-RESOLVED-2026-09-25-as-Move-tool-OPTIONS{user:'make-both-ways-possible';ik_drag_options.md-section-3;Ik_drag_options;Mid-Chain-Drag:Rigid-Children|Pin-Chain-End(ab2c9edb1,Ik_drag_chain-upper+lower);Solve-From:Drag-Start|Previous-Step(50aa400cb,ik_drag-path-arg);Pole-Alignment:Snap|Ease-In(a9078a35c,pole_weight)};THEN-Phase-3-skeleton-editing;INTERACTIVE-PASS-STARTED-2026-09-18{doc/plans/rigging/interactive_test_pass.md;sections-0-3-PASS-by-hand;sections-0-8-AUTOMATED-2026-09-25{scripts/ik_interactive_pass_verify.py;66-checks;x-ray-contrast+Pole-Target-picker-via-.pick/.clear-roles+8.1-mid-chain+8.2-path-independence+8.3-seeded-stability-sweep};findings:F1-Transform-window-tool-groups-shown-from-startup{FIXED-2026-09-25}+F7-pole+hinge-root-constrained-solve-JUMPS{8.3-FAILS;open}+F2-Set-rest-button-hard-to-find};UI-facts:Move-tool-params-live-in-TRANSFORM-window-below-skew;Set-rest-button=attachment's-own-framed-section-row-Rest;RiggedFigure-arm=arm_joint_L_1/2/3{upper/forearm/hand};prompt_queue.txt=3-items{0=F7-solver-fix{AI};1=stiffness+Phase-3;2=changelog};main-ahead-of-origin-UNPUSHED;?user-interactive-DEFERRED-by-user{ik_settings-slice+pole-picker-row+live-drag};FOUND-unfiled:.gltf-text-export-references-buffer0.bin-never-written{cannot-reopen}}
NEXT=prompt_queue.txt{item-0=F7-fix-ik_solver.cpp-until-8.3-passes;pass=70/71}

[TOPICS]{memory-bank/topics/<name>.md;¬auto-loaded;read-the-ones-matching-the-session;DONE-work+traps+open-items-per-topic}
usd::USD compatibility: LightUSD fork, import/export, composition arcs, DrawModes, USD physics/animation, WG asset survey
property_system::erhe::property dependency properties: node values, styles, folders, migrations, Properties window
rigging::IK / skinning / rigging tools (active task in activeContext [FOCUS])
physics::Jolt/Box3D physics, jointed-body drags, convex hulls
viewport_ui::Viewports, cameras, gizmos, grids, four view, ImGui docking, active item, asset browser, MCP UI driving
performance::Startup profiling (Tracy), deferred brush geometry, frame-time work
tooling::Agent tooling: orchestration harness, doc layout, CI tests, skills location, Metal headless, MCP server policy
scenes_and_assets::Scene persistence (glTF), glTF uids, asset manager, inventory slots
editor::Editor object lifetimes: part construction, scene-close + undo-removal reference rules, retention traps
geometry::Geometry / geogram threading, geometry graph payload and pin rules
build::CMake conventions and dependency gotchas (all platforms)
windows::Windows-only build/clangd/VS-MCP/minidump facts

[STATE]
@branch::main{user-pushes-themselves}
@unpushed::main-ahead-of-origin{many-commits-since-2026-09-18;user-pushes;git-push-only-on-explicit-instruction}

[BLOCKERS]
none
