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
- `write_files_via_temp(files)` -- Several `File_write {path, bytes}`, all or none (see
  "Writing several files").
- `rename_file(from, to)` -- One rename replacing `to` (`MoveFileExW` as above on
  Windows, `std::filesystem::rename` elsewhere), logged on failure.
- `Positional_file_reader` -- A read-only handle for positional reads: `read_at(offset, out)` keeps no shared file position, so any number of threads read through one handle at once without a lock (`ReadFile` with an `OVERLAPPED` offset on Windows, `pread` elsewhere). Owners close it as soon as their reads are done: an open handle can keep a save from renaming over the file on Windows. Tests: `test_positional_file_reader.cpp`.
- `to_string(path)` / `from_string(str)` -- Convert between `filesystem::path` and `string`.
- `check_is_existing_non_empty_regular_file(description, path)` -- Validate a file path.
- `ensure_directory_exists(path)` -- Create directory tree if needed.
- `ensure_working_directory_contains(app_name, target)` -- Adjust CWD to locate a required subdirectory.
- `select_file_for_read()` / `select_file_for_write()` -- Native file dialogs returning optional paths.

## Writing several files

`write_files_via_temp(files)` replaces a set of files (a .gltf and its .bin)
all or none. A target that exists and is not a regular file (a directory)
refuses the call up front, logged, with nothing written. A single file is one
rename over its target, as `write_file_via_temp()` does, so the target is never
absent. Two or more go in four stages:

1. Each new file is written to a temporary next to its target, flushed and
   closed. A failure removes every temporary; no target is touched.
2. Each existing target is renamed aside to a temporary name next to it. A
   failure (on Windows, a target another handle holds open without
   `FILE_SHARE_DELETE`) renames the moved-aside targets back and removes the
   temporaries; nothing is replaced.
3. Each new file is renamed onto its target, in the order given (a document
   last, after the files it references). A failure removes the new files
   already in place and the remaining temporaries and renames every old file
   back.
4. The moved-aside old files are removed.

The one window left: a rename back in stage 2 or 3 failing. The target is then
missing, and its old content stays in the moved-aside file, whose name the
error log line gives. `write_files_via_temp(files, rename)` runs every rename
through `rename` (a `Rename_function`), the seam the tests use to fail each
rename in turn.

## Dependencies
- **erhe libraries:** `erhe::defer` (private), `erhe::log` (private), `erhe::verify` (private)
- **External:** Standard library (`<filesystem>`)

## Tests
`erhe_file_tests` (`src/erhe/file/test`): `write_file_via_temp` writes, replaces
a file its writer reads, and keeps the old file byte-identical when the write, the
open or (Windows) the rename fails; `write_files_via_temp` replaces no file when one
of the set cannot be written, and restores every old file when any of its renames
fails (`a_failed_rename_restores_every_old_file`, `a_failed_rename_removes_a_new_target`).

## Notes
- The file dialogs may be platform-specific (uses native OS dialogs).
- All path operations use `std::filesystem::path` for cross-platform compatibility.
