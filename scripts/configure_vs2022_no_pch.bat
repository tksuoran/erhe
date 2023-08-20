@rem rd /S /Q build
@rem -D CMAKE_GENERATOR_INSTANCE="C:/Program Files/Microsoft Visual Studio/2022/Community" ^
@rem -A x64 ^
@rem -Thost=x64 ^

cmake ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -B build ^
    -S . ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1 ^
    -Wno-dev ^
    -DERHE_USE_PRECOMPILED_HEADERS=OFF ^
    -DERHE_FONT_RASTERIZATION_LIBRARY=freetype ^
    -DERHE_GLTF_LIBRARY=cgltf ^
    -DERHE_GUI_LIBRARY=imgui ^
    -DERHE_PHYSICS_LIBRARY=jolt ^
    -DERHE_PNG_LIBRARY=mango ^
    -DERHE_PROFILE_LIBRARY=tracy ^
    -DERHE_RAYTRACE_LIBRARY=bvh ^
    -DERHE_SVG_LIBRARY=lunasvg ^
    -DERHE_TEXT_LAYOUT_LIBRARY=harfbuzz ^
    -DERHE_WINDOW_LIBRARY=glfw ^
    -DERHE_XR_LIBRARY=openxr

@rem    -DCMAKE_EXE_LINKER_FLAGS_DEBUG="/debug /incremental:no" ^
@rem    -DCMAKE_EXE_LINKER_FLAGS_MINSIZEREL="/debug /incremental:no" ^
@rem    -DCMAKE_EXE_LINKER_FLAGS_RELEASE="/debug /incremental:no" ^
@rem    -DCMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO="/debug /incremental:no" ^
@rem    -DCMAKE_STATIC_LINKER_FLAGS_DEBUG="/debug /incremental:no" ^
@rem    -DCMAKE_STATIC_LINKER_FLAGS_MINSIZEREL="/debug /incremental:no" ^
@rem    -DCMAKE_STATIC_LINKER_FLAGS_RELEASE="/debug /incremental:no" ^
@rem    -DCMAKE_STATIC_LINKER_FLAGS_RELWITHDEBINFO="/debug /incremental:no" ^
@rem    -DCMAKE_SHARED_LINKER_FLAGS_DEBUG="/debug /incremental:no" ^
@rem    -DCMAKE_SHARED_LINKER_FLAGS_MINSIZEREL="/debug /incremental:no" ^
@rem    -DCMAKE_SHARED_LINKER_FLAGS_RELEASE="/debug /incremental:no" ^
@rem    -DCMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO="/debug /incremental:no" ^
@rem    -DCMAKE_MODULE_LINKER_FLAGS_DEBUG="/debug /incremental:no" ^
@rem    -DCMAKE_MODULE_LINKER_FLAGS_MINSIZEREL="/debug /incremental:no" ^
@rem    -DCMAKE_MODULE_LINKER_FLAGS_RELEASE="/debug /incremental:no" ^
@rem    -DCMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO="/debug /incremental:no"
