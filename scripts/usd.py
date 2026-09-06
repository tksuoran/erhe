"""Print the USD prim schema type tree known to the installed usd-core.

Lists every schema type deriving from UsdTyped with its `.usda` typeName
(concrete types) or marked abstract, followed by its inheritance chain up
to UsdTyped. Requires `py -3 -m pip install usd-core`.

    py -3 scripts/usd.py            # every type
    py -3 scripts/usd.py Geom Lux   # only types whose C++ name contains one of the words
"""

import sys

from pxr import Tf, Usd


def main(argv):
    filters = argv[1:]
    usd_typed_type = Tf.Type.FindByName("UsdTyped")
    if usd_typed_type.isUnknown:
        print("UsdTyped is not registered; is usd-core installed?")
        return 1

    # GetAllDerivedTypes() returns the types; it takes no output argument.
    derived_types = usd_typed_type.GetAllDerivedTypes()
    print(f"Discovered {len(derived_types)} schema types deriving from UsdTyped.")

    for tf_type in sorted(derived_types, key=lambda t: t.typeName):
        if filters and not any(word in tf_type.typeName for word in filters):
            continue
        concrete_name = Usd.SchemaRegistry.GetConcreteSchemaTypeName(tf_type)
        # GetAllAncestorTypes() starts with the type itself; stop at UsdTyped.
        chain = []
        for ancestor in tf_type.GetAllAncestorTypes()[1:]:
            if ancestor == usd_typed_type:
                break
            chain.append(ancestor.typeName)
        if concrete_name:
            print(f"Prim Type: {concrete_name} (Class: {tf_type.typeName})")
        else:
            print(f"Abstract:  {tf_type.typeName}")
        if chain:
            print(f"   inherits: {' -> '.join(chain)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
