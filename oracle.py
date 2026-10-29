#!/usr/bin/env python3
"""
Phase 4 - Correctness oracle.

A deliberately slow, obviously correct reference. Never timed, never
optimised: later phases are checked against it, so the only goal is that it
is clearly right.

Input is a list with one access list per transaction, in block order; an
access list is [(state_key, mode), ...] with each key at most once, exactly as
Phase 2 emits it (access_lists() converts an AccessSets).

Definitions (the contract every later phase must satisfy):
  conflict    ti and tj conflict if they share a key whose pair of modes is
              not READ/READ and not COMMUTATIVE/COMMUTATIVE.
  edge        ti -> tj for every conflicting pair with i < j (a DAG).
  level       longest path, in edges, ending at the transaction; a
              transaction with no incoming edge has level 0. The critical
              path is max(level) + 1, counted in transactions.
  valid       a level assignment is valid iff every conflicting pair i < j
              has level(i) < level(j). That excludes conflicts inside a level
              and order inversions across levels.

The three parts - edges(), levels(), validate() - share nothing except
conflicts(), so a bug in one cannot hide a bug in another.
"""
import numbers

from access_sets import COMMUTATIVE, READ, WRITE

MODES = (READ, COMMUTATIVE, WRITE)


def conflicts(mode_a, mode_b):
    """The conflict rule, written straight from the definition."""
    if mode_a not in MODES or mode_b not in MODES:
        raise ValueError(f"unknown mode {mode_a!r} / {mode_b!r}")
    if mode_a == READ and mode_b == READ:
        return False
    if mode_a == COMMUTATIVE and mode_b == COMMUTATIVE:
        return False
    return True


def access_lists(sets):
    """AccessSets (Phase 2) -> list of [(key, mode), ...] per transaction."""
    return [sets[i] for i in range(len(sets))]


def check_access(access):
    """Each key at most once per transaction, every mode known."""
    for i, acc in enumerate(access):
        keys = [k for k, _ in acc]
        if len(keys) != len(set(keys)):
            raise ValueError(f"tx {i} lists a key more than once")
        for _, m in acc:
            if m not in MODES:
                raise ValueError(f"tx {i} has unknown mode {m!r}")


# ---- 1. brute-force conflict detection --------------------------------------

def pair_conflicts(a, b):
    """Compare every access of a with every access of b."""
    for key_a, mode_a in a:
        for key_b, mode_b in b:
            if key_a == key_b and conflicts(mode_a, mode_b):
                return True
    return False


def edges(access):
    """Every conflicting pair (i, j), i < j, in ascending (i, j) order."""
    check_access(access)
    n = len(access)
    out = []
    for i in range(n):
        for j in range(i + 1, n):
            if pair_conflicts(access[i], access[j]):
                out.append((i, j))
    return out


# ---- 2. brute-force levelling -------------------------------------------------

def levels(n, edge_list):
    """Longest path ending at each transaction. Edges run from lower to higher
    index, so visiting j in increasing order sees every predecessor first."""
    preds = [[] for _ in range(n)]
    for i, j in edge_list:
        if not 0 <= i < j < n:
            raise ValueError(f"edge ({i}, {j}) is not forward within {n} tx")
        preds[j].append(i)
    level = [0] * n
    for j in range(n):
        for i in preds[j]:
            level[j] = max(level[j], level[i] + 1)
    return level


def critical_path(level):
    return max(level) + 1 if level else 0


def batch_sizes(level):
    """Number of transactions in each level, level 0 first."""
    sizes = [0] * critical_path(level)
    for lv in level:
        sizes[lv] += 1
    return sizes


# ---- 3. schedule validator ----------------------------------------------------

def validate(access, level, limit=10):
    """Check a level assignment from ANY implementation against the access
    sets alone. Returns (ok, violations); each violation is
    (i, j, key, reason) and at most `limit` are kept."""
    n = len(access)
    if len(level) != n:
        return False, [(None, None, None,
                        f"{len(level)} levels for {n} transactions")]
    for i, lv in enumerate(level):
        if not isinstance(lv, numbers.Integral) or isinstance(lv, bool) or lv < 0:
            return False, [(i, None, None, f"level {lv!r} is not an int >= 0")]

    bad = []
    count = 0
    for i in range(n):
        modes_i = {}
        for k, m in access[i]:
            modes_i[k] = m
        for j in range(i + 1, n):
            for k, m in access[j]:
                if k in modes_i and conflicts(modes_i[k], m):
                    if level[i] >= level[j]:
                        reason = ("same level" if level[i] == level[j]
                                  else "order inverted")
                        count += 1
                        if len(bad) < limit:
                            bad.append((i, j, k, reason))
                    break
    if count > len(bad):
        bad.append((None, None, None, f"... {count - len(bad)} more"))
    return count == 0, bad


# ---- whole pipeline -------------------------------------------------------------

def run(access):
    """Reference output for one slice: {"edges", "levels"}."""
    e = edges(access)
    return {"edges": e, "levels": levels(len(access), e)}
