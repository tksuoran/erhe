§MBEL:5.0

[FOCUS]
@task::clang-cl+cl.exe+VS-solution-builds-editor-clean✓DONE{2026-06-29}
©Timo>goal::test-clang-cl-build→fix-every-error→verify-all-local-build-configs
context::clang-cl-strictness+running-other-configs(cl.exe,OpenGL-backend)-each-exposed-a-different-tracy-pin-regression

[RECENT]
>RESOLVED::7-root-cause-fixes✓{each-own-commit-on-main}
  cc9e5fd5::tracy-pin{0.13.1→master-4cd6c389}{=nullptr→(nullptr),clang-cl-rejects-deleted-atomic-copy-ctor<C++17}
  0faceae9::mango-route-clang-cl→MSVC-flag-branch{else-clobbered-/EHsc}@clang-cl
  faa6104e::Clang.cmake-no-g3{GNU-frontend-only,Jolt-Werror-fatal}@clang-cl
  6c0fea37::Jolt-ENABLE_ALL_WARNINGS-OFF{/Wall=-Weverything+/WX→-Wpadded}@clang-cl
  ce656f05::Clang.cmake-global-avx2-baseline{Jolt-PUBLIC-avx2-vs-shared-erhe_pch}@clang-cl
  d756c994::tracy-OPTIONS+"TRACY_ENABLE ON"{master-flipped-default-ON→OFF,ALL-builds;cl.exe-/W4/WX-editor.cpp-unused-name-outside-#if}
  60d63927::profile.hpp-alias-4-more-gl*-for-TracyOpenGL{glGetError/Integerv/String/Stringi;master-probes-GL-context;OpenGL-backend-only,C3861-erhe_profile}
>guards::fixes-1..5=clang-cl-only{FRONTEND_VARIANT-MSVC}|fix-6=all-builds{tracy-default}|fix-7=all-OpenGL-builds{tracy-OpenGL-probe}
>verified::editor.exe-links✓×8{clang-cl-ninja|cl.exe-ninja-vulkan|VS:vulkan+opengl+vulkan_asan(112MB)+opengl_asan(124MB)+headless-null(57MB)+vulkan_headless(66MB);all-0-error}{asan+headless-needed-no-new-fix,share-CMakeLists}
>insight::tracy-pin-had-3-consequences{1-wanted-atomic-fix+2-unwanted-TRACY_ENABLE-default-flip+TracyOpenGL-new-gl-probes};each-surfaced-by-a-different-build-config

[NEXT]
>DONE::all-8-local-build-configs-verified✓{none-pending}

[DECISIONS_RESOLVED]
©Timo>PCH::keep{¬disable-clang-cl}→global-avx2-baseline{clang-PCH-feature-check-symmetric}
©Timo>mango-fix-in-mango-specific-cmake-fine{¬erhe-root}
©Timo>fixes-1..5-clang-cl-guarded
©Timo>run-all-local-builds-to-verify{cl.exe+OpenGL-each-caught-a-distinct-tracy-regression-clang-only-build-masked}
