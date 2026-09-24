§MBEL:5.0
©erhe::TechStack+Setup

[STACK]
Lang::C++20
Build::CMake+CPM
Graphics::Vulkan!default|OpenGL|Metal
Deps#27{geogram+joltphysics+glm+fmt+spdlog+imgui+fastgltf+simdjson+nlohmann_json+cpptrace+tracy+glslang+harfbuzz+freetype+plutosvg+sdl+openxr-sdk+volk+vulkan-headers+VMA+bvh+wuffs+etl+cxxopts+taskflow+httplib+concurrentqueue+fpng}
Python::"py -3"!{¬python/python3→MS-StoreStub-fails}

[RUNTIME]
logs/log.txt::spdlog-file-sink{¬stdout-redirect-empty}
editor-run::repo-root-cwd{config/+res/+logs/}
