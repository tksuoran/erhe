§MBEL:5.0

[TASK::clang-cl+cl.exe-builds-editor-clean]
@status::✓COMPLETE{editor.exe-links-both-compilers,6-commits-on-main}

>DONE:
tracy-pin-master-4cd6c389✓{cc9e5fd5}{>v0.13.1,atomic-copy-init→direct-init-upstream-fix}
mango-clang-cl→MSVC-flag-branch✓{0faceae9}{/EHsc-+-/GR-+-/DWIN32-restored}@clang-cl
Clang.cmake-g3-GNU-frontend-only✓{faa6104e}{Jolt-Werror-no-longer-fatal-clang-cl}@clang-cl
Jolt-ENABLE_ALL_WARNINGS-OFF✓{6c0fea37}{native-Build-no-longer-/Wall-/WX}@clang-cl
Clang.cmake-global-avx2-baseline✓{ce656f05}{shared-erhe_pch+all-TUs-feature-uniform}@clang-cl
tracy-OPTIONS+"TRACY_ENABLE ON"✓{d756c994}{master-flipped-default-ON→OFF,ALL-builds,restores-profiling+fixes-cl.exe-/W4/WX}
verify::editor.exe×2✓{clang-cl-build_ninja_win_clang-68MB,[1487/1490]|cl.exe-build_ninja_win_vulkan-73MB,[613/614];both-0-FAILED}
techContext+memory-bank-updated✓

?PENDING:
optional::VS2026-vulkan/opengl-cl.exe-builds{redundant-with-ninja-vulkan-cl.exe;VS-open→reconfigure-reload-dialog-risk}

[BLOCKERS]
none::both-builds-clean-end-to-end

[NOTES]
!key-learning::running-2nd-compiler(cl.exe)-caught-a-regression-clang-cl-missed{stale-cached-TRACY_ENABLE=ON-hid-it-in-clang-dir}
!fix-6=ALL-builds{tracy-default-change};fixes-1..5=clang-cl-guarded{cl.exe+linux-unaffected}
!tracy-pin-consequences#2{atomic-copy-init-fix-wanted+TRACY_ENABLE-default-flip-unwanted→had-to-re-enable}
