§MBEL:5.0
©erhe::Topic::physics
@scope::Jolt/Box3D physics, jointed-body drags, convex hulls
@docs::doc/erhe/physics.md+doc/editor/physics.md+doc/plans/physics.md
@local::memory-bank/local/physics.md{per-machine,gitignored,if-present}

[STATE]
@newtons-cradle+physics-drag::DONE+USER-VERIFIED+PUSHED-2026-09-17{16-commits-9b7f716cf..6bfd7ca5e;creation-21-scripts/creations/creation_21_newtons_cradle.py{5-hinged-dynamic-balls;--jolt(gap-0.022)|--box3d(gap-0.0005){Jolt-solves-contacts-inside-2cm-speculative-distance-together};--scene-only{no-screenshots/probe/windows};--keep-windows{common.standard_args+add_arguments-hook};hinge=Ball-N-Hinge-under-ball->connected-Ball-N-Pivot-under-frame{world-anchored-joint-with-NO-connected-node-takes-world-frame-from-the-joint-node-itself->dragged-ball-carried-its-pivot};swing-axis-UNLIMITED-since-6bfd7ca5e}
  editor-drag-chain::Transform-tool-drag-of-jointed-dynamic-body=bounded-spring{10Hz,crit-damped,3x-weight,COM-pivot,solver-40/20}+selected-jointed-body-stays-dynamic{873fe258c}|Physics-tool-right-drag-same-for-jointed-bodies{535597229;unjointed=old-rigid-off-center-grab+overrides}|BOTH-project-target-onto-erhe::physics::Joint_reach{8b9317662:point/circle/sphere/box;fixed-anchor-joints-only}+braking-drag-point-per-fixed-step{05ca66ada;v_max=sqrt(2*(max_force/mass-g)*d)}
  !root-cause-bugs-found::Node_joint::on_property_changed-rebuilt-on-EVERY-inherited-Item_base-property{visible/active/name}->rebuild-re-captures-frames+teleport_to_node-zeroes-velocity->visibility-toggle-STOPPED-a-swinging-jointed-body{9aecd84b8:rebuild-only-for-joint_settings+enable_collision}|project_ray-hid+restored-ignore_mesh-every-frame{7fb8411a4:now-steps-over-hits-by-moving-ray-origin}
  regression-check::scripts/physics_drag_joint_sweep.py{878106c4d;16-cases-x-both-tools;rebuilds-cradle-per-case;samples-Hinge-to-Pivot-~17Hz;exit-1-over---threshold-mm;Jolt-16/16-x2,Box3D-16/16;worst-hold-0.23mm}
  mcp-added::drag_selection{Transform-tool-drag-over-N-frames,hold/release}+physics_drag{Physics-tool-drag}=only-headless-way-to-exercise-drags
  !LEFT::unjointed-body-flies-off-after-scripted-drag{drag-point-keeps-kinematic-velocity;pre-existing}|Box3D-Physics-tool-rigid-path-frequency-0=0Hz-joint{does-nothing}|Box3D-six-dof-joints-now-ask-stiffest-softness-GLOBALLY{6de7b258b;60Hz-default-gave-2mm/N-on-a-pendulum}|windowed-mouse-right-drag=USER-VERIFIED,gizmo-mouse-drag-unverified
  (c)User-2026-09-17::git-push-ONLY-on-explicit-instruction{this-push-was-exceptional}
@degenerate-convex-hull::DONE-2026-09-12{4945c0110;classify_affine_span+make_convex_hull-guard+shapes-variant-bool+MCP-error+VERIFY-sites-graceful;plan-section-6-P1-residue-bullet-dropped;geometry-tests-120}

[PROGRESS]
[TASK::degenerate-convex-hull]{DONE-2026-09-12;4945c0110;via-harness;1-coder+1-scout}
✓erhe::math::classify_affine_span{Affine_span:too_few_points|single_point|collinear|coplanar|volumetric;O(n)-no-alloc;epsilon-relative-to-extent}+make_convex_hull-refuses-before-geogram{warn-reason+count}+shapes::make_convex_hull->bool-delegates{BDEL+lock;was-PDEL-void}+MCP-create_shape-isError-reason+3-ERHE_VERIFY-sites-graceful{brush/mesh_operation/move_mesh_vertices:no-hull=no-shape/body}
!measured::BDEL-on-flat-input=warns+returns-NON-hull-true{silent-wrong-answer}|PDEL=never-returns-from-set_vertices{hang,not-getchar}|latent-bug-fixed:double-precision-points-into-single-precision-Geometry-mesh=geo_assert-swallowed->set_double_precision-before-assign
geometry-tests-116->120;headless:coplanar-create_shape=error-editor-alive,tetra-ok;plan-section-6-bullet-dropped,P1-statement-extended

[TASK::newtons-cradle-physics-drag]{DONE+USER-VERIFIED+PUSHED-2026-09-17;via-harness;4-coders}
✓creation-21{8974faec7+fdccecf7d+cabe99c3d+821303b6c+fcd084d98:cradle+backend-gap-flags+--keep-windows+--scene-only+pivots-in-the-frame}
✓drag-through-physics{6de7b258b-physics-springs+Box3D-joint-stiffness|873fe258c-Transform-tool|8b9317662-Joint_reach|7fb8411a4-project_ray|535597229-both-tools-project+Physics-tool-bounded|9aecd84b8-Node_joint-rebuild-scope|05ca66ada-braking-drag-point}
✓instrumentation{621049a60-editor.physics_drag-monitor}->removed-after-verification{41eeff517}+replaced-by-scripts/physics_drag_joint_sweep.py{878106c4d}
✓cradle-hinge-unlimited{6bfd7ca5e:removes-the-last-flake=undragged-ball-hitting-a-hard-limit-on-Jolt}
!measured::hold<=0.23mm-Jolt/0.05mm-Box3D,after-release<=0.37/0.13;reported-drag-704mm->0.01mm;far-ball-peak-unchanged-0.408/0.406
?left::doc/agents/creations.md-entry-21+doc-image-are-in{8974faec7};unjointed-drag-fly-off+Box3D-0Hz-rigid-path-unfixed
