§MBEL:5.0

[FOCUS]
@task::SemanticCpp-MCP-Integration{lsai+xmp4}→erhe-Cpp-navigation!
©Ladislav>requested::give-erhe-Cpp-semantic-MCP{VS-MCP-roslyn-cant-do-cpp}
identity::vulcan{chat-agent-name}

[RECENT]
>built::Ninja-clang-cl-build✓{compile_commands#1731-native-clang}
>installed::lsai+clangd✓{winget LLVM,PATH-persistent,v1.0.180}
>proved::clangd-needs-VS-env→26errors→0{INCLUDE-resolves-headers}✓
>forked::tksuoran/erhe→LadislavSopko/erhe{PUBLIC}✓
>wrote::doc/semantic_cpp_mcp_setup_xmp4_lsai.md✓
>fieldtest::lsai-outline+info+source✓{file-scoped-immediate}|search/usages/hierarchy✗{clangd-index-warmup}
>xmp4::simdjson+nlohmann/json-navigable✓{third-party-internals}

[LSAI_COLLAB]
↔lsai-agent{room:lsai}+Ladislav{chat-hub:cc-chat.0ics.ai}
RootInsight::lsai-analyzer-IGNORES-build{compile_commands#0-occurrences-in-codebase}→re-derives-88s-FS-scan©vulcan+Ladislav+lsai
!FixDirection::parasite-build-output{compile_commands C/C++|sln+csproj C#|cargo Rust|go-list Go|gradle/maven Java}¬scan¬build-self
C#::WorkspaceNeedsRestore::correct-parasite{model-only-post-restore}¬bug
C/C++::scan::THE-bug{model-exists-in-DB,ignored}+WorkspaceUnusable{g++-only-flags→0symbols→DB-fixes}

[BLOCKERS]
!FixpointValidation::pending→validate-via-restart-logs{¬tool-hang}
lsai-workspace_open-explicit::BLOCKS-88s{Ensure-sync-pre-cts}≠auto-open{background}
async-open-MUST-pair-progress{Generating-x/N→Indexing-y/N→Ready}©Ladislav{else-blind}

[ISSUES_LSAI]
#29+#30::parasite-compile_commands{root-fix,TDD}!priority1
#28+#31::non-blocking-open+phased-status{folds#23,TDD}priority2
#22::redundant-CI/CD-deploy{¬blocking}
#24+#25::installer-version-pin+stale-dll-Windows{closed@1.0.180}✓
#27::installer-respawn-race{exit255,fail-safe,tracked}

[DECISIONS_PENDING]
?commit-clang-scripts{configure/build_ninja_win_clang.bat}+gitignore→fork
?link-doc→issue#10{tksuoran}
?lsai-#30/#31-timing{Ladislav-call,proposed-tomorrow-TDD}
