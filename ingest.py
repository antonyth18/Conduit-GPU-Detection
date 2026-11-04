#!/usr/bin/env python3
"""
Phase 1 - Ingest and encode.

Reads the four BigQuery exports in data/ (two tx files, two calldata files)
and writes a compact binary corpus that every later phase consumes. Parsing 738 MB of CSV
inside a timed benchmark would swamp the measurements we care about, so this
runs once and the pipeline never touches CSV again.

Outputs, in corpus/:
  tx.bin        fixed 40-byte records, sorted by (day, block_number, tx_index)
  blocks.bin    fixed 12-byte records: block_number, first_tx_index, tx_count
  addresses.txt one 40-hex address per line; line number == interned id
  selectors.txt one selector per line; line number == interned id
  meta.json     counts, ranges, coverage statistics

Record layout (40 bytes, little-endian):
  u32 block_number
  u16 tx_index
  u16 selector_id
  u32 from_id
  u32 to_id          0xFFFFFFFF = contract creation (null to_address)
  u32 arg_addr1      decoded operand 1, 0xFFFFFFFF = absent
  u32 arg_addr2      decoded operand 2, 0xFFFFFFFF = absent
  u64 value_gwei     wei // 1e9, fits u64 for any realistic amount
  u32 gas_used
  u8  status
  u8  tx_type
  u8  flags          bit0 decoded, bit1 arg3 truncated, bit2 malformed arg,
                     bit3 to_address null
  u8  day_id         index into meta["days"]
"""
import csv
import json
import os
import struct
import sys
from pathlib import Path

UPLOADS = Path(os.environ.get("UPLOADS", Path(__file__).parent / "data"))
OUT = Path(__file__).parent / "corpus"

DAYS = [
    ("2026-09-01", "tx_20260901.csv", "calldata_20260901.csv"),
    ("2026-09-13", "tx_20260913.csv", "calldata_20260913.csv"),
]

ABSENT = 0xFFFFFFFF
REC = struct.Struct("<IHHIIIIQIBBBB")
assert REC.size == 40, REC.size

FLAG_DECODED = 1
FLAG_TRUNC = 2
FLAG_MALFORMED = 4
FLAG_CREATE = 8

# Selectors whose calldata we decode. Operand positions differ per selector.
#   transfer(to, amount)             -> arg1 = recipient
#   approve(spender, amount)         -> arg1 = spender
#   transferFrom(from, to, amount)   -> arg1 = sender, arg2 = recipient
SEL_TRANSFER = "0xa9059cbb"
SEL_APPROVE = "0x095ea7b3"
SEL_TRANSFER_FROM = "0x23b872dd"


class Interner:
    """Maps a string to a dense integer id, preserving first-seen order."""

    def __init__(self):
        self.map = {}
        self.list = []

    def get(self, s):
        i = self.map.get(s)
        if i is None:
            i = len(self.list)
            self.map[s] = i
            self.list.append(s)
        return i

    def write(self, path):
        with open(path, "w") as f:
            f.write("\n".join(self.list))
            f.write("\n")


def norm_addr(a):
    """Strip 0x, lowercase. Returns None for empty/null."""
    if not a:
        return None
    a = a.strip()
    if not a or a == "null":
        return None
    return a[2:].lower() if a.startswith("0x") else a.lower()


def hash_key(h):
    """64-bit key from a transaction hash. 1.25M rows, collision risk nil, and
    an int key costs a fraction of what the 66-char string would."""
    return int(h[2:18], 16)


def arg_to_addr(arg):
    """A 64-hex ABI word holding an address: 24 zeros then 40 hex chars.
    Returns (address, ok)."""
    if not arg or arg == '""':
        return None, True
    arg = arg.strip().strip('"')
    if len(arg) != 64:
        return None, False
    if arg[:24] != "0" * 24:
        return None, False
    return arg[24:].lower(), True


def load_calldata(path):
    """hash_key -> (addr1, addr2, flags). Addresses as raw strings; interning
    happens in the main pass so ids stay dense across both days."""
    out = {}
    stats = {"rows": 0, "truncated": 0, "malformed": 0}
    with open(path, newline="") as f:
        r = csv.reader(f)
        next(r)
        for row in r:
            # block_number, hash, selector, arg1, arg2, arg3, input_len
            if len(row) < 7:
                continue
            _, h, sel, a1, a2, a3, ilen = row[:7]
            stats["rows"] += 1
            flags = 0
            try:
                if int(ilen) > 202:
                    flags |= FLAG_TRUNC
                    stats["truncated"] += 1
            except ValueError:
                pass

            if sel == SEL_TRANSFER_FROM:
                d1, ok1 = arg_to_addr(a1)
                d2, ok2 = arg_to_addr(a2)
            else:  # transfer / approve: one address operand
                d1, ok1 = arg_to_addr(a1)
                d2, ok2 = None, True

            if not (ok1 and ok2) or d1 is None:
                flags |= FLAG_MALFORMED
                stats["malformed"] += 1
            else:
                flags |= FLAG_DECODED
            out[hash_key(h)] = (d1, d2, flags)
    return out, stats


def find_by_date(date, kind):
    """A CSV in data/ for this day when the expected name is missing: a
    transaction export whose first row has this block_timestamp date, or a
    calldata export whose file name contains the date (YYYYMMDD)."""
    stamp = date.replace("-", "")
    for p in sorted(UPLOADS.glob("*.csv")):
        with open(p, newline="") as f:
            r = csv.reader(f)
            header = next(r, [])
            row = next(r, [])
        if kind == "tx" and "from_address" in header and "block_timestamp" in header:
            if row and row[header.index("block_timestamp")].startswith(date):
                return p
        if kind == "calldata" and "from_address" not in header and "input_len" in header \
                and stamp in p.name:
            return p
    return None


def write_local_manifest(out):
    """superblocks.json next to the corpus (tools prefer it), and a check that
    the super-block boundaries equal the repository's Phase 6 manifest."""
    from superblocks import TARGETS, build_manifest, dumps
    m = build_manifest(corpus=out, targets=TARGETS)
    (out / "superblocks.json").write_text(dumps(m))
    root = Path(__file__).parent / "superblocks.json"
    if root.exists():
        old = json.load(open(root))
        same_sha = old["corpus"]["tx_bin_sha256"] == m["corpus"]["tx_bin_sha256"]
        o = {e["id"]: e for e in old["superblocks"]}
        n = {e["id"]: e for e in m["superblocks"]}
        same = sum(1 for k in o if n.get(k) == o[k])
        print(f"\nmanifest {out}/superblocks.json: {same} of {len(o)} Phase 6 super-blocks "
              f"have identical boundaries; tx.bin SHA {'matches' if same_sha else 'differs'} "
              "(address ids follow CSV row order, so a re-exported CSV renumbers them; "
              "results do not depend on the numbering)")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    addrs = Interner()
    sels = Interner()

    tx_f = open(OUT / "tx.bin", "wb")
    blk_f = open(OUT / "blocks.bin", "wb")
    blk_s = struct.Struct("<III")

    meta = {"days": [], "record_bytes": REC.size}
    global_idx = 0

    for day_id, (date, tx_name, cd_name) in enumerate(DAYS):
        tx_path = UPLOADS / tx_name
        cd_path = UPLOADS / cd_name
        if not tx_path.exists():
            tx_path = find_by_date(date, "tx") or tx_path
        if not cd_path.exists():
            cd_path = find_by_date(date, "calldata") or cd_path
        for p in (tx_path, cd_path):
            if not p.exists():
                sys.exit(f"missing input: {p}")
        print(f"[{date}] inputs: {tx_path.name}, {cd_path.name}", flush=True)

        print(f"[{date}] loading calldata ...", flush=True)
        calldata, cd_stats = load_calldata(cd_path)
        print(f"[{date}]   {cd_stats['rows']:,} decodable calls, "
              f"{cd_stats['truncated']:,} truncated, "
              f"{cd_stats['malformed']:,} malformed", flush=True)

        print(f"[{date}] reading transactions ...", flush=True)
        rows = []
        day_start = global_idx
        matched = 0

        with open(tx_path, newline="") as f:
            r = csv.reader(f)
            header = next(r)
            col = {name: i for i, name in enumerate(header)}
            for row in r:
                bn = int(row[col["block_number"]])
                ti = int(row[col["transaction_index"]])
                h = row[col["hash"]]
                frm = norm_addr(row[col["from_address"]])
                to = norm_addr(row[col["to_address"]])
                sel = row[col["selector"]] or "0x"
                try:
                    value_gwei = int(row[col["value"]] or 0) // 10**9
                except ValueError:
                    value_gwei = 0
                try:
                    gas = int(row[col["receipt_gas_used"]] or 0)
                except ValueError:
                    gas = 0
                try:
                    status = int(row[col["receipt_status"]] or 0)
                except ValueError:
                    status = 0
                try:
                    ttype = int(row[col["transaction_type"]] or 0)
                except ValueError:
                    ttype = 0

                flags = 0
                if to is None:
                    flags |= FLAG_CREATE
                    # contract creation: the created address is the write target
                    created = norm_addr(row[col["receipt_contract_address"]])
                    to_id = addrs.get(created) if created else ABSENT
                else:
                    to_id = addrs.get(to)

                a1 = a2 = ABSENT
                cd = calldata.get(hash_key(h))
                if cd is not None:
                    matched += 1
                    d1, d2, cdflags = cd
                    flags |= cdflags
                    if d1:
                        a1 = addrs.get(d1)
                    if d2:
                        a2 = addrs.get(d2)

                rows.append((
                    bn, ti, sels.get(sel), addrs.get(frm) if frm else ABSENT,
                    to_id, a1, a2, min(value_gwei, 2**64 - 1), gas,
                    status, ttype, flags, day_id,
                ))

        # Sort into canonical execution order. The export shuffles rows, and
        # block position is the ordering the dependency rule depends on.
        rows.sort(key=lambda t: (t[0], t[1]))

        buf = bytearray()
        blkbuf = bytearray()
        cur_block = None
        block_start = 0
        count = 0
        for i, t in enumerate(rows):
            buf += REC.pack(*t)
            if t[0] != cur_block:
                if cur_block is not None:
                    blkbuf += blk_s.pack(cur_block, block_start, count)
                cur_block = t[0]
                block_start = global_idx + i
                count = 0
            count += 1
        if cur_block is not None:
            blkbuf += blk_s.pack(cur_block, block_start, count)

        tx_f.write(buf)
        blk_f.write(blkbuf)
        nblocks = len(blkbuf) // blk_s.size

        meta["days"].append({
            "date": date,
            "day_id": day_id,
            "transactions": len(rows),
            "blocks": nblocks,
            "first_tx_index": day_start,
            "block_min": rows[0][0],
            "block_max": rows[-1][0],
            "calldata_rows": cd_stats["rows"],
            "calldata_matched": matched,
            "calldata_coverage": round(matched / len(rows), 4),
            "arg_truncated": cd_stats["truncated"],
            "arg_malformed": cd_stats["malformed"],
        })
        global_idx += len(rows)
        print(f"[{date}]   {len(rows):,} tx in {nblocks:,} blocks, "
              f"calldata matched {matched:,} "
              f"({matched/len(rows)*100:.1f}%)", flush=True)
        del rows, calldata

    tx_f.close()
    blk_f.close()
    addrs.write(OUT / "addresses.txt")
    sels.write(OUT / "selectors.txt")

    meta["total_transactions"] = global_idx
    meta["distinct_addresses"] = len(addrs.list)
    meta["distinct_selectors"] = len(sels.list)
    with open(OUT / "meta.json", "w") as f:
        json.dump(meta, f, indent=2)

    print("\ncorpus written to", OUT)
    for p in sorted(OUT.iterdir()):
        print(f"  {p.name:18s} {p.stat().st_size/1e6:10.1f} MB")
    print(f"\n{global_idx:,} transactions, "
          f"{len(addrs.list):,} distinct addresses, "
          f"{len(sels.list):,} distinct selectors")
    write_local_manifest(OUT)


# ---- generic mode: any BigQuery-style transaction export ----------------------
#
#   python3 ingest.py --tx data/my_export.csv [more.csv ...] [--calldata cd.csv ...]
#                     [--out corpus_eth] [--targets 10000,...]
#
# Needs the columns block_number, transaction_index, hash, from_address,
# to_address; uses selector, value, receipt_status, receipt_gas_used,
# receipt_contract_address, transaction_type and block_timestamp when present.
# Rows are de-duplicated on (block_number, transaction_index), sorted into
# execution order and split into days by the date of block_timestamp. Every
# transaction of the input is written; nothing is sampled or dropped.
#
# Without a calldata file, ERC-20 transfer / approve / transferFrom calls have
# no decoded operands (the recipient is unknown), so P2/P3 conservatively
# treat them like any other contract call (contract granularity). Supplying
# the calldata export (the same format as data/calldata_*.csv) restores the
# slot-level keys.
#
# Also writes <out>/superblocks.json: the usual super-block sizes, larger ones
# (250k, 500k, 1M) and one FULL-DAY super-block per day ("<date>/full") that
# covers every transaction of that day.

REQUIRED = ("block_number", "transaction_index", "hash", "from_address", "to_address")
BIG_TARGETS = (250_000, 500_000, 1_000_000)


def _csv_header(path):
    with open(path, newline="") as f:
        return next(csv.reader(f), [])


def _discover(paths):
    """Expand directories; classify CSVs into tx exports and calldata files."""
    files = []
    for p in paths:
        p = Path(p)
        files += sorted(p.glob("*.csv")) if p.is_dir() else [p]
    tx, cd = [], []
    for f in files:
        h = _csv_header(f)
        if all(c in h for c in REQUIRED):
            tx.append(f)
        elif "hash" in h and len(h) >= 7:
            cd.append(f)
        else:
            print(f"  skipping {f}: not a transaction or calldata export (header {h[:6]}...)")
    return tx, cd


def main_files(argv):
    import argparse
    ap = argparse.ArgumentParser(description="Ingest arbitrary transaction exports")
    ap.add_argument("--tx", nargs="+", required=True,
                    help="transaction CSV files or directories (calldata CSVs found there are used too)")
    ap.add_argument("--calldata", nargs="*", default=[], help="calldata CSV files (optional)")
    ap.add_argument("--out", default="corpus_eth")
    ap.add_argument("--targets", default=None,
                    help="super-block sizes (default: 10k..100k, 250k, 500k, 1M, plus full day)")
    args = ap.parse_args(argv)

    tx_files, cd_found = _discover(args.tx)
    cd_files = [Path(c) for c in args.calldata] + [c for c in cd_found if c not in args.calldata]
    if not tx_files:
        sys.exit("no transaction CSV found (needs columns " + ", ".join(REQUIRED) + ")")
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    calldata = {}
    cd_stats = {"rows": 0, "truncated": 0, "malformed": 0}
    for c in cd_files:
        print(f"loading calldata {c} ...", flush=True)
        d, st = load_calldata(c)
        calldata.update(d)
        for k in cd_stats:
            cd_stats[k] += st[k]

    addrs = Interner()
    sels = Interner()
    rows = {}
    stats = {"read": 0, "duplicates": 0, "creates": 0, "matched": 0, "token_calls": 0,
             "token_calls_decoded": 0}
    TOKEN_SELS = (SEL_TRANSFER, SEL_APPROVE, SEL_TRANSFER_FROM)

    def num(row, col, name, default=0):
        i = col.get(name)
        if i is None:
            return default
        try:
            return int(row[i] or 0)
        except ValueError:
            return default

    for path in tx_files:
        print(f"reading {path} ({path.stat().st_size / 1e6:.0f} MB) ...", flush=True)
        with open(path, newline="") as f:
            r = csv.reader(f)
            header = next(r)
            col = {name: i for i, name in enumerate(header)}
            ts = col.get("block_timestamp")
            for row in r:
                if len(row) < len(REQUIRED):
                    continue
                stats["read"] += 1
                bn = int(row[col["block_number"]])
                ti = int(row[col["transaction_index"]])
                if (bn, ti) in rows:
                    stats["duplicates"] += 1
                    continue
                h = row[col["hash"]]
                frm = norm_addr(row[col["from_address"]])
                to = norm_addr(row[col["to_address"]])
                sel = (row[col["selector"]] if "selector" in col else "") or "0x"
                value_gwei = num(row, col, "value") // 10**9
                gas = num(row, col, "receipt_gas_used")
                status = num(row, col, "receipt_status", 1)
                ttype = num(row, col, "transaction_type")
                date = row[ts][:10] if ts is not None and row[ts] else "day0"

                flags = 0
                if to is None:
                    flags |= FLAG_CREATE
                    stats["creates"] += 1
                    ca = col.get("receipt_contract_address")
                    created = norm_addr(row[ca]) if ca is not None else None
                    to_id = addrs.get(created) if created else ABSENT
                else:
                    to_id = addrs.get(to)
                a1 = a2 = ABSENT
                if sel in TOKEN_SELS:
                    stats["token_calls"] += 1
                cd = calldata.get(hash_key(h)) if calldata else None
                if cd is not None:
                    stats["matched"] += 1
                    d1, d2, cdflags = cd
                    flags |= cdflags
                    if d1:
                        a1 = addrs.get(d1)
                    if d2:
                        a2 = addrs.get(d2)
                    if sel in TOKEN_SELS and (cdflags & FLAG_DECODED):
                        stats["token_calls_decoded"] += 1
                rows[(bn, ti)] = (
                    bn, ti, sels.get(sel), addrs.get(frm) if frm else ABSENT,
                    to_id, a1, a2, min(value_gwei, 2**64 - 1), min(gas, 2**32 - 1),
                    status & 0xFF, ttype & 0xFF, flags, date)

    if len(addrs.list) > (1 << 21) - 1:
        sys.exit(f"{len(addrs.list):,} distinct addresses exceed the 21-bit id space "
                 "(policy.hpp MAX_ID); split the input into smaller corpora")
    if len(sels.list) > 0xFFFF:
        sys.exit("more than 65535 distinct selectors")

    keys = sorted(rows)
    if not keys:
        sys.exit("no rows")
    dates = []
    for k in keys:
        if not dates or dates[-1] != rows[k][-1]:
            dates.append(rows[k][-1])
    if len(set(dates)) != len(dates):
        sys.exit("block_timestamp dates are not contiguous in block order")
    if any(t > 0xFFFF for _, t in keys):
        sys.exit("transaction_index above 65535")

    blk_s = struct.Struct("<III")
    meta = {"days": [], "record_bytes": REC.size, "source": [str(p) for p in tx_files],
            "calldata": [str(p) for p in cd_files]}
    idx = 0
    with open(out / "tx.bin", "wb") as tx_f, open(out / "blocks.bin", "wb") as blk_f:
        for day_id, date in enumerate(dates):
            day = [rows[k] for k in keys if rows[k][-1] == date]
            buf = bytearray()
            blkbuf = bytearray()
            cur, start, count = None, idx, 0
            for i, t in enumerate(day):
                buf += REC.pack(*t[:12], day_id)
                if t[0] != cur:
                    if cur is not None:
                        blkbuf += blk_s.pack(cur, start, count)
                    cur, start, count = t[0], idx + i, 0
                count += 1
            blkbuf += blk_s.pack(cur, start, count)
            tx_f.write(buf)
            blk_f.write(blkbuf)
            meta["days"].append({
                "date": date, "day_id": day_id, "transactions": len(day),
                "blocks": len(blkbuf) // blk_s.size, "first_tx_index": idx,
                "block_min": day[0][0], "block_max": day[-1][0],
            })
            print(f"  [{date}] {len(day):,} tx in {len(blkbuf) // blk_s.size:,} blocks", flush=True)
            idx += len(day)

    addrs.write(out / "addresses.txt")
    sels.write(out / "selectors.txt")
    meta.update({
        "total_transactions": idx,
        "distinct_addresses": len(addrs.list),
        "distinct_selectors": len(sels.list),
        "rows_read": stats["read"], "duplicates_dropped": stats["duplicates"],
        "creates": stats["creates"], "calldata_rows": cd_stats["rows"],
        "calldata_matched": stats["matched"], "token_calls": stats["token_calls"],
        "token_calls_decoded": stats["token_calls_decoded"],
    })
    with open(out / "meta.json", "w") as f:
        json.dump(meta, f, indent=2)

    # super-block manifest: standard + large sizes + one full day per day
    import numpy as np
    from superblocks import BLOCK_DTYPE, TARGETS, build_manifest, dumps, max_sender, tile
    smallest = min(d["transactions"] for d in meta["days"])
    if args.targets:
        targets = tuple(int(x) for x in args.targets.split(","))
    else:
        targets = tuple(t for t in TARGETS + BIG_TARGETS if t < smallest)
    manifest = build_manifest(corpus=out, targets=targets)
    tx = np.memmap(out / "tx.bin", dtype=DTYPE_ARR, mode="r")
    blocks = np.fromfile(out / "blocks.bin", dtype=BLOCK_DTYPE)
    off = 0
    for d in meta["days"]:
        db = blocks[off:off + d["blocks"]]
        start, count = int(db["start"][0]), int(db["count"].sum())
        _, top = max_sender(tx["frm"][start:start + count])
        manifest["superblocks"].append({
            "id": f"{d['date']}/full", "date": d["date"], "day_id": d["day_id"],
            "target": count, "sample": 0, "tx_start": start, "tx_count": count,
            "block_first": int(db["block"][0]), "block_last": int(db["block"][-1]),
            "block_count": int(d["blocks"]), "block_index_start": off,
            "missing_blocks": int(db["block"][-1]) - int(db["block"][0]) + 1 - int(d["blocks"]),
            "max_sender_tx": top})
        manifest["targets"].append(count)
        off += d["blocks"]
    manifest["targets"] = sorted(set(manifest["targets"]))
    (out / "superblocks.json").write_text(dumps(manifest))

    print(f"\ncorpus written to {out}/  ({idx:,} transactions, {len(dates)} day(s), "
          f"{len(addrs.list):,} addresses, {len(sels.list):,} selectors)")
    print(f"manifest {out}/superblocks.json: {len(manifest['superblocks'])} super-blocks, "
          f"targets {manifest['targets']}")
    if stats["duplicates"]:
        print(f"note: {stats['duplicates']:,} duplicate (block, index) rows dropped")
    if stats["token_calls"] and stats["token_calls_decoded"] < stats["token_calls"]:
        miss = stats["token_calls"] - stats["token_calls_decoded"]
        why = "no calldata file was given" if not cd_files else \
            "calldata missing, truncated or malformed for these calls"
        print(f"note: {miss:,} of {stats['token_calls']:,} ERC-20 transfer/approve/transferFrom "
              f"calls have no decoded operands ({why}): P2/P3 treat them at contract "
              "granularity" + (", so P2/P3 collapse towards P1" if not cd_files else "") + ".")


DTYPE_ARR = None


if __name__ == "__main__":
    if len(sys.argv) > 1:
        from access_sets import DTYPE as _DT
        DTYPE_ARR = _DT
        main_files(sys.argv[1:])
    else:
        main()
