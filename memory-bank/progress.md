§MBEL:5.0

[TASK::SemanticCpp-MCP-Integration]
@status::core-done+fixpoint-validation-pending

>DONE:
build-MSVC-Ninja::editor.exe✓{build_ninja_win_vulkan,ran-MainLoop-frames}
build-clang-cl::configure✓+compile_commands#1731✓{link-fails-mango/EHsc+tracy-atomic,¬needed-for-lsai}
geogram-OpenNL-submodule-fix✓{submodule-update-force+reconfigure}
lsai-install::v1.0.180✓+clangd-Ready✓+LLVM-user-PATH-persistent✓
xmp4::HTTP-configured✓{simdjson+nlohmann/json-navigable}
fork::LadislavSopko/erhe-PUBLIC✓{pushed:doc+ninja-vulkan-scripts+.mcp.json@acfac4bc}
.mcp.json::untracked-local{secret-API-key,never-committed-with-secret}✓
doc::semantic_cpp_mcp_setup_xmp4_lsai.md✓{¬committed-yet}
lsai-fieldtest::outline+info+source✓|search/usages/hierarchy✗{index-warmup}
lsai-diagnostics→fixes::1.0.179{fixpoint}+1.0.180{installer}@prod✓
issue#10-reply::tksuoran{lsai-explains-cpp-gap}✓

?PENDING:
FixpointValidation{wipe-mjsf→1open-genera-once→2open-0regen}←needs-lsai-#31-async{else-88s-hang}
commit-clang-scripts+gitignore→fork{configure/build_ninja_win_clang.bat}
link-doc→issue#10
lsai-#30(parasite-DB)+#31(async-open)→re-validate-when-land

[BLOCKERS]
!lsai-open-blocks-88s{Ensure-sync}→validate-via-restart-logs{¬hang}OR-wait-#31
mjsf.json::wiped{will-regenerate-background@next-restart,1.0.180-no-loop}
