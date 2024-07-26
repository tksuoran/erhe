# msys2 clang64

Read this first:

- [https://www.msys2.org/docs/environments/]
- [https://www.msys2.org/docs/terminals/]
- Always use the *clang64* environment

## Updating all packages

Repeat the following until it reports there is nothing to do:

- `pacman -Suy`

## msys2 clang64 install compiler

To install compiler and debugger, cmake and ninja

- `pacman -S mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-lldb`
- `pacman -S mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja`

## lldb break on ubsan report

- `b __ubsan::ScopedReport::~ScopedReport`
- `b __ubsan::Diag::~Diag`
