#!/usr/bin/env python3
"""Python side of the cross-language access-record check. Writes the same
format as tools/dump_access.cpp:

  "CDXA" | u32 version=1 | u64 n | u64 m | i64 offsets[n+1] | u64 keys[m] | u8 modes[m]

Usage: python3 tools/dump_access.py --policy P2 --out FILE [--id SUPERBLOCK_ID]
Without --id, the whole corpus (inferred one day at a time to bound memory).
"""
import argparse
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from access_sets import Selectors, infer, load_corpus  # noqa: E402

MAGIC = b"CDXA"


def write(path, offsets, keys, modes):
    with open(path, "wb") as f:
        f.write(MAGIC + struct.pack("<IQQ", 1, len(offsets) - 1, len(keys)))
        f.write(np.ascontiguousarray(offsets, "<i8").tobytes())
        f.write(np.ascontiguousarray(keys, "<u8").tobytes())
        f.write(np.ascontiguousarray(modes, "u1").tobytes())


def read(path):
    buf = memoryview(open(path, "rb").read())
    if bytes(buf[:4]) != MAGIC:
        raise ValueError(f"{path}: not an access dump")
    version, n, m = struct.unpack_from("<IQQ", buf, 4)
    if version != 1:
        raise ValueError(f"{path}: version {version}")
    o = 24
    offsets = np.frombuffer(buf, "<i8", n + 1, o)
    o += 8 * (n + 1)
    keys = np.frombuffer(buf, "<u8", m, o)
    o += 8 * m
    modes = np.frombuffer(buf, "u1", m, o)
    if o + m != len(buf):
        raise ValueError(f"{path}: {len(buf) - o - m} trailing bytes")
    return offsets, keys, modes


def dump_corpus(policy, path):
    tx, meta, _, sel_names = load_corpus()
    sels = Selectors(sel_names)
    offs, keys, modes = [np.zeros(1, np.int64)], [], []
    base = 0
    for d in meta["days"]:
        s = d["first_tx_index"]
        sets = infer(tx[s:s + d["transactions"]], policy, sels)
        offs.append(sets.offsets[1:] + base)
        keys.append(sets.keys)
        modes.append(sets.modes)
        base += len(sets.keys)
    write(path, np.concatenate(offs), np.concatenate(keys), np.concatenate(modes))


def dump_superblock(policy, sb_id, path):
    from superblocks import SuperBlocks
    sb = SuperBlocks()
    sels = Selectors(load_corpus()[3])
    sets = infer(np.array(sb.load(sb_id)), policy, sels)
    write(path, sets.offsets, sets.keys, sets.modes)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--policy", required=True, choices=("P1", "P2", "P3"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--id")
    a = ap.parse_args()
    if a.id:
        dump_superblock(a.policy, a.id, a.out)
    else:
        dump_corpus(a.policy, a.out)


if __name__ == "__main__":
    main()
