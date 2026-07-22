§MBEL:5.0
©erhe::TechStack+Setup

[STACK]
Lang::C++20
Build::CMake+CPM
Graphics::Vulkan!default|OpenGL|Metal
Deps#27{geogram+joltphysics+glm+fmt+spdlog+imgui+fastgltf+simdjson+nlohmann_json+cpptrace+tracy+glslang+harfbuzz+freetype+plutosvg+sdl+openxr-sdk+volk+vulkan-headers+VMA+bvh+wuffs+etl+cxxopts+taskflow+httplib+concurrentqueue+fpng}
Python::"py -3"!{¬python/python3→MS-StoreStub-fails}

[BUILD_WIN]
VS2026::{folder-"18"¬"2026";edition+install-path-vary-per-machine→ninja-wrappers-locate-via-vswhere{%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe,-latest -requires VC.Tools.x86.x64}}
Official::scripts\configure_vs2026_vulkan.bat→build_vs2026_vulkan/{VS-solution,open+build-editor}
Ninja{no-VS-solution}:
  configure_ninja_win_clang.bat→build_ninja_win_clang/{clang-cl+CMAKE_EXPORT_COMPILE_COMMANDS+VORPALINE_PLATFORM=Win-vs-generic}
  configure_ninja_win_vulkan.bat→build_ninja_win_vulkan/{MSVC cl}
  build_ninja_win_<clang|vulkan>.bat <target>
  Bundled@VS18{cmake+ninja+VsDevCmd}
!NinjaNeedsMSVC-env::wrappers-call-VsDevCmd-themselves{work-from-any-shell}
clang-cl-build::editor.exe-builds-clean✓{2026-06-29}
  tracy-pin{VERSION-0.13.1→GIT_TAG-master-4cd6c389}::ALL-builds-3-consequences:
    +want::=nullptr→(nullptr){clang-cl-rejects-deleted-atomic-copy-ctor<C++17}
    +must::OPTIONS+"TRACY_ENABLE ON"{master-flipped-default-ON→OFF;else-no-profiling+TracyClient-stops-defining-TRACY_ENABLE→editor.cpp-unused-name-/W4/WX-cl.exe}
    +must::profile.hpp-alias-4-more-gl*{glGetError+glGetIntegerv+glGetString+glGetStringi;master-TracyOpenGL.hpp-probes-GL-context;OpenGL-backend-only;C3861-in-erhe_profile}
  fix#4@clang-cl-guarded{FRONTEND_VARIANT-MSVC→cl.exe+linux-clang-unaffected}:
    mango{if(MSVC)→MSVC-flag-branch¬else-set(CMAKE_CXX_FLAGS)-clobbered-/EHsc}
    Clang.cmake{-g3-GNU-frontend-only,clang-cl-rejects→Jolt-Werror-fatal}
    Jolt{set(ENABLE_ALL_WARNINGS-OFF)@clang-cl::/Wall=-Weverything+/WX→-Wpadded-fatal}
    Clang.cmake{global-avx2-baseline@clang-cl::Jolt-PUBLIC-avx2-vs-shared-erhe_pch-feature-mismatch{clang-PCH-check-symmetric}}
  also::compile_commands@configure{clangd,still-works}
  verified✓::editor.exe-links×8{clang-cl-ninja|cl.exe-ninja-vulkan|VS:vulkan+opengl+vulkan_asan+opengl_asan+headless(null-backend)+vulkan_headless;all-0-error}

[CLANGD_SEMANTIC]
.clangd→CompileFlags::CompilationDatabase::build_ninja_win_clang
!.clangd-gotcha::CompilationDatabase-must-nest-under-CompileFlags{top-level="Unknown Config key"→silent-flag-less-fallback→phantom-errors;doc-fixed-3b70f492}
compile_commands.json::clang-cl-native{~1800-TU;regenerate-via-configure_ninja_win_clang.bat}
clangd::needs-LLVM>=22{winget install LLVM.LLVM;installer-does-NOT-add-"C:\Program Files\LLVM\bin"-to-PATH→add-to-user-PATH}
!clangd-needs-INCLUDE{VS-env}→else-phantom-VS8/9/10-paths→headers-fail{std::mutex-not-found}
proof::clangd--check{noEnv:26errors→VS-env:0-errors;re-proven-2026-07-07{verify.cpp-10→0-after-CompileFlags-fix}}

[MCP_SERVERS]
!policy::lsai+xmp4=opt-in-per-machine¬default{2026-07-07;whether-installed→memory-bank/local/}
default-code-nav::Grep/Glob{erhe-naming-consistent}+VS-MCP{goto_definition/find_references-from-open-docs}+clangd--check{per-file-diagnostics}
third-party-deps::read-.cpm_cache-sources-directly{CPM-fetches-all-dep-sources-at-configure}
doc::doc/semantic_cpp_mcp_setup_xmp4_lsai.md+doc/lsai_usage_playbook.md{kept,machine-neutral,for-machines-that-opt-in}
!MCP-loads-at-session-start→restart-to-pick-up-changes

[RUNTIME]
logs/log.txt::spdlog-file-sink{¬stdout-redirect-empty}
editor-run::repo-root-cwd{config/+res/+logs/}

[GOTCHA_GEOGRAM]
.cpm_cache/geogram-submodules{OpenNL+amgcl+libMeshb+rply}can-be-empty{only-.git→C1083-nl.h||LNK-nl*}
→fix::git -C <cache> submodule update --init --force <paths>+RE-configure{sources-needed-pre-configure}

[VS_MCP]
symbol_workspace::¬cpp-index{C#-oriented,0-matches}→goto_definition/find_references-from-open-documents+Grep{AGENTS.md-VS-MCP-section=canonical-debug/build-flow}
¬attach-tool{debugger_launch-only}→hung-external-process::ASK-user-attach-manually{Debug>Attach}→debugger_break/get_callstack/evaluate-then-work{proven-2026-07-12-geogram-wedge}
get_callstack::current-thread-only→multi-thread-hang::ask-user-switch-thread-in-Threads-window-between-reads

[MINIDUMP_ANALYSIS]{no-cdb/windbg-on-machine}
write-live-dump::ctypes-MiniDumpWriteDump{scratchpad-script,ThreadInfo|DataSegs}
parse::py-minidump-pkg{pip}→exception-record+modules+thread-contexts+stack-bytes
symbolize::llvm-symbolizer{LLVM-bin,--obj=editor.exe,addr=0x140000000+offset;default-output-style¬GNU{GNU=no-blank-line-separators→block-mapping-breaks}}
stack-scan-caveat::conservative{stale-frames+cross-stack-bleed}→hypothesis-grade;live-debugger-stack=diagnosis-grade{proven:scan-showed-geo_abort-frames-misread-as-noise,VS-attach-gave-truth}
