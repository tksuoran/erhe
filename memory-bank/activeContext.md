§MBEL:5.0

[FOCUS]
@task::build_ninja_win_clang-make-editor-build-clean✓DONE{2026-06-29}
©Timo>goal::test-clang-cl-build→fix-every-error→editor.exe-links
context::clang-cl=MSVC-ABI-clang-driver{stricter-than-cl.exe}→surfaced-5-latent-build-issues-cl.exe-tolerated

[RECENT]
>RESOLVED::5-root-cause-fixes✓{each-own-commit-on-main,all-guarded-clang-cl-only}
  cc9e5fd5::tracy-pin{0.13.1→master-4cd6c389}{=nullptr→(nullptr)-direct-init,clang-cl-rejects-deleted-atomic-copy-ctor<C++17}
  0faceae9::mango-route-clang-cl→MSVC-flag-branch{else-branch-set(CMAKE_CXX_FLAGS"-Wall")-clobbered-default-/EHsc}
  faa6104e::Clang.cmake-no-g3@clang-cl{GNU-frontend-only,Jolt-Werror-made-unknown-arg-fatal}
  6c0fea37::Jolt-set(ENABLE_ALL_WARNINGS-OFF)@clang-cl{/Wall=-Weverything+/WX→-Wpadded-on-Jolt-structs-fatal}
  ce656f05::Clang.cmake-global-avx2-baseline@clang-cl{Jolt-PUBLIC-avx2-vs-shared-erhe_pch-feature-mismatch}
>verified::editor.exe-links✓{68MB,[1487/1490],0-FAILED,0-pch-mismatch}
>method::iterative×5{rebuild→read-1st-FAILED→isolate-layer→root-cause-fix→guard-clang-cl→rebuild}

[NEXT]
?optional::verify-VS2026-vulkan-cl.exe-build{tracy-pin=only-cross-cutting-change;clang-cl-stricter-already-compiled-tracy-master→low-risk}

[DECISIONS_RESOLVED]
©Timo>PCH::keep{¬disable-for-clang-cl}→global-avx2-baseline{NOT-just-erhe_pch:clang-PCH-feature-check-symmetric,331-jolt-TUs-avx2-vs-258-non-jolt-TUs→must-be-uniform}
©Timo>mango-fix-in-mango-specific-cmake-fine{¬erhe-root-cmake}
©Timo>all-fixes-guarded-clang-cl-specific{cl.exe+linux-clang-keep-prior-behavior}
©upstream-checks{tracy-master-has-(nullptr)-fix;mango-upstream-unchanged-but-never-clobbers-CMAKE_CXX_FLAGS;jolt-ENABLE_ALL_WARNINGS-is-the-knob}
