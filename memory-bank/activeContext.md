§MBEL:5.0

[FOCUS]
>U1-gltf-2.1-unique-ids{577d9f75,2026-07-16}✓
  plan::asset-manager-plan.md{OUTSIDE-repo,next-to-asset_database.md;sequence:F1→U1→R1..R7→X1..X3;U1-done-before-F1-per-user{fastgltf-momentum}}
  fork-pin::a0600c11→e42e44f2{uid-member-all-child-of-root-types+verbatim-re-export+validate-uid/name-single-namespace;fork-commit-pushed-by-user}
  Item_base::m_gltf_uid{set/get_gltf_uid;assigned-once-never-changed;NOT-copied-by-copy/clone{clone=new-object;dup-uids=invalid-file};gtests×3}
  parse::copy_uid-onto-items{node/mesh/camera/material/image/skin/animation}+Gltf_file_reference/Gltf_external_asset.uid
    !mesh-clone-gotcha::parse_node-attaches-CLONE-of-template-mesh{skin-in-mesh}→first-instantiation-inherits-mesh-uid{m_mesh_uid_claimed};later-instantiations=new-objects
  scan::Gltf_scan::*_uids-vectors{parallel-to-names;lights-extension-hosted-no-uid;gltf_none.hpp-synced}
  export::stamp_uids()-after-combine_buffers{carried-verbatim+validated-vs-whole-identifier-namespace{uids+names;collision→warn+regenerate;writer-ValidateAsset-would-reject}+generate-16-char-alnum-once+store-back{const_cast-sanctioned-mutation}→re-save-never-changes-uids}
    ¬stamped::non-item-backed{accessors/buffers/bufferViews/samplers/textures/scene/extra-brush-meshes/synthesized-collider-nodes}→no-churn
    version-stays-2.0{erhe-sets-assetInfo;Blender-leg-safe}
  MCP::scan_gltf-tool{names+uids+extensions+errors-per-category}
  /bigobj::gltf_fastgltf.cpp-C1128{erhe_gltf-CMakeLists,MSVC}
  verify✓::export×2-identical+import→re-export-preserves+save→load_scene→re-save-preserves+save×2-identical+scan-matches+validator-0-errors{32-warnings=UNEXPECTED_PROPERTY-uid}+roundtrip-harness-62/62{Blender-render✓}+item-tests-28/28

[PREV]
>scene-close-bug-class-session::6-commits{2026-07-15-eve;details:archive+progress}
>graph-editor-dragdrop+inventory{2026-07-15-pm}✓

[STATE]
@branch::main
untracked::res/editor/scenes/{user-saved¬touch}+prompt_queue.txt{handoff:F1}
uncommitted-held::doc/gltf_extensions/ERHE_asset_reference.{md,schema.json}+README-row{DRAFT-wire-spec;ask-user-before-committing}

[HANDOFF]
prompt_queue.txt::1-item
  ITEM1::F1-scene-close-fixes{animation-player/window+brush/material-paint-tools+write-only-caches+watchdog-slot-whitelist+2-verify-and-fix;clipboard=OPEN-DECISION-ask-user-first;plan-Phase-F1-section=details}

[OPEN]
?user-verify-INTERACTIVE::
  graph-mesh-hover/pick{c18b2608}+close-scene-graph-window{dd9022bc}+dragdrop-list{2026-07-15-pm}
?content-library-user-interactive-verify{Copy-to-Scene,texture-combo,prefab}
?content-library-deferred+selection-deferred+animation-editor{#243}+6c-fields+PhaseC+cc-perf-leftovers+#239-per-scene-settings{parked}+geogram-upstream-issue{unfiled}

[BLOCKERS]
none
