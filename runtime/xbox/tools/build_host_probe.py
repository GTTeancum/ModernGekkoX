#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a complete generated host diagnostic with content-checked dependencies.

No game is bundled. All C files in the supplied generated directory are linked.
Caches include compiler identity, exact argv, *all* -MD header dependencies, and
object bytes. This deliberately does not import unproven checkpoint caches.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import time

def machine_outliner_flags(enabled):
    # Experimental LLVM backend option. Keep this opt-in and part of the
    # content-checked command key; it never changes generated source coverage.
    return ["-mllvm", "-enable-machine-outliner=always"] if enabled else []

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def dependency_paths(text, cwd):
    # Generated target names do not contain a colon; escaped spaces in input
    # paths are decoded by shlex. Windows drive-letter builds are not supported.
    flat = text.replace("\\\n", " ")
    if ":" not in flat:
        raise ValueError("compiler dependency file lacks target separator")
    paths = shlex.split(flat.split(":", 1)[1], posix=True)
    if not paths:
        raise ValueError("empty compiler dependency set")
    return sorted({str((Path(cwd) / p).resolve()) for p in paths})

def compile_cached(source, obj, command, compiler_identity, cwd):
    source, obj, cwd = Path(source).resolve(), Path(obj).resolve(), Path(cwd).resolve()
    obj.parent.mkdir(parents=True, exist_ok=True)
    depfile, recordfile = obj.with_suffix(".d"), obj.with_suffix(".json")
    key = {"compiler": compiler_identity, "argv": command, "cwd": str(cwd)}
    try:
        old = json.loads(recordfile.read_text())
        if (old["key"] == key and old["source"] == str(source) and
            old["object_sha256"] == sha(obj) and str(source) in old["dependencies"] and
            all(sha(p) == value for p, value in old["dependencies"].items())):
            return dict(old, reused=True)
    except (OSError, ValueError, KeyError, TypeError):
        pass
    recordfile.unlink(missing_ok=True)
    obj.unlink(missing_ok=True)
    depfile.unlink(missing_ok=True)
    actual = command + ["-MD", "-MF", str(depfile), "-c", str(source), "-o", str(obj)]
    result = subprocess.run(actual, cwd=cwd, capture_output=True, text=True, timeout=120)
    obj.with_suffix(".log").write_text(result.stdout + result.stderr)
    if result.returncode:
        obj.unlink(missing_ok=True)
        raise RuntimeError(f"Compile failed: {source}\n{result.stderr[-4000:]}")
    deps = dependency_paths(depfile.read_text(), cwd)
    if str(source) not in deps:
        obj.unlink(missing_ok=True)
        raise ValueError("compiler omitted source from dependency set")
    record = {"key": key, "source": str(source),
              "dependencies": {p: sha(p) for p in deps},
              "object": str(obj), "object_sha256": sha(obj), "reused": False}
    recordfile.write_text(json.dumps(record, indent=2) + "\n")
    return record

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler-source", type=Path, required=True)
    parser.add_argument("--generated", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--object-cache", type=Path, help="Separate content-checked object cache (defaults to output)")
    parser.add_argument("--template-profile", type=Path, help="Private instrumented template header")
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--optimization", choices=["0", "1", "2", "s", "z"], default="0")
    parser.add_argument("--machine-outliner", action="store_true",
                        help="experimental LLVM machine outlining for smaller code; disabled by default")
    parser.add_argument("--jobs", type=int, default=2)
    a = parser.parse_args()
    if not 1 <= a.jobs <= 16:
        parser.error("--jobs must be in 1..16")
    cc = shutil.which(a.cc)
    if not cc:
        parser.error("compiler not found")
    cc = str(Path(cc).resolve())
    version = subprocess.check_output([cc, "--version"], text=True)
    identity = {"path": cc, "sha256": sha(cc), "version": version}
    runtime = Path(__file__).resolve().parents[1]
    source, generated, output = a.compiler_source.resolve()/"src", a.generated.resolve(), a.output.resolve()
    headers = sorted(generated.glob("*.h"))
    if len(headers) != 1:
        parser.error("expected one generated module header")
    module = headers[0].stem
    chunks = sorted((generated/"chunks").glob("*.c"))
    if not chunks:
        parser.error("generated chunks directory is empty")
    files = chunks + [generated/(module+".c"), source/"cpu/cpu.c",
        runtime/"src/mgx_dol.c", runtime/"src/mgx_exec.c", runtime/"src/mgx_math.c",
        runtime/"startup-probe/bridge.c", runtime/"startup-probe/main.c"]
    if any(not f.is_file() for f in files):
        parser.error("required source file missing")
    output.mkdir(parents=True, exist_ok=True)
    cache = a.object_cache.resolve() if a.object_cache else output
    cache.mkdir(parents=True, exist_ok=True)
    (output/"startup-probe").unlink(missing_ok=True)
    common = [cc, "-std=c11", "-O"+a.optimization, "-fno-fast-math",
        "-ffp-contract=off", "-frounding-math", *machine_outliner_flags(a.machine_outliner), "-I"+str(source),
        "-I"+str(runtime/"include"), "-I"+str(generated)]
    def build(pair):
        index, f = pair
        command = common.copy()
        if f == source/"cpu/cpu.c":
            command += ["-include", str(runtime/"include/mgx_math_redirect.h")]
        if f == runtime/"startup-probe/bridge.c":
            command += ['-DMGX_GENERATED_HEADER="'+module+'.h"']
        if f == runtime/"startup-probe/main.c" and a.template_profile:
            command += ['-DMGX_TEMPLATE_PROFILE_HEADER="'+str(a.template_profile.resolve())+'"']
        return compile_cached(f, cache/(f"{index:04d}-"+f.stem+".o"), command, identity, cache)
    start = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        records = list(pool.map(build, enumerate(files)))
    executable, temporary = output/"startup-probe", output/"startup-probe.tmp"
    temporary.unlink(missing_ok=True)
    link = [cc] + [r["object"] for r in records] + ["-lm", "-o", str(temporary)]
    result = subprocess.run(link, capture_output=True, text=True, timeout=120)
    (output/"link.log").write_text(result.stdout+result.stderr)
    if result.returncode:
        temporary.unlink(missing_ok=True)
        raise RuntimeError("link failed\n"+result.stderr[-4000:])
    temporary.replace(executable)
    report = {"machine_outliner": a.machine_outliner, "generated_chunks": len(chunks), "object_cache": str(cache), "objects": records,
        "reused": sum(r["reused"] for r in records), "link_command": link,
        "executable": str(executable), "executable_sha256": sha(executable),
        "compiler": identity, "elapsed_seconds": time.monotonic()-start,
        "all_generated_chunks_linked": True, "executed": False,
        "xbox_hardware_tested": False}
    (output/"build-report.json").write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps({k:v for k,v in report.items() if k not in ("objects","compiler","link_command")}))
if __name__ == "__main__":
    main()
