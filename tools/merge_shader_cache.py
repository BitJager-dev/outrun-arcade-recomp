#!/usr/bin/env python3
"""Merge the SDK's shareable shader caches from several installs into one.

  tools/merge_shader_cache.py <out_dir> <cache_dir> [<cache_dir> ...]

A cache dir is userdata/cache/shaders/shareable/ of an install. It holds
<TITLE>.xsh (guest shaders: 8-byte header, then records of u64 XXH3 of the
ucode, u32 dword count:31 | type:1, ucode) and <TITLE>.<fbo|fsi>.vk.xpso
(pipelines: 12-byte header, then fixed-size records of u64 XXH3 of the
description + description). Records are checked against their hash and kept
once, first-seen order. The game compiles everything in the merged cache at
startup instead of the first time it is drawn (no hitches mid-race). The
files don't depend on the GPU. They contain the game's shader code: keep them
next to your game data, not in git.

Needs: python3 with the xxhash module (Arch: python-xxhash).
"""
import pathlib
import struct
import sys

import xxhash


def xsh_records(data):
    pos, recs = 8, []
    while pos + 12 <= len(data):
        h, w = struct.unpack_from("<QI", data, pos)
        end = pos + 12 + 4 * (w & 0x7FFFFFFF)
        if end > len(data) or xxhash.xxh3_64_intdigest(data[pos + 12:end]) != h:
            break
        recs.append((h, data[pos:end]))
        pos = end
    return recs


def xpso_record_size(data):
    for size in range(16, 1024):
        if (len(data) - 12) % size == 0 and struct.unpack_from("<Q", data, 12)[0] == \
                xxhash.xxh3_64_intdigest(data[20:12 + size]):
            return size
    return None


def xpso_records(data):
    size, recs = xpso_record_size(data), []
    if not size:
        return recs
    for pos in range(12, len(data) - size + 1, size):
        h = struct.unpack_from("<Q", data, pos)[0]
        if h != xxhash.xxh3_64_intdigest(data[pos + 8:pos + size]):
            break
        recs.append((h, data[pos:pos + size]))
    return recs


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out, srcs = pathlib.Path(sys.argv[1]), [pathlib.Path(p) for p in sys.argv[2:]]
    out.mkdir(parents=True, exist_ok=True)
    names = sorted({f.name for s in srcs for f in s.glob("*.xsh")} |
                   {f.name for s in srcs for f in s.glob("*.vk.xpso")})
    for name in names:
        parse = xsh_records if name.endswith(".xsh") else xpso_records
        header_len = 8 if name.endswith(".xsh") else 12
        header, seen, body = None, set(), []
        for src in srcs:
            f = src / name
            if not f.exists():
                continue
            data = f.read_bytes()
            if header is None:
                header = data[:header_len]
            elif data[:header_len] != header:
                print(f"{f}: different format version, skipped")
                continue
            recs = parse(data)
            new = [r for h, r in recs if h not in seen]
            seen.update(h for h, _ in recs)
            body += new
            print(f"{f}: {len(recs)} records, {len(new)} new")
        (out / name).write_bytes(header + b"".join(body))
        print(f"-> {out / name}: {len(body)} records")


if __name__ == "__main__":
    main()
