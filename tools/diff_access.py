#!/usr/bin/env python3
"""Compares two access-record dumps (tools/dump_access.{py,cpp}). Identical
files pass; otherwise reports the first diverging transaction and record.

Usage: python3 tools/diff_access.py A.bin B.bin
"""
import filecmp
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dump_access import read  # noqa: E402


def diff(path_a, path_b):
    """Returns (identical, message)."""
    if filecmp.cmp(path_a, path_b, shallow=False):
        oa, ka, _ = read(path_a)
        return True, f"identical: {len(oa) - 1:,} tx, {len(ka):,} records"
    oa, ka, ma = read(path_a)
    ob, kb, mb = read(path_b)
    if len(oa) != len(ob):
        return False, f"transaction counts differ: {len(oa) - 1:,} vs {len(ob) - 1:,}"
    bad_off = np.flatnonzero(oa != ob)
    if len(bad_off):
        i = max(int(bad_off[0]) - 1, 0)
        return False, (f"access-set sizes diverge at tx {i}: "
                       f"{oa[i + 1] - oa[i]} vs {ob[i + 1] - ob[i]} records")
    bad = np.flatnonzero((ka != kb) | (ma != mb))
    if len(bad):
        r = int(bad[0])
        i = int(np.searchsorted(oa, r, side="right")) - 1
        return False, (f"{len(bad):,} records differ; first at record {r} (tx {i}): "
                       f"key {int(ka[r]):#x}/{int(kb[r]):#x}, mode {ma[r]}/{mb[r]}")
    return False, "arrays equal but files differ (header or padding)"


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    ok, msg = diff(sys.argv[1], sys.argv[2])
    print(("PASS  " if ok else "FAIL  ") + msg)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
