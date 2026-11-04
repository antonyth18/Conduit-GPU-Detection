#!/usr/bin/env python3
"""
Phase 4 - Comparison harness.

Compares a candidate pipeline output against the oracle's. Both outputs are
{"edges": [(i, j), ...], "levels": [int, ...]}.

A faster implementation may use a reduced edge rule (for example, only the
nearest earlier conflicting writer per key) and so produce fewer edges while
producing identical levels. That is fine as long as every dropped edge is
still IMPLIED: the candidate graph still has a path from i to j, so the pair
stays ordered. Verdicts:

  identical        same edges, same levels
  reduced          fewer edges, all implied, same levels      (pass)
  FAIL levels      level assignments differ                    (first divergence)
  FAIL unimplied   a dropped edge leaves a conflicting pair unordered
  FAIL spurious    candidate has edges between non-conflicting pairs
"""


def reachability(n, edge_list):
    """reach[i] = bitset (Python int) of every vertex reachable from i.
    Edges run forward, so filling i from n-1 down sees successors first."""
    succ = [[] for _ in range(n)]
    for i, j in edge_list:
        succ[i].append(j)
    reach = [0] * n
    for i in range(n - 1, -1, -1):
        r = 0
        for j in succ[i]:
            r |= (1 << j) | reach[j]
        reach[i] = r
    return reach


def compare(ref, cand, n, limit=5):
    ref_e, cand_e = set(ref["edges"]), set(cand["edges"])
    missing = sorted(ref_e - cand_e)
    extra = sorted(cand_e - ref_e)

    reach = reachability(n, sorted(cand_e)) if missing else []
    unimplied = [(i, j) for i, j in missing if not (reach[i] >> j) & 1]

    rl, cl = list(ref["levels"]), list(cand["levels"])
    levels_equal = rl == cl
    first = None
    if not levels_equal:
        if len(rl) != len(cl):
            first = (None, len(rl), len(cl))
        else:
            i = next(i for i in range(len(rl)) if rl[i] != cl[i])
            first = (i, rl[i], cl[i])

    if not levels_equal:
        verdict = "FAIL levels"
    elif unimplied:
        verdict = "FAIL unimplied"
    elif extra:
        verdict = "FAIL spurious"
    elif missing:
        verdict = "reduced"
    else:
        verdict = "identical"

    return {
        "verdict": verdict,
        "ok": not verdict.startswith("FAIL"),
        "edges_ref": len(ref_e),
        "edges_cand": len(cand_e),
        "missing": len(missing),
        "missing_unimplied": len(unimplied),
        "extra": len(extra),
        "first_unimplied": unimplied[:limit],
        "first_extra": extra[:limit],
        "levels_equal": levels_equal,
        "first_level_divergence": first,   # (tx, ref level, cand level)
    }


def describe(result):
    r = result
    s = (f"{r['verdict']}: edges {r['edges_cand']:,} vs ref {r['edges_ref']:,} "
         f"(missing {r['missing']:,}, {r['missing_unimplied']:,} unimplied; "
         f"extra {r['extra']:,}); levels {'match' if r['levels_equal'] else 'differ'}")
    if r["first_level_divergence"]:
        tx, a, b = r["first_level_divergence"]
        s += (f"; level counts {a} vs {b}" if tx is None
              else f"; first divergence at tx {tx}: ref {a}, cand {b}")
    if r["first_unimplied"]:
        s += f"; unimplied e.g. {r['first_unimplied']}"
    if r["first_extra"]:
        s += f"; spurious e.g. {r['first_extra']}"
    return s
