# erhe_configuration

## Purpose
Provides INI/TOML configuration file reading with a section-based API. The primary config
file is `erhe.toml`. Supports typed value extraction (bool, int, float, glm vectors, strings)
and caching of parsed files through a singleton `Ini_cache`.

## Key Types
- `Ini_section` -- Abstract interface for reading typed values by key from a config section.
- `Ini_file` -- Abstract interface representing a parsed config file containing named sections.
- `Ini_cache` -- Singleton cache of parsed config files; avoids re-parsing.

## Public API
- `get_ini_file_section(file_name, section_name)` -- One-call access to a config section.
- `get_ini_file(file_name)` -- Get a cached parsed file.
- `parse_toml(table, file_name)` / `write_toml(table, file_name)` -- Direct TOML read/write.
- `Ini_section::get(key, destination)` -- Overloaded for bool, int, float, glm types, string.
- Utility: `split()`, `trim()`, `to_lower()`, `str(bool)`.

## Dependencies
- **erhe libraries:** `erhe::profile` (public), `erhe::file` (private)
- **External:** toml++ (public), glm (public)

## Notes
- Default config file path is the constant `erhe::c_erhe_config_file_path` = `"erhe.toml"`.
- The `Ini_cache` singleton is accessed via `get_instance()`.
