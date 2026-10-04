"""Summarize the newest Crash Diagnostic Layer dump against logs/log.txt.

Prints the fault address, the command the GPU stopped on, the faulting render
pass with its attachments (from the `erhe.graphics.render_pass` debug trace),
the bind / unbind history of the fault page (from the `erhe.graphics.debug`
`[VA]` trace), the live mappings around it and the fault's offset from each
attachment of the faulting pass. Run from the repository root after a device
loss under the Crash Diagnostic Layer with those log categories at debug
(doc/agents/debugging.md "GPU faults").

    py -3 scripts/gpu_fault_report.py [--dump <cdl_dump.yaml>] [--log logs/log.txt]
"""
import argparse
import glob
import os
import re
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dump", help="cdl_dump.yaml to read (default: newest under ~/cdl)")
    parser.add_argument("--log", default=os.path.join("logs", "log.txt"))
    arguments = parser.parse_args()

    dump_path = arguments.dump
    if dump_path is None:
        dumps = sorted(glob.glob(os.path.join(os.path.expanduser("~"), "cdl", "*", "cdl_dump.yaml")))
        if not dumps:
            print("no dump found under ~/cdl")
            return 1
        dump_path = dumps[-1]
    dump = open(dump_path, encoding="utf-8", errors="replace").read()
    log = open(arguments.log, encoding="utf-8", errors="replace").read().splitlines()
    print("dump:", dump_path)

    match = re.search(r"type: Invalid (Write|Read)\s+begin: (0x[0-9A-Fa-f]+)", dump)
    fault = int(match.group(2), 16) if match else None
    print(f"fault: {match.group(1) if match else 'none'} at {'0x%x' % fault if fault is not None else '-'}")

    stop = dump.find("LAST STARTED")
    names = re.findall(r"name: (vkCmd\w+)", dump[max(0, stop - 4000):stop])
    print("last started command:", names[-1] if names else "?")

    attachments = []
    passes = re.findall(r'renderPass: "0x([0-9A-F]+)\[Render_pass_impl off-screen: ([^\]]+)\]', dump[:stop])
    if passes:
        handle, name = passes[-1]
        handle = handle.lstrip("0").lower()
        print("faulting pass:", name, "render_pass=0x" + handle)
        for line in log:
            if f"render_pass=0x{handle}" in line:
                print("  ", line.strip()[14:400])
                attachments = re.findall(r"'([^']*)'=image 0x([0-9a-f]+) layer (\d+)", line)

    if fault is None:
        return 0
    binding = re.compile(r"\[VA\] (bind|unbind)\s+0x([0-9a-f]+)\.\.0x([0-9a-f]+) size=0x([0-9a-f]+)(.*)")
    live = {}
    print("---- bind / unbind history of the fault page")
    for line in log:
        m = binding.search(line)
        if not m:
            continue
        begin = int(m.group(2), 16)
        end = int(m.group(3), 16)
        if begin <= fault < end:
            print("  ", line[1:13], m.group(1), f"0x{begin:x}..0x{end:x}", m.group(5).strip()[:110])
        if m.group(1) == "bind":
            live[(begin, end)] = (line[1:13], m.group(5).strip())
        else:
            live.pop((begin, end), None)
    names_by_image = {}
    for line in log:
        m = re.search(r"Texture created: '([^']*)'.*image=0x([0-9a-f]+)", line)
        if m:
            names_by_image[m.group(2)] = m.group(1)
    print("---- live mappings within 64 MiB of the fault")
    for (begin, end), (when, objects) in sorted(live.items()):
        if abs(begin - fault) < 64 * 1024 * 1024:
            image = re.search(r"IMAGE=0x([0-9a-f]+)", objects)
            label = names_by_image.get(image.group(1), "") if image else ""
            mark = " <-- contains fault" if begin <= fault < end else ""
            print(f"   {when} 0x{begin:x}..0x{end:x} size=0x{end-begin:x} {objects[:60]} '{label}'{mark}")
    print("---- faulting pass attachments: range and fault offset")
    for name, image, layer in attachments:
        for (begin, end), (when, objects) in live.items():
            if f"IMAGE=0x{image}" in objects:
                sign = "+" if fault >= end else "-"
                print(f"   '{name}' layer {layer}: 0x{begin:x}..0x{end:x} size=0x{end-begin:x} fault-base=0x{fault-begin:x} fault-end={sign}0x{abs(fault-end):x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
