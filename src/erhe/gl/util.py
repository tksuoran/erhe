"""Utility functions."""
import os
import re

FIRST_CAP_RE = re.compile(r'(.)([A-Z][a-z]+)')
ALL_CAP_RE   = re.compile(r'([a-z0-9])([A-Z0-9])')

def read(name) -> str:
    """Read file contents to string."""
    try:
        script_path = os.path.dirname(os.path.realpath(__file__))
        path = os.path.join(script_path, "templates")
        path = os.path.join(path, name)
        with open(path, 'r') as in_file:
            return in_file.read()
    except Exception as exception:
        print("Error reading {} from {}: {}".format(name, path, str(exception   )))
        raise

def sjoin(collection) -> str:
    """Sort and join collection of strings into lines."""
    return '\n'.join(sorted(collection))

def to_snake_case(text) -> str:
    """Convert string to snake_case."""
    initial_string = FIRST_CAP_RE.sub(r'\1_\2', text)
    return ALL_CAP_RE.sub(r'\1_\2', initial_string).lower()

def to_int(value) -> int:
    """Convert base 10 or base 16 string to int."""
    if value.startswith('0x'):
        return int(value, base=16)
    return int(value)
