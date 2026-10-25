# erhe_file

Stability: stable

## Purpose
Filesystem utility library providing file read/write, path conversion, file existence
checks, directory creation, and native file open/save dialogs.

## Key Types
No classes; this library exposes free functions in `erhe::file`.

## Public API
- `read(description, path)` -- Read entire file to `optional<string>`; returns empty if file missing/empty.
- `write_file(path, text)` -- Write string to file, returns success bool.
- `write_file_via_temp(path, text)` / `write_file_via_temp(path, write)` -- Write
  through a temporary file in the target's directory (`<file name>.<pid>.<n>.tmp`;
  `write` is a `bool(FILE*)` callback that fills it), flushed and closed, then
  moved over `path` in one rename (`MoveFileExW` with `MOVEFILE_REPLACE_EXISTING |
  MOVEFILE_WRITE_THROUGH` on Windows, `std::filesystem::rename` elsewhere). The old
  file stays intact and readable until the rename, so a writer can read the file it
  replaces; on any failure (callback returns false, open / write / flush / close /
  rename fails) the temporary file is removed, the old file is unchanged and the
  call returns false with the failure logged. On Windows the rename fails while
  another handle holds the target open without `FILE_SHARE_DELETE`.
- `Positional_file_reader` -- A read-only handle for positional reads: `read_at(offset, out)` keeps no shared file position, so any number of threads read through one handle at once without a lock (`ReadFile` with an `OVERLAPPED` offset on Windows, `pread` elsewhere). Owners close it as soon as their reads are done: an open handle can keep a save from renaming over the file on Windows. Tests: `test_positional_file_reader.cpp`.
- `to_string(path)` / `from_string(str)` -- Convert between `filesystem::path` and `string`.
- `check_is_existing_non_empty_regular_file(description, path)` -- Validate a file path.
- `ensure_directory_exists(path)` -- Create directory tree if needed.
- `ensure_working_directory_contains(app_name, target)` -- Adjust CWD to locate a required subdirectory.
- `select_file_for_read()` / `select_file_for_write()` -- Native file dialogs returning optional paths.

## Dependencies
- **erhe libraries:** `erhe::defer` (private), `erhe::log` (private), `erhe::verify` (private)
- **External:** Standard library (`<filesystem>`)

## Tests
`erhe_file_tests` (`src/erhe/file/test`): `write_file_via_temp` writes, replaces
a file its writer reads, and keeps the old file byte-identical when the write, the
open or (Windows) the rename fails.

## Notes
- The file dialogs may be platform-specific (uses native OS dialogs).
- All path operations use `std::filesystem::path` for cross-platform compatibility.
