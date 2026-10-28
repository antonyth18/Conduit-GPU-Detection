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
        for p in (tx_path, cd_path):
            if not p.exists():
                sys.exit(f"missing input: {p}")

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


if __name__ == "__main__":
    main()
