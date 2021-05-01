@rem rd /S /Q build

cmake -G "Visual Studio 16 2019" -A x64 -Thost=x64 -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -Wno-dev
