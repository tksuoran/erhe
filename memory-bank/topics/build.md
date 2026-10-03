§MBEL:5.0
©erhe::Topic::build
@scope::CMake conventions and dependency gotchas (all platforms)
@docs::doc/cmake_conventions.md

[CMAKE]
CPM::configure-time-deps
¬file(GLOB)→ExplicitSources!
¬cmake-preset→UseScripts{scripts/}
DepPins¬sacred{bump>patch||fork tksuoran/<dep>}
¬CPM-PATCHES{host-patch-unportable}→ForkRepo{precedent:tksuoran/fastgltf}

[GOTCHA_GEOGRAM]
.cpm_cache/geogram-submodules{OpenNL+amgcl+libMeshb+rply}can-be-empty{only-.git→C1083-nl.h||LNK-nl*}
→fix::git -C <cache> submodule update --init --force <paths>+RE-configure{sources-needed-pre-configure}

[AUDIT_2026_09_30]
googletest::one-CPMAddPackage-root{ERHE_BUILD_TESTS}¬per-test-copies{238830d45}
pins::concurrentqueue+cpp-terminal-archive-URL_HASH{cpp-terminal-52f1768cb6f3=last-before-API-change;net-test-builds-with-it-a83e88a26}
!linux-configure-wrappers-pass-"$@"-BEFORE-fixed-options->cannot-override-them{same-as-Windows-%*};headless=own-wrapper
erhe_profile::PUBLIC-dep-of-erhe_commands{user-approved}
