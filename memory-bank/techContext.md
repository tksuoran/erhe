§MBEL:5.0
©erhe::TechStack+Setup

[STACK]
Lang::C++20
Build::CMake+CPM
Graphics::Vulkan!default|OpenGL|Metal
Deps#27{geogram+joltphysics+glm+fmt+spdlog+imgui+fastgltf+simdjson+nlohmann_json+cpptrace+tracy+glslang+harfbuzz+freetype+plutosvg+sdl+openxr-sdk+volk+vulkan-headers+VMA+bvh+wuffs+etl+cxxopts+taskflow+httplib+concurrentqueue+fpng}
Python::"py -3"!{¬python/python3→MS-StoreStub-fails}

[BUILD_WIN]
VS2026::"C:\Program Files\Microsoft Visual Studio\18\Community"{folder18¬2026}
Official::scripts\configure_vs2026_vulkan.bat→build_vs2026_vulkan/{VS-solution,open+build-editor}
@NEW>Ninja{no-VS-solution}©vulcan:
  configure_ninja_win_clang.bat→build_ninja_win_clang/{clang-cl+CMAKE_EXPORT_COMPILE_COMMANDS+VORPALINE_PLATFORM=Win-vs-generic}
  configure_ninja_win_vulkan.bat→build_ninja_win_vulkan/{MSVC cl}
  build_ninja_win_<clang|vulkan>.bat <target>
  Bundled@VS18{cmake+ninja+VsDevCmd}
!NinjaNeedsMSVC-env::launch-from-x64-NativeTools-prompt{cl+INCLUDE}
clang-cl-build::editor.exe-builds-clean✓{2026-06-29}>was::editor-link-fails
  tracy-pin{VERSION-0.13.1→GIT_TAG-master-4cd6c389}::ALL-builds-3-consequences:
    +want::=nullptr→(nullptr){clang-cl-rejects-deleted-atomic-copy-ctor<C++17}
    +must::OPTIONS+"TRACY_ENABLE ON"{master-flipped-default-ON→OFF;else-no-profiling+TracyClient-stops-defining-TRACY_ENABLE→editor.cpp-unused-name-/W4/WX-cl.exe}
    +must::profile.hpp-alias-4-more-gl*{glGetError+glGetIntegerv+glGetString+glGetStringi;master-TracyOpenGL.hpp-probes-GL-context;OpenGL-backend-only;C3861-in-erhe_profile}
  fix#4@clang-cl-guarded{FRONTEND_VARIANT-MSVC→cl.exe+linux-clang-unaffected}:
    mango{if(MSVC)→MSVC-flag-branch¬else-set(CMAKE_CXX_FLAGS)-clobbered-/EHsc}
    Clang.cmake{-g3-GNU-frontend-only,clang-cl-rejects→Jolt-Werror-fatal}
    Jolt{set(ENABLE_ALL_WARNINGS-OFF)@clang-cl::/Wall=-Weverything+/WX→-Wpadded-fatal}
    Clang.cmake{global-avx2-baseline@clang-cl::Jolt-PUBLIC-avx2-vs-shared-erhe_pch-feature-mismatch{clang-PCH-check-symmetric}}
  also::compile_commands@configure{lsai/clangd,still-works}
  verified✓::editor.exe-links×4{clang-cl:build_ninja_win_clang|cl.exe:build_ninja_win_vulkan|VS:build_vs2026_vulkan|VS:build_vs2026_opengl}{running-2nd-compiler+OpenGL-backend-each-exposed-a-tracy-pin-regression}

[CLANGD_SEMANTIC]
.clangd→CompilationDatabase::build_ninja_win_clang
compile_commands.json::clang-cl-native#1731
clang-cl22+clangd22::sameLLVM{"C:\Program Files\LLVM\bin"@user-PATH-persistent}
!clangd-needs-INCLUDE{VS-env}→else-phantom-VS8/9/10-paths→headers-fail{std::mutex-not-found}
proof::clangd--check{noEnv:26errors→VS-env:0-real-errors}

[MCP_SERVERS]
lsai::stdio{C:/Users/ladis/.lsai/run.cmd --stdio}{clangd-broker,local-cpp,@v1.0.180}
xmp4::http{https://mcp.example4.ai/mcp}{SCIP,third-party-libs,no-auth/no-key,900+OSS}
claude-chat::stdio{bunx cc-chat-mcp,NPM_CONFIG_REGISTRY=nexus.0ics.ai,room:lsai,name:vulcan}
config::.mcp.json{project,¬commit-secret}||~/.claude/mcp.json{user,xmp4+lsai}
!MCP-loads-at-session-start→restart-to-pick-up-changes

[RUNTIME]
logs/log.txt::spdlog-file-sink{¬stdout-redirect-empty}
editor-run::repo-root-cwd{config/+res/+logs/}

[GOTCHA_GEOGRAM]
.cpm_cache/geogram-submodules{OpenNL+amgcl+libMeshb+rply}can-be-empty{only-.git→C1083-nl.h||LNK-nl*}
→fix::git -C <cache> submodule update --init --force <paths>+RE-configure{sources-needed-pre-configure}

[VS_MCP]
visualstudio-MCP::roslyn-based→¬cpp-support{WorkspaceNeedsRestore/no-solution}→use-lsai-for-cpp
