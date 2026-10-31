#!/usr/bin/env python3
"""Verification bridge: reads C++ pipeline dumps (build/conduit_run --dump)
and checks them with the Python oracle tools.

  1. Phase 4 slices (500/1000/2000 tx, both days, all policies): levels must
     equal oracle_ref/ exactly; edges are compared with compare.py, where a
     reduced edge set passes only if every dropped edge is implied. Both
     validators must accept.
  2. Each day's median super-block at every target, up to 100k, where the
     oracle is too slow: validate_fast() must accept, and the critical path
     must not drop below the manifest's single-sender floor.

Access sets for validation come from the Python policies, not the C++ port.

Usage: python3 tools/verify_cpp.py [conduit_run flags...]
Extra flags pass through to conduit_run, e.g. to verify the OpenMP pipeline:
  python3 tools/verify_cpp.py --impl omp --threads 8 --level kahn
"""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from access_sets import POLICIES, Selectors, infer, load_corpus  # noqa: E402
from compare import compare, describe  # noqa: E402
from oracle import access_lists, validate, validate_fast  # noqa: E402
from superblocks import SuperBlocks  # noqa: E402

RUN = ROOT / "build" / "conduit_run"
REF = ROOT / "oracle_ref"


def read_pipeline(path):
    buf = memoryview(open(path, "rb").read())
    if bytes(buf[:4]) != b"CDXP":
        raise ValueError(f"{path}: not a pipeline dump")
    version, pol, n, tx_start, m = struct.unpack_from("<IIIQQ", buf, 4)
    if version != 1:
        raise ValueError(f"{path}: version {version}")
    o = 32
    levels = np.frombuffer(buf, "<u4", n, o)
    o += 4 * n
    row_offsets = np.frombuffer(buf, "<u8", n + 1, o)
    o += 8 * (n + 1)
    cols = np.frombuffer(buf, "<u4", m, o)
    if o + 4 * m != len(buf):
        raise ValueError(f"{path}: size mismatch")
    src = np.repeat(np.arange(n, dtype=np.int64), np.diff(row_offsets).astype(np.int64))
    return {"policy": POLICIES[pol - 1], "n": n, "tx_start": tx_start,
            "levels": levels, "src": src, "cols": cols}


EXTRA = sys.argv[1:]


def run_cpp(args, dump_dir):
    subprocess.run([str(RUN), *EXTRA, "--corpus", str(ROOT / "corpus"),
                    "--manifest", str(ROOT / "superblocks.json"),
                    "--policy", "all", "--runs", "1", "--warmup", "0",
                    "--dump", str(dump_dir)] + args,
                   check=True, stdout=subprocess.DEVNULL)


def main():
    if not RUN.exists():
        sys.exit("build build/conduit_run first (cmake --build build)")
    sb = SuperBlocks()
    sels = Selectors(load_corpus()[3])
    ok = True
    if EXTRA:
        print("conduit_run flags:", " ".join(EXTRA), "\n")

    print("1. Phase 4 slices against the oracle")
    refs = sorted(REF.glob("*.npz"))
    if not refs:
        sys.exit("no oracle_ref/; run report_oracle.py first")
    slices = sorted({(str(np.load(f)["slice_id"]), int(np.load(f)["n"])) for f in refs})
    with tempfile.TemporaryDirectory() as tmp:
        for sid, n in slices:
            run_cpp(["--id", sid, "--limit", str(n)], tmp)
            for policy in POLICIES:
                date = sid.split("/")[0]
                z = np.load(REF / f"{date}_{n}_{policy}.npz")
                c = read_pipeline(Path(tmp) / f"{sid.replace('/', '_')}_{n}_{policy}.bin")
                ref = {"edges": [tuple(e) for e in z["edges"].tolist()],
                       "levels": z["levels"].tolist()}
                cand = {"edges": list(zip(c["src"].tolist(), c["cols"].tolist())),
                        "levels": c["levels"].tolist()}
                r = compare(ref, cand, n)
                tx = np.array(sb.tx[c["tx_start"]:c["tx_start"] + n])
                sets = infer(tx, policy, sels)
                v_brute = validate(access_lists(sets), cand["levels"])[0]
                v_fast = validate_fast(sets, c["levels"])[0]
                good = r["ok"] and r["levels_equal"] and v_brute and v_fast
                ok &= good
                print(f"  {'PASS' if good else 'FAIL'}  {sid}[:{n}] {policy}  {describe(r)}; "
                      f"validators {'accept' if v_brute and v_fast else 'REJECT'}")

    print("\n2. median super-blocks, full size, validate_fast")
    with tempfile.TemporaryDirectory() as tmp:
        run_cpp(["--median"], tmp)
        for f in sorted(Path(tmp).glob("*.bin")):
            c = read_pipeline(f)
            entry = next(e for e in sb.manifest["superblocks"]
                         if e["tx_start"] == c["tx_start"] and e["tx_count"] == c["n"])
            tx = np.array(sb.load(entry))
            sets = infer(tx, c["policy"], sels)
            v, bad = validate_fast(sets, c["levels"])
            cp = int(c["levels"].max()) + 1
            floor = entry["max_sender_tx"]
            good = v and cp >= floor
            ok &= good
            print(f"  {'PASS' if good else 'FAIL'}  {entry['id']:<22}{c['policy']}  "
                  f"n {c['n']:>7,}  edges {len(c['cols']):>9,}  levels {cp:>6,}  "
                  f"floor {floor:>5,}  validator {'accept' if v else 'REJECT ' + str(bad[:2])}")

    print("\n" + ("ALL CHECKS PASSED" if ok else "SOME CHECKS FAILED"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
