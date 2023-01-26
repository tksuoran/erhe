#!/bin/bash

cp \
    ../../imgui/imgui.cpp \
    ../../imgui/imgui.h \
    ../../imgui/imgui_demo.cpp \
    ../../imgui/imgui_draw.cpp \
    ../../imgui/imgui_internal.h \
    ../../imgui/imgui_tables.cpp \
    ../../imgui/imgui_widgets.cpp \
    ../../imgui/imstb_rectpack.h \
    ../../imgui/imstb_textedit.h \
    ../../imgui/imstb_truetype.h \
    ../../imgui/LICENSE.txt \
    src/imgui/

cp -r ../../imgui/misc/cpp src/imgui/misc/
cp -r ../../imgui/misc/freetype src/imgui/misc/
cp \
    ../../imgui/backends/imgui_impl_glfw.cpp \
    ../../imgui/backends/imgui_impl_glfw.h \
    ../../imgui/backends/imgui_impl_win32.cpp \
    ../../imgui/backends/imgui_impl_win32.h \
    src/imgui/backends/
