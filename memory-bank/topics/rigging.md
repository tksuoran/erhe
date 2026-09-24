§MBEL:5.0
©erhe::Topic::rigging
@scope::IK / skinning / rigging tools (active task in activeContext [FOCUS])
@docs::doc/plans/rigging/+doc/editor/transform.md

[STATE]
@ik-as-node-properties::DONE-2026-09-19{4-commits-7f4739a90+82b9db1be+2beea77db+5387c7e30;UNPUSHED;via-harness;(c)User:migrate-away-from-node-attachments->properties-of-the-node-grouped-by-Property_ui::group{doc/plans/node_attachments_to_properties.md=direction+candidates+8-step-recipe;Ik=first-done};erhe::property-Weak_object_reference{Property_type::weak_object=18;entry-stored-weak_ptr;is_object_reference_type/get_referenced_object/make_object_reference-serve-both-kinds}|editor::Ik-registration-holder{src/editor/scene/ik_properties.{hpp,cpp};register_attached-holder-Node;Ik.lock_x..Ik.pole_angle;group-IK;inherits=false{Styles-share-limit-sets};visible_when=bone;Ik.rest_rotation-compute_default=erhe::scene::get_bind_pose_local_rotation-else-identity;read_ik_settings(node)->Ik_settings_data}|resolve_constraint-rest=effective-Ik.rest_rotation-when-any-Ik-lock/limit-on,drag-start-rotation-ONLY-for-channel-lock-only-joint{(c)User-rejected-drifting-limits}|GONE:Ik_settings-class+catalog-entry+Item_type-bit-57+attach_new_ik_settings+ERHE_rig{no-migration,(c)User}|glTF:ERHE_node.property_node_refs{qualified-property-name->glTF-node-index;generic-for-node-held-refs-naming-nodes;path-kept-as-fallback};ROOT-CAUSE-FIXED:by-name-object-ref-resolution-ran-BEFORE-imported-nodes-entered-scene+live-scene-path-vs-elided-import_root|verified:property-140,ik-solver-29,ik_pole_verify-13,effector-5,roundtrip-426/429-baseline-3;?user-interactive{IK-group-on-bone-without-adding-anything;Set-rest-one-undo;F2-now=button-above-groups-apart-from-IK-group};get_addable_item_properties-offers-Ik.*-on-non-bones{same-as-Layout.*}}
@skin-test-asset::DONE+USER-VERIFIED-2026-09-19{13ea538a1-MCP-create_skin{parts=[{node_id,joint_node_id}]->one-rigid-skinned-mesh+Skin;undoable-compound;refuses-bone_proxy/duplicate/already-skinned;src/editor/mcp/mcp_server_skinning.cpp;get_node_details-reports-skin+joints}+e98bfe126-scripts/creations/creation_22_skin_test_boxes.py->res/editor/assets/skin_test/skin_test_3_boxes.glb{3-closed-0.2x1x0.2-boxes-one-primitive;bone_0/1/2-chain-at-y=0/1/2;weight-1-own-bone;26-verts/box};common.py-skin()+export();UNPUSHED;smooth-weight/animation-variants=not-requested}

[PROGRESS]
[TASK::ik-as-node-properties]{DONE-2026-09-19;via-harness;4-coders+1-scout}
✓commit-1{7f4739a90:erhe::property-Weak_object_reference;property-tests-140}
✓commit-2{82b9db1be:Ik.*-attached-properties-on-Node+bind-pose-rest-default+Ik_settings/ERHE_rig-code-removed;ik-solver-tests-29}
✓commit-3{2beea77db:ERHE_node.property_node_refs+by-name-resolution-after-node-inserts;roundtrip-426/429}
✓commit-4{5387c7e30:docs+plans/node_attachments_to_properties.md}
?user-interactive{interactive_test_pass.md-resume-note-at-section-4}
