"""Offline GPU compiler analytics for a shader variant browser directory
(doc/erhe/graphics.md "Shader variant browser", "Mali analytics" and
"PowerVR analytics").

Reads <browser-directory>/variants/manifest.json (written by the editor run
with --shader-variant-browser), runs the vendor offline compilers on every
recorded program before and after the SPIRV-Tools optimizer, keeps each raw
JSON report next to the modules, and writes <browser-directory>/analytics.js,
the sidecar index.html and the per-program pages render when it is present.

Two compilers, chosen per core by its name:

- Mali-<name> (Arm Mali Offline Compiler, malioc): found through --malioc,
  the ERHE_MALIOC path list (executables or directories), or "malioc" on
  PATH. One compile per stage module; report
  variants/<n>_<stage>[_optimized].<core>.malioc.json.
- PowerVR-<GPU> or PowerVR-<BVNC> (Imagination PowerVR profiling compiler,
  ProfilingCompilerCLIWrapper, with its compiler libraries in the same
  directory): found through --powervr, the ERHE_POWERVR path list, or PATH.
  One compile per stage module; report
  variants/<n>_<stage>[_optimized].<core>.powervr.json.

Without --core every compiler found contributes its default core: Mali-G310
(the weakest Mali core that accepts erhe's modules) and PowerVR-BXM-8-256
(the one PowerVR core of the SDK that accepts them). --core may repeat. A
report newer than its module(s) with the same core and compiler version is
not compiled again unless --force. A failed compile is a result
too: the error is kept in the report (malioc: every "Unsupported capability
(<n>)" resolved to the SPIR-V capability name; a compiler crash is recorded as
an error naming the signal).

    py -3 scripts/shader_analytics.py <browser-directory> [--core Mali-G310]
        [--core PowerVR-BXM-8-256] [--malioc <path>] [--powervr <path>]
        [--jobs N] [--force] [--check]

--check re-reads the analytics.js it wrote and verifies that every program
and stage of the manifest has a before entry, and an after entry where the
optimized module(s) exist. Exit code 0 when every module produced a report,
1 when a compiler or the manifest is missing or a compiler reports a command
line error.
"""

import argparse
import concurrent.futures
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

DEFAULT_MALI_CORE = "Mali-G310"
DEFAULT_POWERVR_CORE = "PowerVR-BXM-8-256"
POWERVR_EXE = "ProfilingCompilerCLIWrapper"
SIDECAR_SCHEMA_VERSION = 2

# SPIR-V capability numbers the error reports name (spirv.hpp); only the
# ones erhe's modules declare and a few neighbours.
CAPABILITY_NAMES = {
    1: "Shader",
    11: "Float16",
    22: "Int64",
    23: "Int16",
    32: "ClipDistance",
    33: "CullDistance",
    45: "SampledCubeArray",
    49: "StorageImageExtendedFormats",
    50: "ImageQuery",
    51: "DerivativeControl",
    61: "GroupNonUniform",
    4423: "SubgroupBallotKHR",
    4427: "DrawParameters",
    4433: "StorageBuffer16BitAccess",
    4434: "UniformAndStorageBuffer16BitAccess",
    4437: "DeviceGroup",
    4439: "MultiView",
    4441: "VariablePointersStorageBuffer",
    4472: "RayQueryKHR",
    5301: "ShaderNonUniform",
    5302: "RuntimeDescriptorArray",
    5345: "VulkanMemoryModel",
    5347: "PhysicalStorageBufferAddresses",
    5379: "DemoteToHelperInvocation",
    5391: "RayQueryPositionFetchKHR",
}


def compiler_of(core):
    if core.startswith("Mali-"):
        return "malioc"
    if core.startswith("PowerVR-"):
        return "powervr"
    sys.exit(f"shader_analytics: core {core} names no Mali (Mali-<name>) or PowerVR (PowerVR-<GPU>) GPU")


def find_tool(explicit, env_name, exe_name):
    # env_name holds a path list; each entry is the executable or a directory holding it.
    file_name = exe_name + (".exe" if os.name == "nt" else "")
    candidates = [explicit]
    for entry in os.environ.get(env_name, "").split(os.pathsep):
        if entry:
            entry_path = Path(os.path.expandvars(os.path.expanduser(entry)))
            candidates.append(str(entry_path / file_name) if entry_path.is_dir() else str(entry_path))
    candidates.append(shutil.which(exe_name))
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(Path(candidate).resolve())
    return None


def write_json_via_temp(path, data):
    temp = path.with_suffix(path.suffix + ".tmp")
    with open(temp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)
    os.replace(temp, path)


def read_report(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError):
        return None


# ---------------------------------------------------------------- Mali (malioc)

def run_json(command):
    """Run malioc with --format json; returns (exit_code, parsed_json_or_None, raw_output)."""
    completed = subprocess.run(command, capture_output=True, text=True)
    output = completed.stdout if completed.stdout.strip() else completed.stderr
    parsed = None
    if output.strip().startswith("{"):
        try:
            parsed = json.loads(output)
        except json.JSONDecodeError:
            parsed = None
    return completed.returncode, parsed, output


def query_malioc(malioc, cores):
    """The compiler version, after validating the cores, or exits."""
    code, listing, raw = run_json([malioc, "--list", "--format", "json"])
    if code != 0 or listing is None:
        sys.exit(f"shader_analytics: {malioc} --list failed ({code}):\n{raw[:500]}")
    producer = listing.get("producer", {})
    version = ".".join(str(x) for x in producer.get("version", []))
    # list schema version 0: {"cores": [{"core": "Mali-G310", "apis": [...]}, ...]}
    known = {entry.get("core") for entry in listing.get("cores", []) if isinstance(entry, dict) and entry.get("core")}
    for core in cores:
        if known and core not in known:
            sys.exit(f"shader_analytics: unknown core {core}; malioc --list names: {', '.join(sorted(known))}")
    return version


def resolve_capability_names(message):
    """'Unsupported capability (5302)' -> 'Unsupported capability (5302 RuntimeDescriptorArray)'."""
    out = message
    start = 0
    while True:
        i = out.find("Unsupported capability (", start)
        if i < 0:
            return out
        j = out.find(")", i)
        if j < 0:
            return out
        number = out[i + len("Unsupported capability (") : j]
        if number.isdigit() and int(number) in CAPABILITY_NAMES:
            out = out[:j] + " " + CAPABILITY_NAMES[int(number)] + out[j:]
        start = j + 1


def malioc_report_is_current(report_path, module_path, core, version):
    if not report_path.is_file():
        return False
    if report_path.stat().st_mtime < module_path.stat().st_mtime:
        return False
    report = read_report(report_path)
    if report is None:
        return False
    erhe = report.get("erhe", {})
    return erhe.get("core") == core and erhe.get("malioc_version") == version


def compile_malioc(malioc, version, job):
    """Runs malioc on one module and writes the raw report with an 'erhe' block."""
    module_path, report_path, core, entry_point, specialization_constants = job
    command = [malioc, "--vulkan", "--spirv", "-c", core, "--format", "json"]
    if entry_point and entry_point != "main":
        command += ["-n", entry_point]
    for constant in specialization_constants:
        command += ["-S", f"{constant['location']}={constant['value']}"]
    command.append(str(module_path))
    try:
        completed = subprocess.run(command, capture_output=True, text=True)
    except OSError as error:
        return False, f"cannot run {malioc}: {error}"
    output = completed.stdout if completed.stdout.strip() else completed.stderr
    report = None
    if output.strip().startswith("{"):
        try:
            report = json.loads(output)
        except json.JSONDecodeError:
            report = None
    if completed.returncode == 2:
        return False, f"malioc command line error for {module_path.name}:\n{output[:800]}"
    if report is None:
        # A crash (negative return code: signal) or non-JSON output.
        if completed.returncode < 0:
            message = f"compiler crashed with signal {-completed.returncode}"
        else:
            message = f"compiler exited {completed.returncode} without a JSON report: {output.strip()[:300]}"
        report = {"schema": {"name": "error", "version": 0}, "shaders": [{"errors": [message]}]}
    report["erhe"] = {
        "core": core,
        "malioc_version": version,
        "module": module_path.name,
        "command": " ".join(command),
        "exit_code": completed.returncode,
    }
    write_json_via_temp(report_path, report)
    return True, None


def summarize_malioc_report(report):
    """The sidecar's per-module summary from a raw malioc performance or error report."""
    erhe = report.get("erhe", {})
    summary = {"command": erhe.get("command", ""), "error": None, "variants": [], "properties": {}, "attribute_streams": None, "warnings": [], "notes": []}
    shaders = report.get("shaders", [])
    if report.get("schema", {}).get("name") != "performance" or not shaders:
        messages = []
        for shader in shaders:
            for error in shader.get("errors", []):
                # error schema version 0: a list of strings
                messages.append(resolve_capability_names(error if isinstance(error, str) else json.dumps(error)))
        summary["error"] = "; ".join(messages) if messages else "no performance report"
        return summary, None
    shader = shaders[0]
    hardware = shader.get("hardware", {})
    pipelines = [p.get("name") for p in hardware.get("pipelines", [])]
    core_info = {
        "revision": hardware.get("revision"),
        "architecture": hardware.get("architecture"),
        "driver": shader.get("driver"),
        "pipelines": pipelines,
    }
    for prop in shader.get("properties", []):
        summary["properties"][prop.get("name")] = prop.get("value")
    for variant in shader.get("variants", []):
        props = {p.get("name"): p.get("value") for p in variant.get("properties", [])}
        performance = variant.get("performance", {})
        # A variant's cycle_count arrays follow the variant's own "pipelines"
        # order, which is not the hardware block's order; the sidecar stores
        # them in the hardware order so one list names every column.
        variant_pipelines = performance.get("pipelines", pipelines)

        def cycles(key):
            entry = performance.get(key, {})
            counts = entry.get("cycle_count")
            if counts is not None and variant_pipelines != pipelines:
                by_name = dict(zip(variant_pipelines, counts))
                counts = [by_name.get(name) for name in pipelines]
            return counts, [b for b in entry.get("bound_pipelines", []) if b]

        total, bound_total = cycles("total_cycles")
        shortest, bound_shortest = cycles("shortest_path_cycles")
        longest, bound_longest = cycles("longest_path_cycles")
        summary["variants"].append({
            "name": variant.get("name"),
            "work_registers": props.get("work_registers_used"),
            "uniform_registers": props.get("uniform_registers_used"),
            "occupancy": props.get("thread_occupancy"),
            "stack_size": props.get("stack_size"),
            "has_spilling": props.get("has_stack_spilling"),
            "spill_bytes": props.get("stack_spill_bytes"),
            "alloca_bytes": props.get("stack_alloca_bytes"),
            "fp16": props.get("fp16_arithmetic"),
            "total": total,
            "bound_total": bound_total,
            "shortest": shortest,
            "bound_shortest": bound_shortest,
            "longest": longest,
            "bound_longest": bound_longest,
        })
    summary["attribute_streams"] = shader.get("attribute_streams")
    summary["warnings"] = [w if isinstance(w, str) else json.dumps(w) for w in shader.get("warnings", [])]
    summary["notes"] = [n if isinstance(n, str) else json.dumps(n) for n in shader.get("notes", [])]
    return summary, core_info


def plan_malioc_jobs(manifest, variants_dir, cores, version, force):
    """(module path, report path, core, entry point, spec constants) for every module to compile."""
    jobs = []
    skipped = 0
    for program in manifest.get("programs", []):
        for stage in program.get("stages", []):
            for key in ("spirv", "optimized_spirv"):
                name = stage.get(key)
                if not name:
                    continue
                module_path = variants_dir / name
                if not module_path.is_file():
                    print(f"shader_analytics: missing module {module_path}", file=sys.stderr)
                    continue
                for core in cores:
                    report_path = variants_dir / f"{Path(name).stem}.{core}.malioc.json"
                    if not force and malioc_report_is_current(report_path, module_path, core, version):
                        skipped += 1
                        continue
                    jobs.append((module_path, report_path, core, stage.get("entry_point", "main"), stage.get("specialization_constants", [])))
    return jobs, skipped


def build_malioc_core_block(manifest, variants_dir, core, version, generated):
    block = {"core": core, "compiler": "malioc", "revision": None, "architecture": None, "driver": None, "pipelines": [], "malioc_version": version, "generated": generated, "programs": {}}
    missing = 0
    for program in manifest.get("programs", []):
        program_block = {}
        for stage in program.get("stages", []):
            stage_block = {"before": None, "after": None}
            for key, slot in (("spirv", "before"), ("optimized_spirv", "after")):
                name = stage.get(key)
                if not name:
                    continue
                report = read_report(variants_dir / f"{Path(name).stem}.{core}.malioc.json")
                if report is None:
                    missing += 1
                    continue
                summary, core_info = summarize_malioc_report(report)
                stage_block[slot] = summary
                if core_info and not block["pipelines"]:
                    block.update(core_info)
            program_block[stage.get("type")] = stage_block
        block["programs"][str(program.get("id"))] = program_block
    return block, missing


# ------------------------------------------------------ PowerVR (ProfilingCompilerCLIWrapper)

# Counters whose report entry has no long_name.
POWERVR_COUNTER_LABELS = {"uNaiveCycleCount": "Naive Cycle Count"}


def query_powervr_listing(tool):
    """[(bvnc, gpu, description)] from the wrapper's -h BVNC list ("35.3.408.101: Series B BXM-8-256, MediaTek Dimensity 930")."""
    completed = subprocess.run([tool, "-h"], capture_output=True, text=True)
    listing = []
    for line in (completed.stdout + completed.stderr).splitlines():
        match = re.match(r"\s*(\d+\.\d+\.\d+\.\d+)\s*:\s*(.+)$", line)
        if match:
            description = match.group(2).strip()
            gpu = description.split(",")[0].split()[-1]
            listing.append((match.group(1), gpu, description))
    return listing


def resolve_powervr_bvnc(core, listing):
    """PowerVR-BXM-8-256 or PowerVR-35.3.408.101 -> (bvnc, None), or (None, error)."""
    name = core[len("PowerVR-") :]
    if re.fullmatch(r"\d+\.\d+\.\d+\.\d+", name):
        return name, None
    bvncs = [bvnc for bvnc, gpu, _ in listing if gpu == name]
    if len(bvncs) == 1:
        return bvncs[0], None
    known = ", ".join(sorted({f"PowerVR-{gpu}" for _, gpu, _ in listing}))
    if len(bvncs) > 1:
        return None, f"{core} names several BVNCs ({', '.join(bvncs)}); use PowerVR-<BVNC>"
    return None, f"unknown core {core}; the wrapper's -h list names: {known}"


def powervr_library(tool, bvnc):
    """The SPIR-V compiler library of one BVNC next to the wrapper, or None."""
    matches = sorted(Path(tool).parent.glob(f"*spvcompiler_sdk.{bvnc}.*"))
    return matches[0] if matches else None


def powervr_library_version(library):
    """The DDK build the library embeds ("rogueddk MAIN@6451761" -> "MAIN@6451761"), or None."""
    match = re.search(rb"rogueddk ([A-Za-z0-9_.@-]+)", library.read_bytes())
    return match.group(1).decode("ascii") if match else None


def query_powervr(tool, cores):
    """{core: (bvnc, library version)}, after validating each core has its SPIR-V library, or exits."""
    listing = query_powervr_listing(tool)
    if not listing:
        sys.exit(f"shader_analytics: {tool} -h listed no BVNCs")
    result = {}
    for core in cores:
        bvnc, error = resolve_powervr_bvnc(core, listing)
        if error:
            sys.exit(f"shader_analytics: {error}")
        library = powervr_library(tool, bvnc)
        if library is None:
            sys.exit(f"shader_analytics: no SPIR-V compiler library for {core} (BVNC {bvnc}) next to {tool}")
        version = powervr_library_version(library)
        if version is None:
            sys.exit(f"shader_analytics: {library} names no rogueddk build")
        result[core] = (bvnc, version)
    return result


def powervr_report_is_current(report_path, module_path, core, version):
    if not report_path.is_file():
        return False
    if report_path.stat().st_mtime < module_path.stat().st_mtime:
        return False
    report = read_report(report_path)
    if report is None:
        return False
    erhe = report.get("erhe", {})
    return erhe.get("core") == core and erhe.get("powervr_version") == version


def compile_powervr(tool, core_versions, job):
    """Runs the wrapper on one module and writes the raw report with an 'erhe' block."""
    module_path, report_path, core, entry_point, specialization_constants = job
    bvnc, version = core_versions[core]
    if (entry_point and entry_point != "main") or specialization_constants:
        return False, f"PowerVR: the compiler takes no entry point or specialization constants ({module_path.name})"
    command = [tool, "-a", "spv", "-b", bvnc, "-p", str(Path(tool).parent), "-i", str(module_path)]
    # The report is written into the working directory as <module file name>.json.
    try:
        with tempfile.TemporaryDirectory(prefix="erhe_powervr_") as work_dir:
            completed = subprocess.run(command, capture_output=True, text=True, cwd=work_dir)
            report = read_report(Path(work_dir) / (module_path.name + ".json"))
    except OSError as error:
        return False, f"cannot run {tool}: {error}"
    messages = []
    if report is None or not isinstance(report.get("shaders"), dict):
        # stdout carries the disassembly on success, the error line on failure.
        messages = [line.strip() for line in (completed.stdout + "\n" + completed.stderr).splitlines() if line.strip()]
        if any("libraries not found" in m for m in messages):
            return False, f"PowerVR compiler libraries not found for {report_path.name}:\n" + "\n".join(messages[:10])
        if completed.returncode < 0:
            messages.insert(0, f"compiler crashed with signal {-completed.returncode}")
        elif not messages:
            messages.insert(0, f"compiler exited {completed.returncode} without a report")
        report = {"output": "failed", "shaders": {}}
    report["erhe"] = {
        "core": core,
        "bvnc": bvnc,
        "powervr_version": version,
        "module": module_path.name,
        "command": " ".join(command),
        "exit_code": completed.returncode,
        "messages": messages,
    }
    write_json_via_temp(report_path, report)
    return True, None


def summarize_powervr_report(report):
    """The sidecar's per-module summary from a raw PowerVR report."""
    erhe = report.get("erhe", {})
    summary = {"command": erhe.get("command", ""), "error": None, "variants": [], "messages": erhe.get("messages", [])}
    shaders = report.get("shaders")
    if not isinstance(shaders, dict) or not shaders:
        summary["error"] = "; ".join(erhe.get("messages", [])) or "no report"
        return summary, None
    information = report.get("information", {})
    core_info = {"gpu": information.get("gpu"), "architecture": information.get("architecture"), "soc": information.get("soc")}
    for name, shader in shaders.items():
        counters = shader.get("counters", {}) if isinstance(shader, dict) else {}
        values = {key: entry.get("value") for key, entry in counters.items() if isinstance(entry, dict)}
        summary["variants"].append({
            "name": name,
            "counters": [[(entry.get("long_name") or POWERVR_COUNTER_LABELS.get(key) or key), entry.get("value")] for key, entry in counters.items() if isinstance(entry, dict)],
            "cycles": values.get("uNaiveCycleCount"),
            "instructions": values.get("uInstructionCount"),
            "temp_registers": values.get("uTempRegisterCount"),
            "samples": values.get("uSamples"),
            "utilisation": values.get("fUSCUtilisation"),
        })
    return summary, core_info


def plan_powervr_jobs(manifest, variants_dir, core_versions, force):
    """(module path, report path, core, entry point, spec constants) for every module to compile."""
    jobs = []
    skipped = 0
    for program in manifest.get("programs", []):
        for stage in program.get("stages", []):
            for key in ("spirv", "optimized_spirv"):
                name = stage.get(key)
                if not name:
                    continue
                module_path = variants_dir / name
                if not module_path.is_file():
                    print(f"shader_analytics: missing module {module_path}", file=sys.stderr)
                    continue
                for core, (_, version) in core_versions.items():
                    report_path = variants_dir / f"{Path(name).stem}.{core}.powervr.json"
                    if not force and powervr_report_is_current(report_path, module_path, core, version):
                        skipped += 1
                        continue
                    jobs.append((module_path, report_path, core, stage.get("entry_point", "main"), stage.get("specialization_constants", [])))
    return jobs, skipped


def build_powervr_core_block(manifest, variants_dir, core, bvnc, version, generated):
    block = {"core": core, "compiler": "powervr", "bvnc": bvnc, "gpu": None, "architecture": None, "soc": None, "powervr_version": version, "generated": generated, "programs": {}}
    missing = 0
    for program in manifest.get("programs", []):
        program_block = {}
        for stage in program.get("stages", []):
            stage_block = {"before": None, "after": None}
            for key, slot in (("spirv", "before"), ("optimized_spirv", "after")):
                name = stage.get(key)
                if not name:
                    continue
                report = read_report(variants_dir / f"{Path(name).stem}.{core}.powervr.json")
                if report is None:
                    missing += 1
                    continue
                summary, core_info = summarize_powervr_report(report)
                stage_block[slot] = summary
                if core_info and not block["gpu"]:
                    block.update(core_info)
            program_block[stage.get("type")] = stage_block
        block["programs"][str(program.get("id"))] = program_block
    return block, missing


# ------------------------------------------------------------------- sidecar

def load_manifest(directory):
    path = directory / "variants" / "manifest.json"
    if not path.is_file():
        sys.exit(f"shader_analytics: {path} not found; record a browser directory with the editor's --shader-variant-browser option first")
    with open(path, encoding="utf-8") as f:
        manifest = json.load(f)
    if manifest.get("schema", {}).get("name") != "erhe_shader_variants":
        sys.exit(f"shader_analytics: {path} is not an erhe shader variant manifest")
    return manifest


def write_sidecar(directory, sidecar):
    path = directory / "analytics.js"
    # '<' escaped so no "</script>" can end the element that loads this file.
    text = "window.erhe_shader_analytics = " + json.dumps(sidecar, indent=1).replace("<", "\\u003c") + ";\n"
    temp = path.with_suffix(".js.tmp")
    with open(temp, "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(temp, path)
    return path


def check_sidecar(directory, manifest):
    """Re-reads analytics.js as JSON and verifies its coverage of the manifest."""
    path = directory / "analytics.js"
    with open(path, encoding="utf-8") as f:
        text = f.read()
    prefix = "window.erhe_shader_analytics = "
    if not text.startswith(prefix) or not text.rstrip().endswith(";"):
        return [f"{path}: unexpected framing"]
    sidecar = json.loads(text[len(prefix) :].rstrip().rstrip(";"))
    problems = []
    for core, core_block in sidecar["cores"].items():
        for program in manifest.get("programs", []):
            program_block = core_block["programs"].get(str(program["id"]))
            if program_block is None:
                problems.append(f"{core}: program {program['id']} missing")
                continue
            for stage in program.get("stages", []):
                stage_block = program_block.get(stage["type"])
                if stage_block is None:
                    problems.append(f"{core}: program {program['id']} stage {stage['type']} missing")
                    continue
                if stage_block["before"] is None:
                    problems.append(f"{core}: program {program['id']} {stage['type']}: no before entry")
                expect_after = bool(stage.get("optimized_spirv"))
                if expect_after and stage_block["after"] is None:
                    problems.append(f"{core}: program {program['id']} {stage['type']}: no after entry")
    return problems


def run_jobs(label, function, jobs, worker_count):
    failures = []
    if not jobs:
        return failures
    with concurrent.futures.ThreadPoolExecutor(max_workers=worker_count) as pool:
        futures = [pool.submit(function, job) for job in jobs]
        done = 0
        for future in concurrent.futures.as_completed(futures):
            ok, message = future.result()
            done += 1
            if not ok:
                failures.append(message)
            if done % 50 == 0 or done == len(jobs):
                print(f"shader_analytics: {label} {done}/{len(jobs)}")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("directory", type=Path, help="the shader variant browser directory (holds index.html and variants/)")
    parser.add_argument("--core", action="append", help=f"target GPU, Mali-<name> or PowerVR-<GPU> (repeatable; default {DEFAULT_MALI_CORE} and {DEFAULT_POWERVR_CORE} for each compiler found)")
    parser.add_argument("--malioc", help="path to the malioc executable (else $ERHE_MALIOC, else PATH)")
    parser.add_argument("--powervr", help=f"path to the PowerVR {POWERVR_EXE} executable, its compiler libraries beside it (else $ERHE_POWERVR, else PATH)")
    parser.add_argument("--jobs", type=int, default=max(1, min(8, os.cpu_count() or 1)), help="parallel compiler processes")
    parser.add_argument("--force", action="store_true", help="compile every module even when its report is current")
    parser.add_argument("--check", action="store_true", help="after writing analytics.js, verify it covers the manifest")
    args = parser.parse_args()

    directory = args.directory.resolve()
    variants_dir = directory / "variants"
    manifest = load_manifest(directory)
    malioc = find_tool(args.malioc, "ERHE_MALIOC", "malioc")
    powervr = find_tool(args.powervr, "ERHE_POWERVR", POWERVR_EXE)
    if args.core:
        cores = list(dict.fromkeys(args.core))
        if any(compiler_of(c) == "malioc" for c in cores) and malioc is None:
            sys.exit("shader_analytics: malioc not found; pass --malioc, set ERHE_MALIOC, or put it on PATH")
        if any(compiler_of(c) == "powervr" for c in cores) and powervr is None:
            sys.exit(f"shader_analytics: {POWERVR_EXE} not found; pass --powervr, set ERHE_POWERVR, or put it on PATH")
    else:
        cores = ([DEFAULT_MALI_CORE] if malioc else []) + ([DEFAULT_POWERVR_CORE] if powervr else [])
        if not cores:
            sys.exit(f"shader_analytics: no malioc or {POWERVR_EXE} found; pass --malioc / --powervr, set ERHE_MALIOC / ERHE_POWERVR, or put them on PATH")
    mali_cores = [c for c in cores if compiler_of(c) == "malioc"]
    powervr_cores = [c for c in cores if compiler_of(c) == "powervr"]

    tools = manifest.get("tools", {})
    if tools.get("api") not in (None, "vulkan"):
        print(f"shader_analytics: the manifest's modules were compiled for api '{tools.get('api')}'; they are analyzed as Vulkan modules", file=sys.stderr)

    generated = datetime.datetime.now().isoformat(timespec="seconds")
    sidecar = {
        "schema": {"name": "erhe_shader_analytics", "version": SIDECAR_SCHEMA_VERSION},
        "generated": generated,
        "manifest_tools": tools,
        "default_core": cores[0],
        "default_cores": {},
        "cores": {},
    }
    failures = []
    missing = 0

    if mali_cores:
        malioc_version = query_malioc(malioc, mali_cores)
        jobs, skipped = plan_malioc_jobs(manifest, variants_dir, mali_cores, malioc_version, args.force)
        print(f"shader_analytics: {malioc} {malioc_version}; cores {', '.join(mali_cores)}; {len(jobs)} module(s) to compile, {skipped} current")
        failures += run_jobs("malioc", lambda job: compile_malioc(malioc, malioc_version, job), jobs, args.jobs)
        sidecar.update({"malioc": malioc, "malioc_version": malioc_version})
        sidecar["default_cores"]["malioc"] = mali_cores[0]
        for core in mali_cores:
            block, core_missing = build_malioc_core_block(manifest, variants_dir, core, malioc_version, generated)
            sidecar["cores"][core] = block
            missing += core_missing

    if powervr_cores:
        core_versions = query_powervr(powervr, powervr_cores)
        jobs, skipped = plan_powervr_jobs(manifest, variants_dir, core_versions, args.force)
        described = ", ".join(f"{core} (BVNC {bvnc}, {version})" for core, (bvnc, version) in core_versions.items())
        print(f"shader_analytics: {powervr}; cores {described}; {len(jobs)} module(s) to compile, {skipped} current")
        failures += run_jobs("powervr", lambda job: compile_powervr(powervr, core_versions, job), jobs, args.jobs)
        sidecar.update({"powervr": powervr})
        sidecar["default_cores"]["powervr"] = powervr_cores[0]
        for core in powervr_cores:
            bvnc, version = core_versions[core]
            block, core_missing = build_powervr_core_block(manifest, variants_dir, core, bvnc, version, generated)
            sidecar["cores"][core] = block
            missing += core_missing

    for message in failures:
        print(f"shader_analytics: {message}", file=sys.stderr)

    path = write_sidecar(directory, sidecar)
    for core, core_block in sidecar["cores"].items():
        entries = 0
        errors = 0
        for program_block in core_block["programs"].values():
            for stage_block in program_block.values():
                for slot in ("before", "after"):
                    if stage_block[slot] is not None:
                        entries += 1
                        if stage_block[slot]["error"]:
                            errors += 1
        print(f"shader_analytics: {core}: {entries} stage report(s), {errors} with compile errors")
    print(f"shader_analytics: wrote {path}: {len(manifest.get('programs', []))} program(s), {missing} report(s) missing")

    if args.check:
        problems = check_sidecar(directory, manifest)
        for problem in problems:
            print(f"shader_analytics: check: {problem}", file=sys.stderr)
        if problems:
            return 1
        print("shader_analytics: check passed")
    return 1 if (failures or missing) else 0


if __name__ == "__main__":
    sys.exit(main())
