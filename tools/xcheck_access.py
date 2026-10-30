#!/usr/bin/env python3
"""Cross-language check: the C++ policy port must emit byte-identical access
records to access_sets.py. This is what lets the Python oracle keep vouching
for the C++ pipeline.

Checks, for each policy:
  - the whole corpus (every transaction once; the policies are pure
    per-transaction functions, so this covers every super-block's content);
  - each day's median super-block at every target, loaded through the C++
    super-block path, so slicing is covered too.
Dumps go to xcheck/ and are deleted on success, kept on failure.

Usage: python3 tools/xcheck_access.py   (after building build/dump_access)
"""
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from diff_access import diff  # noqa: E402
from dump_access import dump_corpus, dump_superblock  # noqa: E402

CPP = ROOT / "build" / "dump_access"
OUT = ROOT / "xcheck"


def median_ids():
    m = json.load(open(ROOT / "superblocks.json"))
    groups = {}
    for e in m["superblocks"]:
        groups.setdefault((e["date"], e["target"]), []).append(e["id"])
    return [ids[len(ids) // 2] for ids in groups.values()]


def main():
    if not CPP.exists():
        sys.exit(f"build {CPP.relative_to(ROOT)} first (cmake --build build)")
    OUT.mkdir(exist_ok=True)
    ok = True
    t0 = time.time()
    for policy in ("P1", "P2", "P3"):
        cases = [("corpus", None)] + [(i.replace("/", "_"), i) for i in median_ids()]
        for tag, sb_id in cases:
            py, cpp = OUT / f"py_{policy}_{tag}.bin", OUT / f"cpp_{policy}_{tag}.bin"
            if sb_id:
                dump_superblock(policy, sb_id, py)
            else:
                dump_corpus(policy, py)
            cmd = [str(CPP), "--policy", policy, "--out", str(cpp),
                   "--corpus", str(ROOT / "corpus"), "--manifest", str(ROOT / "superblocks.json")]
            if sb_id:
                cmd += ["--id", sb_id]
            subprocess.run(cmd, check=True)
            same, msg = diff(py, cpp)
            ok &= same
            print(f"  {'PASS' if same else 'FAIL'}  {policy}  {sb_id or 'whole corpus':<22} {msg}",
                  flush=True)
            if same:
                py.unlink()
                cpp.unlink()
    print(f"\n{'ALL BYTE-IDENTICAL' if ok else 'DIVERGENCE FOUND (dumps kept in xcheck/)'}"
          f"  ({time.time() - t0:.0f} s)")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
