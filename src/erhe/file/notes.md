# erhe_file

## Purpose
Filesystem utility library providing file read/write, path conversion, file existence
checks, directory creation, and native file open/save dialogs.

## Key Types
No classes; this library exposes free functions in `erhe::file`.

## Public API
- `read(description, path)` -- Read entire file to `optional<string>`; returns empty if file missing/empty.
- `write_file(path, text)` -- Write string to file, returns success bool.
- `to_string(path)` / `from_string(str)` -- Convert between `filesystem::path` and `string`.
- `check_is_existing_non_empty_regular_file(description, path)` -- Validate a file path.
- `ensure_directory_exists(path)` -- Create directory tree if needed.
- `ensure_working_directory_contains(app_name, target)` -- Adjust CWD to locate a required subdirectory.
- `select_file_for_read()` / `select_file_for_write()` -- Native file dialogs returning optional paths.

## Dependencies
- **erhe libraries:** `erhe::defer` (private), `erhe::log` (private), `erhe::verify` (private)
- **External:** Standard library (`<filesystem>`)

## Notes
- The file dialogs may be platform-specific (uses native OS dialogs).
- All path operations use `std::filesystem::path` for cross-platform compatibility.
