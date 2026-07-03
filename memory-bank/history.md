§MBEL:5.0
@WriteOnly{¬read,archive→archive/YYYY-MM-DD.md@EndOfDay}

[2026-06-19]
>built::Ninja-Windows-builds{vulkan-MSVC+clang-cl}✓©vulcan
>fixed::geogram-OpenNL-submodule-empty→submodule-update-force+reconfigure✓
>ran::editor.exe{Vulkan,MainLoop-frames-OK}✓
>installed::lsai-MCP+clangd{winget LLVM}+LLVM-user-PATH-persistent✓
>forked::tksuoran/erhe→LadislavSopko/erhe-PUBLIC✓
>committed::doc+ninja-vulkan-scripts→fork{acfac4bc,pushed}✓
>untracked::.mcp.json{secret-CLAUDE_CHAT_API_KEY,gitignored,never-committed-with-secret}✓
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
