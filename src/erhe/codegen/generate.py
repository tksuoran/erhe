#!/usr/bin/env python3
"""erhe_codegen generator entry point.

Usage: python generate.py <definitions_dir> <output_dir> [extra_dir1 extra_dir2 ...]

Loads all .py definition files from <definitions_dir>, validates them,
and generates C++ headers/sources into <output_dir>.

Optional extra directories are loaded for reference resolution only --
their structs are registered but NOT generated. This allows StructRef
to reference types defined in other libraries.
"""

import sys
import importlib.util
from pathlib import Path


def load_definitions(definitions_dir: Path) -> None:
    """Load and execute all .py files in the definitions directory."""
    for py_file in sorted(definitions_dir.glob("*.py")):
        spec = importlib.util.spec_from_file_location(py_file.stem, py_file)
        if spec is None or spec.loader is None:
            print(f"Warning: could not load {py_file}", file=sys.stderr)
            continue
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)


def _to_snake_case(name: str) -> str:
    """Convert PascalCase to snake_case."""
    result = []
    for i, c in enumerate(name):
        if c.isupper() and i > 0 and name[i - 1] != '_':
            result.append('_')
        result.append(c.lower())
    return ''.join(result)


def write_if_changed(path: Path, content: str) -> bool:
    """Write file only if content differs. Returns True if written."""
    if path.exists():
        existing = path.read_text(encoding="utf-8")
        if existing == content:
            return False
    path.write_text(content, encoding="utf-8")
    return True


def main() -> None:
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <definitions_dir> <output_dir> [extra_dir1 ...]", file=sys.stderr)
        sys.exit(1)

    definitions_dir = Path(sys.argv[1])
    output_dir = Path(sys.argv[2])
    # Extra args are "dir:include_prefix" pairs for reference-only definitions
    extra_entries: list[tuple[Path, str]] = []
    for arg in sys.argv[3:]:
        if ':' in arg:
            dir_str, prefix = arg.split(':', 1)
            extra_entries.append((Path(dir_str), prefix))
        else:
            extra_entries.append((Path(arg), ""))

    if not definitions_dir.is_dir():
        print(f"Error: definitions directory not found: {definitions_dir}", file=sys.stderr)
        sys.exit(1)

    output_dir.mkdir(parents=True, exist_ok=True)

    # Ensure erhe_codegen package is importable
    codegen_pkg_dir = Path(__file__).parent
    if str(codegen_pkg_dir) not in sys.path:
        sys.path.insert(0, str(codegen_pkg_dir))

    from erhe_codegen.schema import (
        get_struct_registry,
        get_enum_registry,
        validate_references,
    )
    from erhe_codegen.emit_hpp import emit_struct_hpp, emit_struct_serialization_hpp
    from erhe_codegen.emit_cpp import emit_struct_cpp
    from erhe_codegen.emit_enum import emit_enum_hpp, emit_enum_cpp

    # Load extra definition directories first (for reference resolution only).
    # Their structs get an include_prefix so generated #includes use the right path.
    external_struct_names: set[str] = set()
    external_enum_names: set[str] = set()
    for extra_dir, include_prefix in extra_entries:
        if not extra_dir.is_dir():
            continue
        before_structs = set(get_struct_registry().keys())
        before_enums = set(get_enum_registry().keys())
        load_definitions(extra_dir)
        new_structs = set(get_struct_registry().keys()) - before_structs
        new_enums = set(get_enum_registry().keys()) - before_enums
        external_struct_names |= new_structs
        external_enum_names |= new_enums
        if include_prefix:
            for name in new_structs:
                get_struct_registry()[name].include_prefix = include_prefix

    # Load primary definition files
    load_definitions(definitions_dir)

    # Validate cross-references
    validate_references()

    structs = get_struct_registry()
    enums = get_enum_registry()

    print(f"Loaded {len(structs)} struct(s) and {len(enums)} enum(s)")

    files_written = 0

    # Generate struct headers and sources (skip externally-defined structs)
    for name, s in structs.items():
        if name in external_struct_names:
            continue
        snake = _to_snake_case(name)

        hpp_content = emit_struct_hpp(s)
        ser_content = emit_struct_serialization_hpp(s)
        cpp_content = emit_struct_cpp(s)

        if write_if_changed(output_dir / f"{snake}.hpp", hpp_content):
            files_written += 1
            print(f"  wrote {snake}.hpp")

        if write_if_changed(output_dir / f"{snake}_serialization.hpp", ser_content):
            files_written += 1
            print(f"  wrote {snake}_serialization.hpp")

        if write_if_changed(output_dir / f"{snake}.cpp", cpp_content):
            files_written += 1
            print(f"  wrote {snake}.cpp")

    # Generate enum headers and sources (skip externally-defined enums)
    for name, e in enums.items():
        if name in external_enum_names:
            continue
        snake = _to_snake_case(name)

        hpp_content = emit_enum_hpp(e)
        cpp_content = emit_enum_cpp(e)

        if write_if_changed(output_dir / f"{snake}.hpp", hpp_content):
            files_written += 1
            print(f"  wrote {snake}.hpp")

        if write_if_changed(output_dir / f"{snake}.cpp", cpp_content):
            files_written += 1
            print(f"  wrote {snake}.cpp")

    if files_written == 0:
        print("  all files up to date")
    else:
        print(f"  {files_written} file(s) written")


if __name__ == "__main__":
    main()
