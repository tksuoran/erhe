#!/usr/bin/env python3
"""erhe_codegen generator entry point.

Usage: python generate.py <definitions_dir> <output_dir>

Loads all .py definition files from <definitions_dir>, validates them,
and generates C++ headers/sources into <output_dir>.
"""

import sys
import os
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


def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <definitions_dir> <output_dir>", file=sys.stderr)
        sys.exit(1)

    definitions_dir = Path(sys.argv[1])
    output_dir = Path(sys.argv[2])

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

    # Load all definition files
    load_definitions(definitions_dir)

    # Validate cross-references
    validate_references()

    structs = get_struct_registry()
    enums = get_enum_registry()

    print(f"Loaded {len(structs)} struct(s) and {len(enums)} enum(s)")

    # TODO (M2+): Generate C++ headers and sources
    for name, s in structs.items():
        print(f"  struct {name} v{s.version} ({len(s.fields)} fields)")
    for name, e in enums.items():
        print(f"  enum {name} ({len(e.values)} values)")


if __name__ == "__main__":
    main()
