§MBEL:5.0
@WriteOnly{¬read,archive→archive/YYYY-MM-DD.md@EndOfDay}

[2026-06-19]
>built::Ninja-Windows-builds{vulkan-MSVC+clang-cl}✓©vulcan
>fixed::geogram-OpenNL-submodule-empty→submodule-update-force+reconfigure✓
>ran::editor.exe{Vulkan,MainLoop-frames-OK}✓
>installed::lsai-MCP+clangd{winget LLVM}+LLVM-user-PATH-persistent✓
>forked::tksuoran/erhe→LadislavSopko/erhe-PUBLIC✓
>committed::doc+ninja-vulkan-scripts→fork{acfac4bc,pushed}✓
>verified::.mcp.json-gitignored{machine-local,never-committed}✓
>wrote::doc/semantic_cpp_mcp_setup_xmp4_lsai.md{xmp4+lsai-setup-for-tksuoran}✓
>proved::clangd-VS-env-requirement{26→0-errors,INCLUDE}✓
>fieldtest::lsai-on-erhe{outline/info/source✓,project-wide✗-index-warmup}✓
>xmp4::navigated-simdjson-internals{dom_parser_implementation}✓
>diagnosed::lsai-jdtls-fixpoint{CheckStale:63,88832ms→172ms-via-stub,non-converging-fixpoint}©vulcan+lsai✓
>diagnosed::lsai-install-stale-dll-Windows{dotnet-host-lock,kill-PID+reinstall→1.0.180-on-disk}©lsai✓
>shipped::lsai-1.0.179{fixpoint-fix}+1.0.180{installer-hardening}@prod©lsai✓
>insight::lsai-must-parasite-build{compile_commands#0-occurrences}¬scan→issues#28/#29/#30/#31©vulcan+Ladislav+lsai
>replied::tksuoran-issue#10{vs-mcp-roslyn-no-cpp,lsai-fills-gap}✓
>initialized::memory-bank{MBEL5.0,6-core-files}©vulcan

[2026-06-29]
>FIXED::build_ninja_win_clang-editor.exe-builds-clean✓©Timo{5-commits-main}
  cc9e5fd5::tracy-pin-master-4cd6c389{=nullptr→(nullptr),clang-cl-deleted-atomic-copy-ctor<C++17;no-release>0.13.1-yet}
  0faceae9::mango-route-clang-cl→MSVC-flag-branch{else-set(CMAKE_CXX_FLAGS)-clobbered-/EHsc}
  faa6104e::Clang.cmake-g3-GNU-frontend-only{Jolt-Werror-fatal-on-clang-cl-unknown-arg}
  6c0fea37::Jolt-ENABLE_ALL_WARNINGS-OFF@clang-cl{/Wall=-Weverything+/WX→-Wpadded}
  ce656f05::Clang.cmake-global-avx2@clang-cl{Jolt-PUBLIC-avx2-vs-shared-erhe_pch,clang-PCH-feature-check-symmetric}
  d756c994::tracy-OPTIONS+"TRACY_ENABLE ON"{master-flipped-default-ON→OFF,ALL-builds;cl.exe-/W4/WX-on-editor.cpp-unused-name-outside-#if;clang-passed-only-on-stale-cached-ON}
  60d63927::profile.hpp-alias-4-more-gl*-for-TracyOpenGL{glGetError+glGetIntegerv+glGetString+glGetStringi;master-TracyOpenGL.hpp-probes-GL-context;OpenGL-backend-only-C3861}
>verified::editor.exe×4{clang-cl-ninja+cl.exe-ninja-vulkan+VS-vulkan+VS-opengl;all-0-error}✓
>learning::each-extra-build-config-exposed-a-tracy-pin-regression{cl.exe→TRACY_ENABLE-default-flip;OpenGL-backend→TracyOpenGL-new-gl-probes;clang-only-build_ninja_win_clang-masked-both}
>verified-also::VS-asan×2(vulkan+opengl)+headless-null+vulkan_headless✓{all-0-error,needed-no-new-fix}→8-local-build-configs-total-clean
>reviewed::upstream-mango-cmake{no-clang-cl-handling-but-never-clobbers-CMAKE_CXX_FLAGS}+jolt-native-flags{ENABLE_ALL_WARNINGS-knob}
>archived-from-activeContext::SkillKit-delivery-task©vulcan{Deliver-Cpp-Semantic-MCP+SkillKit→tksuoran,issue#10}
  pending-was{commit+push→LadislavSopko/erhe-fork|PR→tksuoran:main|comment-issue#10}{status-unknown,superseded-by-Timo-clang-cl-focus}

[2026-07-02]
✓geometry-nodes::phases1-5+MCP-tools+undo-redo+serialization{branch:geometry_nodes,713eb22d..d812547c,live-verified-headless-MCP,plan-doc-updated,phase6→prompt_queue.txt}
✓#240-selectable-scene::archived{done-2026-07-01,see-git}
✓session-tooling::mcp_call.py+erhe-headless-verify-skill+CLAUDE.md{cli-builds+testing+gotchas}+renderdoc-skill-verify-wording+.mcp.json-recreated
✓geometry-nodes-phase6::6a-incremental+6b-CoW+param-undo+MCP-set_parameter+spawn-grid+output-name{a11abd21..0881e107}
✓geometry-nodes-phase6-completion::output-physics+6d-instances+6e-groups+6c-design+CSG-fix+CC-quadratic-fix+smoke-sweep-65/65{ff414965..a2a36dd5}
✓catmull-clark-performance::harness+items-11-12-1-3-2{0171c8c4..650dc354,editor-x6-25.1→17.8s-Debug,Release-chain-689→570ms,doc/catmull_clark.md}

[2026-07-03]
✓smoke-coverage-extension::65→120-checks+2-real-fixes{cycle-acceptance-b553559b,empty-geometry-output-crash-4491835f,script-bdc71123}
✓doc-audit::52-files-reviewed→16-deleted+~16-refreshed{6106f4d0..5b1cc01c,doc-52→38}
✓editor-improvements-geometry-graph::pin-colors+pipeline-breadcrumbs+geogram-fork-resolution+ASYNC-EVAL-snapshot-isolation{d753e5d5..8f179479,sweep-120/120}

[2026-07-05]
✓PhaseC-graph-editor-dedup::C1..C7→src/editor/graph_editor/{5a211b01..f85e3f56,as-built-doc/graph_editor.md,shader-graph-left-as-is,C7-remainder+C8-deferred}
✓#252-independent-target::5-phases{7d80b0e8..3df49974,doc/252.md,explicit-weak_ptr-targets+pinned-Properties+Editor_windows-multi-instance+OpenEditor/OpenProperties}

[2026-07-07]
✓agent-tooling-setup::local-prereqs{dotnet10-runtime+LLVM-clangd-22}+compile-db{build_ninja_win_clang}+.clangd{CompileFlags-nesting-fix}+clangd--check-0-errors
✓ninja-wrappers-locate-VS-via-vswhere{was-hardcoded-Community-path}+setup-doc-.clangd-fix
✗LSAI+xmp4::now-opt-in-per-machine¬default{installer-reviewed-not-run;default-code-nav=Grep+VS-MCP+clangd--check,deps→.cpm_cache}
✓memory-bank-trim::progress.md-completed-tasks→history{20.7KB→747B}+activeContext/techContext-refresh{stale-branch-claims:geometry_nodes-actually-merged,no-crease-branch;stale-MCP-entries-superseded}

[2026-07-08]
✓machine-scope-rule::README.md{committed-files-machine-neutral:¬usernames/¬hostnames/¬user-paths/¬install-state/¬secrets;capabilities¬inventory;©public-identities-only}+memory-bank/local/{gitignored-per-machine-state}+scrub{activeContext/techContext/history-person-machine-attributions}

[2026-07-09]
>completed::uniform-scale-gizmo-handle{c162eb69}
  dormant-e_handle_scale_xyz→center-cube-mesh+materials+visibility{both-scale-modes}
  Scale_tool::update_uniform{screen-up-right-diagonal,s=2^(drive/gizmo_radius),deferred-baseline}
  +MCP-set_gizmo_visibility{headless-gizmo-activation}+c_str-scale_xz-typo-fix
  verified::headless-screenshots-both-modes✓;?user-drag-feel-verify-pending
>archived::agent-tooling-setup{2026-07-07}✓DONE{from-activeContext}
  local-prereqs{dotnet10+LLVM-clangd-22}✓;compile_commands+.clangd✓
  ninja-wrappers-vswhere-fix✓;clangd--check-0-errors✓
  policy::LSAI+xmp4-opt-in-per-machine;memory-bank-trimmed+Machine-Scope-Rule-added
