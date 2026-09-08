#!/usr/bin/env python3
"""Composed-stage facts of one USD file, read with OpenUSD's own `pxr`
module and printed as one JSON object - the ground truth the USD-WG survey
(scripts/usd_wg_asset_survey.py) compares the erhe editor's load against.

Runs under the Python that ships with an OpenUSD build (`<usd_root>`), not
under `py -3`; the survey script launches it that way:

    <usd_root>/scripts/set_usd_env.bat && python scripts/usd_wg_pxr_stage.py <file>

Output fields:
  prims            prims on the composed stage, instance proxies included
  meshes           UsdGeomMesh prims (the count erhe should reach)
  gprims           every UsdGeomGprim (Mesh, Cube, Sphere, ..., Points, Curves)
  point_instancers UsdGeomPointInstancer prims
  materials        UsdShadeMaterial prims
  lights           prims carrying the UsdLux LightAPI
  cameras          UsdGeomCamera prims, in traversal order (paths)
  bounds_min/max   world-space AABB of the default and render purposes at the
                   stage's start time code, in stage units and stage up axis
  up_axis, meters_per_unit, start_time_code, has_authored_time_codes
  errors           what pxr reported while opening, if anything
"""
import json
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    path = sys.argv[1]
    from pxr import Tf, Usd, UsdGeom, UsdLux, UsdShade

    errors = []
    delegate = None
    try:
        delegate = Tf.CapturingDiagnosticDelegate()  # OpenUSD >= 24.x
    except AttributeError:
        pass
    stage = Usd.Stage.Open(path)
    if stage is None:
        print(json.dumps({"path": path, "errors": ["Usd.Stage.Open returned None"]}))
        return 1

    result = {
        "path": path,
        "prims": 0,
        "meshes": 0,
        "gprims": 0,
        "point_instancers": 0,
        "materials": 0,
        "lights": 0,
        "cameras": [],
        "prim_types": {},
        "up_axis": str(UsdGeom.GetStageUpAxis(stage)),
        "meters_per_unit": float(UsdGeom.GetStageMetersPerUnit(stage)),
        "start_time_code": float(stage.GetStartTimeCode()),
        "has_authored_time_codes": bool(stage.HasAuthoredTimeCodeRange()),
        "default_prim": str(stage.GetDefaultPrim().GetPath()) if stage.GetDefaultPrim() else "",
        "bounds_min": None,
        "bounds_max": None,
        "errors": [],
    }
    try:
        cache = UsdGeom.BBoxCache(Usd.TimeCode(stage.GetStartTimeCode()),
                                  [UsdGeom.Tokens.default_, UsdGeom.Tokens.render], useExtentsHint=False)
        box = cache.ComputeWorldBound(stage.GetPseudoRoot()).ComputeAlignedRange()
        if not box.IsEmpty():
            result["bounds_min"] = [float(v) for v in box.GetMin()]
            result["bounds_max"] = [float(v) for v in box.GetMax()]
    except Exception as error:  # a stage whose bounds cannot be computed still has counts
        errors.append(f"bounds: {error}"[:300])
    for prim in stage.Traverse(Usd.TraverseInstanceProxies(Usd.PrimDefaultPredicate)):
        result["prims"] += 1
        type_name = prim.GetTypeName() or "(typeless)"
        result["prim_types"][type_name] = result["prim_types"].get(type_name, 0) + 1
        if prim.IsA(UsdGeom.Mesh):
            result["meshes"] += 1
        if prim.IsA(UsdGeom.Gprim):
            result["gprims"] += 1
        if prim.IsA(UsdGeom.PointInstancer):
            result["point_instancers"] += 1
        if prim.IsA(UsdShade.Material):
            result["materials"] += 1
        if prim.HasAPI(UsdLux.LightAPI):
            result["lights"] += 1
        if prim.IsA(UsdGeom.Camera):
            result["cameras"].append(str(prim.GetPath()))
    if delegate is not None:
        for diagnostic in delegate.TakeUncoalescedDiagnostics():
            errors.append(str(diagnostic.commentary)[:300])
    result["errors"] = errors
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    sys.exit(main())
