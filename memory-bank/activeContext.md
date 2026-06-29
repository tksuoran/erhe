§MBEL:5.0

[FOCUS]
@task::clang-cl+cl.exe-builds-editor-clean✓DONE{2026-06-29}
©Timo>goal::test-clang-cl-build→fix-every-error→editor.exe-links→then-verify-other-local-builds
context::clang-cl=MSVC-ABI-clang-driver{stricter-than-cl.exe}→surfaced-latent-build-issues;cl.exe-run-then-caught-a-tracy-pin-regression-clang-missed

[RECENT]
>RESOLVED::6-root-cause-fixes✓{each-own-commit-on-main}
  cc9e5fd5::tracy-pin{0.13.1→master-4cd6c389}{=nullptr→(nullptr)-direct-init,clang-cl-rejects-deleted-atomic-copy-ctor<C++17}
  0faceae9::mango-route-clang-cl→MSVC-flag-branch{else-set(CMAKE_CXX_FLAGS"-Wall")-clobbered-default-/EHsc}@clang-cl
  faa6104e::Clang.cmake-no-g3{GNU-frontend-only,clang-cl-rejects→Jolt-Werror-fatal}@clang-cl
  6c0fea37::Jolt-set(ENABLE_ALL_WARNINGS-OFF){/Wall=-Weverything+/WX→-Wpadded-fatal}@clang-cl
  ce656f05::Clang.cmake-global-avx2-baseline{Jolt-PUBLIC-avx2-vs-shared-erhe_pch-feature-mismatch}@clang-cl
  d756c994::tracy-OPTIONS+"TRACY_ENABLE ON"{master-flipped-default-ON→OFF,ALL-builds}{silently-disabled-profiling;cl.exe-/W4/WX-on-editor.cpp-unused-name-outside-#if-TRACY_ENABLE}
>guards::fixes-1..5-clang-cl-only{FRONTEND_VARIANT-MSVC}|fix-6-all-builds{tracy-default-changed}
>verified::editor.exe-links✓×2{clang-cl:build_ninja_win_clang-68MB|cl.exe:build_ninja_win_vulkan-73MB,both-0-FAILED}
>method::iterative{rebuild→read-1st-FAILED→isolate-layer→root-cause-fix→rebuild}
>caught::clang-build-passed-on-STALE-cached-TRACY_ENABLE=ON{cl.exe-fresh-configure-exposed-real-regression}→value-of-running-2nd-compiler

[NEXT]
?optional::VS2026-vulkan/opengl-cl.exe-builds{same-CMakeLists,ninja-vulkan-cl.exe-already-proved-tracy-pin+TRACY_ENABLE-fix;VS-open-PID9088→reconfigure-would-pop-reload-dialog}

[DECISIONS_RESOLVED]
©Timo>PCH::keep{¬disable-clang-cl}→global-avx2-baseline{clang-PCH-feature-check-symmetric,331-jolt-avx2-TUs-vs-258-non-jolt→must-be-uniform}
©Timo>mango-fix-in-mango-specific-cmake-fine{¬erhe-root}
©Timo>fixes-1..5-clang-cl-guarded
©Timo>run-local-builds-to-verify{cl.exe-run-caught-tracy-TRACY_ENABLE-regression}
