@rem rd /S /Q build

@rem "Visual Studio 16 2019"
cmake -G "Visual Studio 17 2022" -A x64 -Thost=x64 -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -Wno-dev -DERHE_PHYSICS_LIBRARY=bullet
